#include <RendeerAlpha.h>
#include <unordered_map>
#include <Core/Core.h>
#include <Core/LoopWork.h>
#include <Logger/Logger.h>
#include <Core/Framework.h>
#include <Core/BackendConnector.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Renderer.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Texture.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <new>

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <glfw3.h>

namespace RDA {
	RDA::AppInfo applicationInfo;
	stack_list windowList;
	Window* mainWindow = nullptr;
	VkInstance appInstance;
	Renderer gRenderer;
	Scene gScene;
	RDA_DEBUG_FUNC(VkDebugUtilsMessengerEXT debugMessenger);

	// Loop control. gRunning is cleared by rendeerStop(); gLoopThread only exists in
	// Owned mode, where rendeerWait() joins it.
	std::atomic<bool> gRunning{ false };
	std::thread       gLoopThread;

	// OnDemand redraw: set by rendeerRequestRedraw(), consumed once per frame. Atomic
	// because it may be requested from another thread while the loop runs in Owned mode.
	std::atomic<bool> gRedrawRequested{ true };
	// Set by either form of rendeerRequestRedraw(), and only to break the idle wait. The
	// request itself lives in gRedrawRequested or on the window; this just says the pump
	// should stop waiting and let the loop look.
	std::atomic<bool> gWakeRequested{ false };
	std::atomic<bool> gGlfwReady{ false }; // guards glfwPostEmptyEvent() before/after init
	// Loop iterations in which anything was drawn, and in which nothing was.
	uint64_t gFramesRendered = 0, gFramesSkipped = 0; // loop-thread only
	// Individual window draws. With per-window gating these are the numbers that show the
	// saving: on a runtime serving several applications, one busy window no longer drags
	// the idle ones through a frame, and only this pair can tell you so.
	uint64_t gWindowFramesDrawn = 0, gWindowFramesSkipped = 0;

	// Double/triple click detection state (loop thread only).
	// Per window: a click in one window must not count towards a double click in
	// another, and each window has its own pointer position anyway.
	struct ClickHistory {
		double    time = -1.0;
		glm::vec2 pos{ 0.0f };
		int       count = 1;
	};
	std::unordered_map<const RDA::Window*, ClickHistory> gClickHistory;

	// The renderer is built against a surface, so it cannot exist before some window
	// does. An application that opens no window at startup — a runtime serving other
	// processes, say — brings it up with its first window instead, which is why the
	// settings it needs are kept here rather than read from a config that has gone.
	struct DeferredRendererSetup {
		std::string  fontPath;
		float        fontHeight = 18.0f;
		ViewportMode viewportMode = ViewportMode::Fullscreen;
		bool         guiEnabled = true;
	};
	DeferredRendererSetup gRendererSetup;
	bool gRendererReady = false;
	// Whether the application's life is tied to having a window. An ordinary app ends
	// when its last window closes; a service that opens windows on behalf of other
	// processes has none most of the time and must not take that for "finished".
	bool gWindowDependent = true;

	// Gathers one window's input and opens its GUI frame. Every window owns its own Gui
	// and its own Input, so this is per window rather than per application: the runtime
	// gives each client a window of its own, and each has to be driven independently.
	void beginWindowGui(RDA::Window& window, RDA::GuiInput& gi, float dtSeconds) {
		using namespace RDA;
				Input& in = window.input();
		// Reset only what is accumulated below; every other field is assigned.
		gi.typed.clear();
		gi.editKeys.clear();
		gi.copy = gi.cut = gi.paste = gi.selectAll = gi.submit = false;
		gi.pointer = window.cursorToFramebuffer(in.mousePosition());
		VkExtent2D ext = window.cachedExtent();
		gi.viewport = { static_cast<float>(ext.width), static_cast<float>(ext.height) };
		gi.down = in.isMouseButtonDown(0);      // GLFW_MOUSE_BUTTON_LEFT
		gi.pressed = in.mouseButtonPressed(0);
		gi.released = in.mouseButtonReleased(0);

		// Click runs: presses close together in both time and space count up, so a
		// widget can tell a double click from two separate ones. Tracked here
		// rather than in the GUI because the timing is a platform concern.
		if (gi.pressed) {
			constexpr double kDoubleClickSeconds = 0.4;
			constexpr float  kSlopPixels = 6.0f;
			const double now = glfwGetTime();
			ClickHistory& clicks = gClickHistory[&window];
			const glm::vec2 delta = gi.pointer - clicks.pos;
			const bool sameSpot = std::abs(delta.x) <= kSlopPixels &&
			                      std::abs(delta.y) <= kSlopPixels;
			if (now - clicks.time <= kDoubleClickSeconds && sameSpot) {
				clicks.count = (clicks.count >= 3) ? 1 : clicks.count + 1;
			} else {
				clicks.count = 1;
			}
			clicks.time = now;
			clicks.pos = gi.pointer;
		}
		gi.clickCount = gClickHistory[&window].count;
		gi.scroll = in.scroll().y;
		gi.dt = dtSeconds;
		gi.shift = in.isKeyDown(GLFW_KEY_LEFT_SHIFT) || in.isKeyDown(GLFW_KEY_RIGHT_SHIFT);
		gi.ctrl = in.isKeyDown(GLFW_KEY_LEFT_CONTROL) || in.isKeyDown(GLFW_KEY_RIGHT_CONTROL);
		gi.alt = in.isKeyDown(GLFW_KEY_LEFT_ALT) || in.isKeyDown(GLFW_KEY_RIGHT_ALT);

		// Distill typed text and edit keys from this frame's event queue, which
		// carries key repeats (held backspace/arrows) that the polled edges miss.
		for (const InputEvent& e : in.events()) {
			if (e.type == InputEventType::Char) {
				if (e.codepoint >= 32 && e.codepoint < 127) {
					gi.typed.push_back(static_cast<char>(e.codepoint));
				}
			} else if (e.type == InputEventType::Key &&
			           (e.action == InputAction::Press || e.action == InputAction::Repeat)) {
				switch (e.key) {
				case GLFW_KEY_BACKSPACE: gi.editKeys.insert(GuiEditKey::Backspace); break;
				case GLFW_KEY_DELETE:    gi.editKeys.insert(GuiEditKey::Delete);    break;
				case GLFW_KEY_LEFT:      gi.editKeys.insert(GuiEditKey::Left);       break;
				case GLFW_KEY_RIGHT:     gi.editKeys.insert(GuiEditKey::Right);      break;
				case GLFW_KEY_UP:        gi.editKeys.insert(GuiEditKey::Up);         break;
				case GLFW_KEY_DOWN:      gi.editKeys.insert(GuiEditKey::Down);       break;
				case GLFW_KEY_HOME:      gi.editKeys.insert(GuiEditKey::Home);       break;
				case GLFW_KEY_END:       gi.editKeys.insert(GuiEditKey::End);        break;
				case GLFW_KEY_ENTER:
				case GLFW_KEY_KP_ENTER:
					if (gi.ctrl) gi.submit = true;
					gi.editKeys.insert(GuiEditKey::Enter);
					break;
				case GLFW_KEY_TAB:       gi.editKeys.insert(GuiEditKey::Tab);        break;
				case GLFW_KEY_C: if (gi.ctrl) gi.copy = true;      break;
				case GLFW_KEY_X: if (gi.ctrl) gi.cut = true;       break;
				case GLFW_KEY_V: if (gi.ctrl) gi.paste = true;     break;
				case GLFW_KEY_A: if (gi.ctrl) gi.selectAll = true; break;
				default: break;
				}
			}
		}
		window.gui().begin(gi);
	}


	// Defined below, next to the frame it drives; needed here because every window that
	// opens has to be watched, and the first one opens during bring-up.
	static void watchForModalLoop(Window* window);

	// Engine-owned resource pools. Everything created through the factories below
	// lives here so teardown can release every GPU resource while the device is
	// still alive, regardless of what handles the application still holds.
	obj_list<Mesh>    gMeshPool(64);
	obj_list<Texture> gTexturePool(64);

	Scene& getScene() { return gScene; }

	obj_ref<Mesh> createMesh(MeshKind kind) {
		obj_ref<Mesh> ref = gMeshPool.addObject();
		if (ref.IsValid()) {
			::new (ref.GetRawPointer()) Mesh();
			ref->setKind(kind);
		}
		return ref;
	}

	obj_ref<Texture> createTexture() {
		obj_ref<Texture> ref = gTexturePool.addObject();
		if (ref.IsValid()) {
			::new (ref.GetRawPointer()) Texture();
		}
		return ref;
	}

	obj_ref<Texture> loadTexture(const std::string& path, bool srgb) {
		obj_ref<Texture> ref = createTexture();
		if (ref.IsValid()) {
			*ref = Texture::loadFromFile(path, srgb);
		}
		return ref;
	}

	obj_ref<Texture> createTextureFromPixels(const void* rgba, uint32_t width, uint32_t height, bool srgb) {
		obj_ref<Texture> ref = createTexture();
		if (!ref.IsValid() || !rgba || width == 0 || height == 0) return ref;

		TextureDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		// A 1x1 default gets a single level anyway; anything larger is real content.
		desc.mipmapped = (width > 1 || height > 1);
		if (!ref->create(desc)) {
			RDA_LOG_ERROR("Failed to create a texture from pixels");
			return ref;
		}
		ref->uploadPixels(rgba, static_cast<VkDeviceSize>(width) * height * 4);
		return ref;
	}

	Material createForwardMaterial() { return gRenderer.forwardMaterial(); }
	Material createForwardMaterial(const MaterialTextures& textures) {
		return gRenderer.forwardMaterial(textures);
	}

	void releaseForwardMaterial(Material& material) { gRenderer.releaseMaterial(material); }
}

RDA_DEBUG_FUNC(VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger));
RDA_DEBUG_FUNC(void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator));
static bool vulkan_Instance_Init(RDA::AppInfo& pInfo);

namespace RDA {

	// Pumps OS events and mirrors each window's state into its cache. Runs on the
	// loop thread (GLFW is not thread-safe). Returns false once no window is open.
	// `waitSeconds` > 0 blocks until an event arrives or that long passes, instead of
	// returning immediately — how an OnDemand loop idles rather than spinning a core.
	static bool pumpEvents(double waitSeconds = 0.0) {
		// Roll each window's input forward before the poll, so this frame's events form
		// clean rising/falling edges against last frame's state.
		for (auto& node : windowList) {
			Window::getRefFromNode(node)->input().newFrame();
		}

		// Fires the input trampolines, which fill each window's Input.
		if (waitSeconds > 0.0) {
			// Idle: wait out the whole period, but return the moment real input arrives.
			// The OS wakes glfwWaitEventsTimeout for plenty of messages that change
			// nothing on screen, and returning on those would have the loop rebuild the
			// GUI at their rate instead of idling. newFrame() ran before the first wait,
			// so events accumulate across these waits rather than being rolled away.
			const double deadline = glfwGetTime() + waitSeconds;
			for (;;) {
				const double remaining = deadline - glfwGetTime();
				if (remaining <= 0.0) break;
				glfwWaitEventsTimeout(remaining);
				// gRedrawRequested is read, not consumed: the render gate below owns it.
				// gWakeRequested is consumed here, because breaking out is all it means.
				if (!gRunning.load() || gRedrawRequested.load() ||
				    gWakeRequested.exchange(false)) break;

				bool sawInput = false;
				for (auto& node : windowList) {
					Window* window = Window::getRefFromNode(node);
					if (!window->input().events().empty() ||
					    window->input().mouseDelta() != glm::vec2(0.0f) ||
					    glfwWindowShouldClose(window->getGLFW())) {
						sawInput = true;
						break;
					}
				}
				if (sawInput) break;
			}
		} else {
			glfwPollEvents();
		}

		bool anyAlive = false;
		for (auto& node : windowList) {
			Window* window = Window::getRefFromNode(node);
			window->syncOSState();
			if (window->windowIsUp()) anyAlive = true;
		}
		// A windowless application keeps running with nothing open: that is its normal
		// resting state, not the end of its work. It stops when something calls
		// rendeerStop(), not when the last window it was lent goes away.
		return anyAlive || !gWindowDependent;
	}

	// Full engine bring-up on whichever thread owns the loop. GLFW must be initialized
	// here (not by the caller) so it lives on the same thread that will pump its events.
	static void engineBringUp(const AppConfig& config) {
		applicationInfo = config.app;
		gWindowDependent = config.app.windowDependent;
		InitGlfw();
		gGlfwReady.store(true); // from here on, another thread may post a wake-up event
		if (!vulkan_Instance_Init(applicationInfo)) {
			RDA_RUNTIME_ERROR("Failed to create instance!");
		}
		if (!initDeviceHandler()) {
			RDA_RUNTIME_ERROR("Failed to init the device handler");
		}
		gRendererSetup.fontPath = config.gui.fontPath;
		gRendererSetup.fontHeight = config.gui.fontHeight;
		gRendererSetup.viewportMode = config.viewportMode;
		gRendererSetup.guiEnabled = config.gui.enabled;

		if (config.app.windowDependent) {
			WindowInfo createInfo;
			// Sized for an editor rather than a demo: a docked left panel, a viewport and
			// a notebook along the bottom do not all fit in 640x480.
			createInfo.Width = 1280;
			createInfo.Height = 800;
			createInfo.name = config.app.name;
			createInfo.vsync = config.vsync;
			mainWindow = new Window(createInfo);
			mainWindow->input().setCallbacks(config.input);
			watchForModalLoop(mainWindow);
			// Present only as a device anchor: hidden here rather than never created,
			// because the device and the pipelines are chosen against its surface.
			if (config.hiddenMainWindow && mainWindow->getGLFW()) {
				glfwHideWindow(mainWindow->getGLFW());
			}

			gRendererReady = true;
			if (!gRenderer.init(*mainWindow, config.gui.fontPath, config.gui.fontHeight,
			                    config.viewportMode, config.gui.enabled)) {
				RDA_RUNTIME_ERROR("Failed to init the renderer");
			}
			gRenderer.setGuiLayerCaching(config.cacheGuiLayer);

			// Everything below is GUI-only. With config.gui.enabled false none of it
			// exists: no font metrics, no theme, no languages, no clipboard hooks.
			if (config.gui.enabled) {
				// Share the baked font's CPU metrics with the window's GUI frontend.
				mainWindow->gui().init(&gRenderer.fontAtlas());

				// Load the optional XML widget theme + syntax languages. Languages first,
				// so a theme variant can reference a language this file defines.
				if (!config.gui.languagesPath.empty()) {
					mainWindow->gui().syntax().loadFromFile(config.gui.languagesPath);
				}
				if (!config.gui.themePath.empty()) {
					mainWindow->gui().theme().loadFromFile(config.gui.themePath);
				}

				// Wire the OS clipboard for text widgets (Ctrl+C/X/V).
				GLFWwindow* clipWindow = mainWindow->getGLFW();
				mainWindow->gui().setClipboardHandlers(
					[clipWindow]() {
						const char* s = glfwGetClipboardString(clipWindow);
						return s ? std::string(s) : std::string();
					},
					[clipWindow](const char* s) { glfwSetClipboardString(clipWindow, s); });
			}
		}
	}

	// Reverse of engineBringUp, on the same thread. Everything GPU-side is released
	// while the device and allocator are still alive.
	static void engineTearDown() {
		if (mainWindow) {
			gRenderer.waitIdle();
		}

		gScene.clear();

		// Latch the pools closed and destroy every pooled resource now: any obj_ref the
		// application still holds becomes an inert handle instead of a dangling one.
		gMeshPool.erase_all();
		gTexturePool.erase_all();

		if (mainWindow) {
			gRenderer.destroy();
		}

		for (auto& node : windowList) {
			Window::getRefFromNode(node)->closeWindow();
		}
		if (mainWindow) {
			delete mainWindow;
			mainWindow = nullptr;
		}

		destroyDeviceHandler();

		RDA_DEBUG_FUNC(DestroyDebugUtilsMessengerEXT(appInstance, debugMessenger, nullptr));
		vkDestroyInstance(appInstance, nullptr);
		gGlfwReady.store(false); // no more wake-ups past this point
		endGlfw();
	}

	// The loop itself: bring up, run the user's callbacks + render each frame, tear
	// down. Runs on the caller's thread (Caller) or the spawned thread (Owned).
	// The loop's own clock, at file scope because the frame can also be driven from a GLFW
	// callback (see serviceWhileModal) and the two must not each keep their own idea of
	// when the last frame was.
	std::chrono::steady_clock::time_point gLastFrame = std::chrono::steady_clock::now();

	static float advanceClock() {
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - gLastFrame).count();
		gLastFrame = now;
		return dt;
	}

	// One iteration of the engine's work, with the event queue left alone.
	//
	// Split out of the loop because it has to run from two places: the loop calls it after
	// pumping events, and serviceWhileModal() calls it from inside an OS modal loop where
	// the pump cannot return. Returns whether anything was drawn.
	static bool runFrame(const AppConfig& config, float dtSeconds) {
		// Reused across frames: its `typed` string and `editKeys` vector keep their
		// capacity, so a frame with keyboard activity does not allocate. Function-local
		// rather than a loop local because the frame is now called from two places.
		static GuiInput gi;

		// Every window feeds its own input into its own GUI.
		if (config.gui.enabled) {
			// Widget viewport: size the offscreen scene target to the Viewport widget's
			// rect from last frame. Only the main window shows the scene, so only it asks.
			if (mainWindow && config.viewportMode == ViewportMode::Widget) {
				Rect vr = mainWindow->gui().viewportRect();
				if (vr.w >= 1.0f && vr.h >= 1.0f) {
					gRenderer.ensureSceneTarget({ static_cast<uint32_t>(vr.w), static_cast<uint32_t>(vr.h) });
				}
			}
			if (mainWindow) mainWindow->gui().setSceneTexture(gRenderer.sceneTexture());

			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (!window || !window->windowIsUp()) continue;
				if (config.hiddenMainWindow && window == mainWindow) continue;
				beginWindowGui(*window, gi, dtSeconds);
			}
		}

		if (config.onUpdate) config.onUpdate(dtSeconds);

		if (config.gui.enabled) {
			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (!window || !window->windowIsUp()) continue;
				if (config.hiddenMainWindow && window == mainWindow) continue;
				window->gui().end();
			}
		}

		// Whether this frame is worth rendering is decided per window, not once for
		// all of them.
		//
		// In Continuous mode every window draws. In OnDemand a window earns a frame
		// only when its own interface changed, it needs a new swapchain, or the
		// application asked for one. Skipping means no submit and no present, so the
		// GPU does nothing and that window keeps showing what was presented last.
		//
		// Deciding once for the whole loop is what the runtime could least afford: its
		// windows belong to different processes, so ten connected applications meant
		// one of them animating dragged the other nine through a redraw they had no
		// reason to want. Now an idle application costs an idle window.
		//
		// A request through rendeerRequestRedraw() still applies to every window. It
		// means "something changed that the engine cannot see", and the engine has no
		// way to know which window the caller meant.
		const bool requested = (config.redrawMode == RedrawMode::OnDemand)
			? gRedrawRequested.exchange(false) // consumed either way, never carried over
			: false;

		bool renderedAny = false;
		for (auto& node : windowList) {
			Window* window = Window::getRefFromNode(node);
			if (!window || !window->windowIsUp()) continue;
			if (config.hiddenMainWindow && window == mainWindow) continue; // anchor only

			// Consumed every iteration whatever else is true, so a request for this
			// window is honoured exactly once and never lingers into a later frame.
			const bool windowRequested = window->takeRedrawRequest();

			bool draw = true;
			if (config.redrawMode == RedrawMode::OnDemand) {
				draw = requested || windowRequested || window->wasResized();
				// With no GUI there is nothing for the engine to detect a change in, so
				// an on-demand app drives its own frames with rendeerRequestRedraw().
				if (!draw && config.gui.enabled) draw = window->gui().drawChanged();
			}
			if (!draw) { ++gWindowFramesSkipped; continue; }

			// One renderer serves them all: they share the device, the pipelines and
			// the atlas, and differ only in the per-window resources it keeps for each.
			gRenderer.drawWindow(*window);
			++gWindowFramesDrawn;
			renderedAny = true;
		}

		// The event pump idles only when nothing at all was drawn.
		if (renderedAny) ++gFramesRendered; else ++gFramesSkipped;
		return renderedAny;
	}

	// The configuration the running loop was started with, so a GLFW callback can reach it.
	// Null whenever no loop is running.
	const AppConfig* gLoopConfig = nullptr;

	// Runs a frame from inside the message loop Windows enters while a window is being
	// dragged or resized.
	//
	// glfwPollEvents does not return until the user lets go, and everything the engine does
	// lives after that call — the host poll that reads client messages, the GUI, every
	// window's draw. So dragging one window stops all of them. On an ordinary application
	// that is a stutter; on the runtime it is every connected application freezing because
	// somebody moved a neighbour's window.
	//
	// GLFW still delivers position and refresh events from inside that loop, so the frame
	// is driven off them. Two things matter here: this runs *inside* glfwPollEvents, so it
	// must never pump events again, and it must roll each window's input forward afterwards
	// or the same press would be delivered on every callback for as long as the drag lasts.
	static void serviceWhileModal() {
		static bool inside = false;
		if (inside || !gLoopConfig || !gRunning.load()) return;
		inside = true;

		runFrame(*gLoopConfig, advanceClock());
		for (auto& node : windowList) {
			Window::getRefFromNode(node)->input().newFrame();
		}

		inside = false;
	}

	// Attaches the callbacks that keep the engine running while `window` is dragged or
	// resized. Every window needs them, including the ones the runtime opens for clients.
	static void watchForModalLoop(Window* window) {
		GLFWwindow* handle = window ? window->getGLFW() : nullptr;
		if (!handle) return;
		// Moving reports position, resizing reports size, and both report paint. All three
		// are listened for rather than the minimum that happens to work: the refresh alone
		// does carry a resize today, but only because the OS invalidates the window while
		// it grows, which is a detail of the platform rather than a promise it makes.
		glfwSetWindowPosCallback(handle, [](GLFWwindow*, int, int) { serviceWhileModal(); });
		glfwSetWindowSizeCallback(handle, [](GLFWwindow*, int, int) { serviceWhileModal(); });
		glfwSetWindowRefreshCallback(handle, [](GLFWwindow*) { serviceWhileModal(); });
	}

	static void engineMain(AppConfig config) {
		engineBringUp(config);

		// Before onStart, not after: an application that spawns a thread there can ask for
		// a resource before the loop has run a single frame, and until this is set such a
		// request runs wherever it was made — which for anything touching the device is
		// exactly the thread it must not be on.
		loopWork().setServiceThread();

		// Set before onStart, because onStart is allowed to change its mind. A headless
		// check that finishes its work there, or a startup that finds a bad argument,
		// calls rendeerStop() and must actually stop -- raising the flag afterwards would
		// overwrite that decision and run the loop forever with nothing left to do.
		gRunning.store(true);

		if (config.onStart) config.onStart();


		// In OnDemand, an idle iteration blocks in the event pump for at most this long
		// rather than spinning. Short enough that time-based UI (the caret blink) still
		// ticks, long enough that an idle app costs almost no CPU.
		constexpr double kIdleWaitSeconds = 0.05;
		bool renderedLastFrame = true;

		// Reachable from the GLFW callbacks that drive a frame during a drag.
		gLoopConfig = &config;

		while (gRunning.load()) {
			// Only idle the pump once a frame has been skipped: while frames are being
			// produced the loop stays uncapped here and is paced by vsync at present.
			const double wait = (config.redrawMode == RedrawMode::OnDemand && !renderedLastFrame)
				? kIdleWaitSeconds : 0.0;
			if (!pumpEvents(wait)) break; // every window closed

			// Anything another thread has asked the loop to make, before the frame that
			// might use it. Costs one empty check on the overwhelming majority of frames.
			loopWork().service();

			renderedLastFrame = runFrame(config, advanceClock());
		}
		gRunning.store(false);
		gLoopConfig = nullptr; // no callback may run a frame after this
		// Releases anything still blocked on a resource rather than leaving it to time
		// out, so shutting down does not take five seconds per waiting caller.
		loopWork().stop();
		// One line at shutdown each, so both savings are observable without a profiler.
		if (config.redrawMode == RedrawMode::OnDemand) {
			RDA_LOG_INFO("OnDemand redraw - frames rendered: " << gFramesRendered
			             << ", skipped: " << gFramesSkipped
			             << " | window draws: " << gWindowFramesDrawn
			             << ", skipped: " << gWindowFramesSkipped);
		}
		if (mainWindow && config.gui.enabled) {
			const Gui::CacheStats& cache = mainWindow->gui().cacheStats();
			RDA_LOG_INFO("GUI retained cache - tree walked: " << cache.walked
			             << ", reused: " << cache.reused);
		}

		if (config.onShutdown) config.onShutdown();

		engineTearDown();
	}
}

void rendeerRun(const RDA::AppConfig& config) {
	if (config.threadMode == RDA::ThreadMode::Caller) {
		RDA::engineMain(config);
	} else {
		RDA::gLoopThread = std::thread(RDA::engineMain, config);
	}
}

void rendeerStop() {
	RDA::gRunning.store(false);
	// Wake an OnDemand loop that is blocked in the event pump, so it exits now rather
	// than after the idle timeout. glfwPostEmptyEvent is safe from any thread.
	if (RDA::gGlfwReady.load()) glfwPostEmptyEvent();
}

void rendeerRequestRedraw() {
	RDA::gRedrawRequested.store(true);
	RDA::gWakeRequested.store(true);
	if (RDA::gGlfwReady.load()) glfwPostEmptyEvent();
}

void rendeerRequestRedraw(RDA::Window* window) {
	if (!window) { rendeerRequestRedraw(); return; } // null means "all of them"
	window->requestRedraw();
	RDA::gWakeRequested.store(true);
	if (RDA::gGlfwReady.load()) glfwPostEmptyEvent();
}

void rendeerWait() {
	if (RDA::gLoopThread.joinable()) {
		RDA::gLoopThread.join();
	}
}

RDA::Window* rendeerCreateWindow(uint32_t width, uint32_t height, const char* title,
                                 bool vsync) {
	using namespace RDA;
	WindowInfo info;
	info.Width = width;
	info.Height = height;
	info.name = title ? title : "";
	info.vsync = vsync;

	// Registers itself in windowList, so the event pump and the draw loop pick it up
	// without being told about it.
	Window* window = new Window(info);
	if (!window->windowIsUp()) {
		delete window;
		RDA_LOG_ERROR("Failed to open a window");
		return nullptr;
	}
	// So dragging or resizing this one does not stop every other window with it.
	watchForModalLoop(window);
	// An application that opened no window at startup has no renderer yet: the pipelines
	// and the surface format are chosen against a window, so the first one to exist is
	// what brings the renderer up. Every window after this shares it.
	if (!gRendererReady) {
		if (!gRenderer.init(*window, gRendererSetup.fontPath, gRendererSetup.fontHeight,
		                    gRendererSetup.viewportMode, gRendererSetup.guiEnabled)) {
			RDA_LOG_ERROR("Failed to init the renderer against the first window");
			window->closeWindow();
			delete window;
			return nullptr;
		}
		gRendererReady = true;
		RDA_LOG_INFO("Renderer brought up by the first window");
	}

	// Same footing as the main window: without the font metrics its GUI cannot measure
	// anything, so nothing it is asked to draw would come out right.
	if (gRenderer.guiEnabled()) window->gui().init(&gRenderer.fontAtlas());
	return window;
}

void rendeerDestroyWindow(RDA::Window* window) {
	using namespace RDA;
	if (!window || window == mainWindow) return; // the main window is the engine's

	// Its fences and semaphores may still be in use by a frame that has not finished.
	gRenderer.waitIdle();
	gRenderer.forgetWindow(window);
	gClickHistory.erase(window);
	window->closeWindow();
	delete window;
}

void rendeerWaitIdle() {
	RDA::gRenderer.waitIdle();
}

void rendeerForgetTexture(const RDA::Texture* texture) {
	if (texture) RDA::gRenderer.forgetTexture(texture);
}

RDA::Window* getMainWindow() {
	return RDA::mainWindow;
}

static bool vulkan_Instance_Init(RDA::AppInfo& pInfo) {
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = pInfo.name.c_str();
	appInfo.applicationVersion = pInfo.appVersion;
	appInfo.pEngineName = "Rendeer Alpha";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledLayerCount = 0;
	createInfo.pNext = nullptr;

	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
	RDA_DEBUG_FUNC(extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	RDA_DEBUG_FUNC(
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
		populateDebugMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
	);
	if (vkCreateInstance(&createInfo, nullptr, &RDA::appInstance) != VK_SUCCESS) {
		RDA_LOG_ERROR("Failed to create vulkan instance!");
		return false;
	}
	RDA_DEBUG_FUNC(
		if (CreateDebugUtilsMessengerEXT(RDA::appInstance, &debugCreateInfo, nullptr, &RDA::debugMessenger) != VK_SUCCESS) {
			RDA_RUNTIME_ERROR("Failed to set up debug messenger!");
		}
	)
	return true;
}

RDA_DEBUG_FUNC(VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	}
	else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
})

RDA_DEBUG_FUNC(void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) {
		func(instance, debugMessenger, pAllocator);
	}
})
