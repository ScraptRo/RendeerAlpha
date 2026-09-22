#include <RendeerAlpha.h>
#include <Layout/Bindings.h>
#include <Core/Signals.h>
#include <unordered_map>
#include <Core/Core.h>
#include <Core/LoopWork.h>
#include <Core/Utf8.h>
#include <Logger/Logger.h>
#include <Core/Framework.h>
#include <Core/BackendConnector.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Renderer.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Viewports.h>
#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Effects.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Texture.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <Core/Location.h>
#if !defined(_WIN32)
	#include <unistd.h>
#endif
#include <new>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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
	// A pointer that is never deleted rather than a std::thread with static storage.
	// If something calls exit() from the loop thread itself -- Xlib's default error
	// handler does exactly that, after printing one line -- static destruction runs on
	// that thread, and destroying a joinable std::thread is std::terminate: the one
	// readable line is followed by "terminate called without an active exception" and
	// a core dump. Leaked, the process ends the way exit() meant it to.
	std::thread*      gLoopThread = nullptr;

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
		std::vector<float> fontSizes;
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
		gi.undo = gi.redo = false;
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
				// GLFW reports a codepoint; GuiInput::typed is UTF-8, which is what a
				// text field stores and what the atlas looks glyphs up by. This used to
				// keep only 32..126, so a keyboard laid out for any language but English
				// dropped half of what it typed before anything could draw it.
				//
				// Control characters are still dropped -- 0x7F included, which is Delete
				// arriving as a character on some layouts, and would otherwise be typed
				// into the field as text.
				if (e.codepoint >= 32 && e.codepoint != 127) {
					Utf8::encode(e.codepoint, gi.typed);
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
				case GLFW_KEY_ESCAPE:    gi.editKeys.insert(GuiEditKey::Escape);     break;
				// Space reaches a text field as typed text and must go on doing so; it is
				// also how a keyboard presses a button, so it arrives as both and the
				// widget that has the keyboard decides which one it was.
				case GLFW_KEY_SPACE:     gi.editKeys.insert(GuiEditKey::Space);      break;
				case GLFW_KEY_C: if (gi.ctrl) gi.copy = true;      break;
				case GLFW_KEY_X: if (gi.ctrl) gi.cut = true;       break;
				case GLFW_KEY_V: if (gi.ctrl) gi.paste = true;     break;
				case GLFW_KEY_A: if (gi.ctrl) gi.selectAll = true; break;
				// Both spellings of redo, because both are muscle memory somewhere:
				// Ctrl+Y on Windows, Ctrl+Shift+Z everywhere a Mac keyboard taught it.
				case GLFW_KEY_Z: if (gi.ctrl) { if (gi.shift) gi.redo = true; else gi.undo = true; } break;
				case GLFW_KEY_Y: if (gi.ctrl) gi.redo = true;      break;
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
#if !defined(_WIN32)
		// Not a refusal, because a root-owned display exists; a warning, because the
		// common case is `sudo` under an ordinary desktop session. The X server then
		// runs as the user and cannot attach shared memory a root client created, and
		// Mesa's software presentation is shared memory -- the first frame ends with
		// "BadAccess (attempt to access private resource denied)" and exit(1). Nothing
		// this engine does needs root, so the fix is to run it without.
		if (geteuid() == 0) {
			RDA_LOG_WARNING("running as root. If this is `sudo` under a desktop session, "
			                "the X server will refuse the first frame (BadAccess); run "
			                "it as the user who owns the session");
		}
#endif
		InitGlfw();
		gGlfwReady.store(true); // from here on, another thread may post a wake-up event
		if (!vulkan_Instance_Init(applicationInfo)) {
			RDA_RUNTIME_ERROR("Failed to create instance!");
		}
		if (!initDeviceHandler()) {
			RDA_RUNTIME_ERROR("Failed to init the device handler");
		}
		// The engine's own files: as given if they exist beside the application, and
		// otherwise beside the engine itself, which is where a staged bin/ keeps them.
		// Resolved once here so every later use -- a second window's renderer included
		// -- sees the answer rather than the question.
		const std::string fontPath = resolveEngineAsset(config.gui.fontPath);
		const std::string languagesPath = resolveEngineAsset(config.gui.languagesPath);
		gRendererSetup.fontPath = fontPath;
		gRendererSetup.fontHeight = config.gui.fontHeight;
		gRendererSetup.fontSizes = config.gui.fontSizes;
		gRendererSetup.viewportMode = config.viewportMode;
		gRendererSetup.guiEnabled = config.gui.enabled;

		if (config.app.windowDependent) {
			WindowInfo createInfo;
			// Sized for an editor rather than a demo: a docked left panel, a viewport and
			// a notebook along the bottom do not all fit in 640x480.
			createInfo.Width = config.windowWidth > 0 ? config.windowWidth : 1280;
			createInfo.Height = config.windowHeight > 0 ? config.windowHeight : 800;
			createInfo.name = config.app.name;
			createInfo.vsync = config.vsync;
			createInfo.style = config.window;
			mainWindow = new Window(createInfo);
			mainWindow->input().setCallbacks(config.input);
			watchForModalLoop(mainWindow);
			// Present only as a device anchor: hidden here rather than never created,
			// because the device and the pipelines are chosen against its surface.
			if (config.hiddenMainWindow && mainWindow->getGLFW()) {
				glfwHideWindow(mainWindow->getGLFW());
			}

			gRendererReady = true;
			if (!gRenderer.init(*mainWindow, fontPath, config.gui.fontHeight,
			                   config.gui.fontSizes,
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
				if (!languagesPath.empty()) {
					mainWindow->gui().syntax().loadFromFile(languagesPath);
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

	// What the engine knows about itself, as signals a layout can read: state.rda.width
	// and state.rda.height, in the same pixels every widget is laid out in.
	//
	// Defined here rather than by an application, because they are the engine's answer
	// and not the application's -- and defined during bring-up, before onStart, so the
	// first layout to read one finds it instead of inventing it as an undeclared number.
	//
	// Updated every frame and not on a resize callback: a callback is one more thing to
	// keep in step with minimise, maximise, a monitor change and a compositor that
	// resizes without telling anyone. Signals::set already refuses a write that changes
	// nothing, so a still window costs two comparisons a frame and wakes no bindings.
	static uint32_t gWidthSignal = kNoSignal, gHeightSignal = kNoSignal;
	static uint32_t gWindowSignalWidth = 0, gWindowSignalHeight = 0;

	// The window's own state, as signals that read *and* write.
	//
	// Width and height are the engine reporting; these four are a conversation. The OS
	// changes them -- somebody pressed the maximise button, or alt-tabbed away -- and so
	// does the interface, because a window with no frame of its own has to draw those
	// buttons and they have to do something. Making them state rather than calls is the
	// same answer this engine gives everywhere else: `state.rda.maximized = true` is a
	// window maximising, in a handler that is otherwise just an assignment.
	static uint32_t gMaximizedSignal = kNoSignal, gMinimizedSignal = kNoSignal;
	static uint32_t gFullscreenSignal = kNoSignal, gFocusedSignal = kNoSignal;
	static uint32_t gOpenSignal = kNoSignal;
	// What was last agreed. A flag that differs from this on the signal side was written
	// by the interface; one that differs on the OS side was done by the reader.
	static bool gMirrorMaximized = false, gMirrorMinimized = false;
	static bool gMirrorFullscreen = false, gMirrorFocused = false;

	// Whichever side moved is the one that meant it. The signal is checked first, so a
	// write made during this frame's handlers is acted on rather than being overwritten
	// by the state the window has not reached yet.
	template <typename Apply>
	static void syncWindowFlag(uint32_t signal, bool& mirror, bool osValue, Apply apply) {
		if (signal == kNoSignal) return;
		const bool wanted = signals().boolean(signal);
		if (wanted != mirror) {
			apply(wanted);
			mirror = wanted;
			return;
		}
		if (osValue != mirror) {
			signals().set(signal, osValue);
			mirror = osValue;
		}
	}

	static void publishWindowSignals() {
		// Created once, at their opening values, and written only through set()
		// afterwards. define() carries a value and installs it *without* marking
		// observers dirty -- which is right for declaring state and wrong for changing
		// it, so calling it every frame updated the number while no binding ever heard
		// about it. The window resized, the label did not, and nothing anywhere said so.
		if (gWidthSignal == kNoSignal) {
			gWidthSignal = signals().define("rda.width", 0.0);
			gHeightSignal = signals().define("rda.height", 0.0);
			gMirrorMaximized = mainWindow && mainWindow->isMaximized();
			gMirrorMinimized = mainWindow && mainWindow->isMinimized();
			gMirrorFullscreen = mainWindow && mainWindow->isFullscreen();
			gMirrorFocused = mainWindow && mainWindow->isFocused();
			gMaximizedSignal = signals().define("rda.maximized", gMirrorMaximized);
			gMinimizedSignal = signals().define("rda.minimized", gMirrorMinimized);
			gFullscreenSignal = signals().define("rda.fullscreen", gMirrorFullscreen);
			gFocusedSignal = signals().define("rda.focused", gMirrorFocused);
			gOpenSignal = signals().define("rda.open", true);
		}

		if (mainWindow) {
			syncWindowFlag(gMaximizedSignal, gMirrorMaximized, mainWindow->isMaximized(),
			               [](bool on) { mainWindow->setMaximized(on); });
			syncWindowFlag(gMinimizedSignal, gMirrorMinimized, mainWindow->isMinimized(),
			               [](bool on) { mainWindow->setMinimized(on); });
			syncWindowFlag(gFullscreenSignal, gMirrorFullscreen, mainWindow->isFullscreen(),
			               [](bool on) { mainWindow->setFullscreen(on); });
			// Read-only: nothing an application writes can make the reader look at it.
			const bool focused = mainWindow->isFocused();
			if (focused != gMirrorFocused) {
				gMirrorFocused = focused;
				signals().set(gFocusedSignal, focused);
			}
			// The window being open is state too, and writing false is how a title bar
			// this engine drew closes the window it is sitting on.
			if (gOpenSignal != kNoSignal && !signals().boolean(gOpenSignal)) {
				rendeerStop();
			}
		}

		const uint32_t width = mainWindow ? mainWindow->cachedExtent().width : 0;
		const uint32_t height = mainWindow ? mainWindow->cachedExtent().height : 0;
		if (width == gWindowSignalWidth && height == gWindowSignalHeight) return;
		gWindowSignalWidth = width;
		gWindowSignalHeight = height;
		signals().set(gWidthSignal, static_cast<double>(width));
		signals().set(gHeightSignal, static_cast<double>(height));
	}

	// A second run in the same process gets its own signal table, so the ids above do
	// not survive one.
	static void forgetWindowSignals() {
		gWidthSignal = kNoSignal;
		gHeightSignal = kNoSignal;
		gMaximizedSignal = gMinimizedSignal = kNoSignal;
		gFullscreenSignal = gFocusedSignal = gOpenSignal = kNoSignal;
		gMirrorMaximized = gMirrorMinimized = gMirrorFullscreen = gMirrorFocused = false;
		gWindowSignalWidth = 0;
		gWindowSignalHeight = 0;
	}

	// Reverse of engineBringUp, on the same thread. Everything GPU-side is released
	// while the device and allocator are still alive.
	// Textures whose owner is gone, waiting for a safe moment. See rendeerRetireTexture.
	static std::vector<std::unique_ptr<Texture>> gRetiredTextures;

	// Drained at the top of a frame: by now the frame that referenced them has been
	// recorded and submitted, and waiting makes sure it has also finished.
	static void drainRetiredTextures() {
		if (gRetiredTextures.empty()) return;
		gRenderer.waitIdle();
		for (auto& texture : gRetiredTextures) {
			if (!texture) continue;
			gRenderer.forgetTexture(texture.get());
			texture->destroy();
		}
		gRetiredTextures.clear();
	}

	// The same, for teardown. The GUI is already destroyed by the time the windows are, so
	// there are no descriptor sets left to release -- only images to free, and only while
	// there is still an allocator to free them with. A widget tree is destroyed with the
	// window that owns it, which is after the renderer has gone and before the device has.
	static void destroyRetiredTextures() {
		for (auto& texture : gRetiredTextures) {
			if (texture) texture->destroy();
		}
		gRetiredTextures.clear();
	}

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

		// Closing the windows destroyed the widget trees they own, and anything in one of
		// those that held a texture has just put it here. This is the last moment the
		// allocator exists to free it -- past this, VMA rightly asserts that allocations
		// outlived the block they came from.
		destroyRetiredTextures();

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
		// Before anything is walked or drawn, so nothing this frame can still be pointing
		// at what is about to be destroyed.
		drainRetiredTextures();

		// Before the walk, so a binding that reads the window's size is evaluated
		// against this frame's rather than the last one's.
		publishWindowSignals();

		// Reused across frames: its `typed` string and `editKeys` vector keep their
		// capacity, so a frame with keyboard activity does not allocate. Function-local
		// rather than a loop local because the frame is now called from two places.
		static GuiInput gi;

		// Every window feeds its own input into its own GUI.
		if (config.gui.enabled) {
			// Which viewport holds the GPU is settled again every frame, because the
			// interface may have changed between them -- a route that swapped one
			// viewport for another has to be able to hand the target over.
			viewports().beginFrame();

			// A C++ callback needs somewhere to record into, and in Fullscreen mode the
			// scene goes straight to the window instead. Said once: it is a one-line
			// config change, and the symptom otherwise is a viewport that stays empty.
			if (config.viewportMode != ViewportMode::Widget && !viewports().empty()) {
				static bool warnedAboutMode = false;
				if (!warnedAboutMode && viewports().drawnByCode(viewports().gpuViewport())) {
					warnedAboutMode = true;
					RDA_LOG_WARNING("viewport: a draw callback is registered but viewportMode is "
					                "Fullscreen, so there is no target to record into. Set "
					                "config.viewportMode = ViewportMode::Widget.");
				}
			}

			// Widget viewport: size the offscreen scene target to the Viewport widget's
			// rect from last frame. Only the main window shows the scene, so only it asks.
			if (mainWindow && config.viewportMode == ViewportMode::Widget) {
				Rect vr = mainWindow->gui().viewportRect();
				if (vr.w >= 1.0f && vr.h >= 1.0f) {
					gRenderer.ensureSceneTarget({ static_cast<uint32_t>(vr.w), static_cast<uint32_t>(vr.h) });
				}
			}
			if (mainWindow) mainWindow->gui().setSceneTexture(gRenderer.sceneTexture());

			// Bindings whose signals moved since the last frame, applied before the
			// interface is walked so the walk sees the new values rather than last
			// frame's. Nothing is compared and no tree is searched: the signal graph
			// already knows exactly which bindings a change reached, and on almost every
			// frame that list is empty and this costs one check.
			//
			// The redraw request is what makes the change visible in an on-demand window,
			// which would otherwise have no reason to look again.
			if (Layout::bindings().applyDirty() > 0) {
				// The tree has to be walked again, not just presented again.
				//
				// The retained cache decides whether to walk by looking at input -- the
				// pointer moving, a button, a key, a focused caret. A binding writing a
				// widget property is none of those, so without this the frame renders the
				// geometry it already had and the change does not appear until something
				// unrelated happens to move the mouse. That is what "the interface takes
				// a second or two to catch up" was.
				for (auto& node : windowList) {
					Window* window = Window::getRefFromNode(node);
					if (window && window->windowIsUp()) window->gui().markDirty();
				}
				rendeerRequestRedraw();
			}

			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (!window || !window->windowIsUp()) continue;
				if (config.hiddenMainWindow && window == mainWindow) continue;
				beginWindowGui(*window, gi, dtSeconds);
			}
		}

		// What a stream's "is anybody looking" ages against. Counted here rather than in
		// the GUI, because a window with no GUI at all still turns the loop.
		streams().tick();

		if (config.onUpdate) config.onUpdate(dtSeconds);

		if (config.gui.enabled) {
			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (!window || !window->windowIsUp()) continue;
				if (config.hiddenMainWindow && window == mainWindow) continue;
				window->gui().end();
				// After the walk, because whether a title bar is being held is something
				// the walk works out -- and before the frame is drawn, so the window and
				// what is in it move together rather than a frame apart.
				window->followGrip(window->gui().windowGrabbed());
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
				//
				// A window with something still moving earns frames until it has arrived,
				// and then goes quiet again. That is the whole cost of animation in an
				// on-demand loop: frames while something is happening, none while not.
				if (!draw && config.gui.enabled) {
					draw = window->gui().drawChanged() || window->gui().animating();
				}
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
		publishWindowSignals();

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

		// The GPU is finished before the application is asked to clean up.
		//
		// An application that recorded into a <viewport> owns Vulkan objects -- a
		// pipeline, a buffer, a descriptor set -- and onShutdown is where it destroys
		// them. Destroying one a frame in flight is still using is a validation error and
		// a real hazard, and every such application would have to know to wait first. It
		// is one call here and a whole class of mistake nobody has to hear about.
		gRenderer.waitIdle();
		if (config.onShutdown) config.onShutdown();

		// The application just let go of its widget trees, so anything they owned is in
		// the queue with no frame left to drain it. Emptied here, while there is still a
		// device and an allocator to free it with -- left to static teardown, VMA asserts
		// that allocations outlived the block they came from, which is exactly true.
		drainRetiredTextures();

		forgetWindowSignals();
		viewports().forget();
		// Registered pictures, released while there is still a device to release them
		// with. Left to static teardown, VMA asserts -- correctly -- that an allocation
		// outlived the block it came from.
		images().clear();
		streams().clear();
		effects().clear();
		engineTearDown();
	}
}

void rendeerRun(const RDA::AppConfig& config) {
	if (config.threadMode == RDA::ThreadMode::Caller) {
		RDA::engineMain(config);
	} else {
		delete RDA::gLoopThread; // a previous run, already joined by rendeerWait()
		RDA::gLoopThread = new std::thread(RDA::engineMain, config);
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

void rendeerInterfaceChanged() {
	for (auto& node : RDA::windowList) {
		RDA::Window* window = RDA::Window::getRefFromNode(node);
		if (window && window->windowIsUp()) window->gui().markDirty();
	}
	rendeerRequestRedraw();
}

void rendeerRequestRedraw(RDA::Window* window) {
	if (!window) { rendeerRequestRedraw(); return; } // null means "all of them"
	window->requestRedraw();
	RDA::gWakeRequested.store(true);
	if (RDA::gGlfwReady.load()) glfwPostEmptyEvent();
}

void rendeerWait() {
	if (RDA::gLoopThread && RDA::gLoopThread->joinable()) {
		RDA::gLoopThread->join();
	}
}

bool rendeerRunning() { return RDA::gRunning.load(); }

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
			                   gRendererSetup.fontSizes,
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

void rendeerRetireTexture(std::unique_ptr<RDA::Texture> texture) {
	if (texture) RDA::gRetiredTextures.push_back(std::move(texture));
}

namespace RDA {
	// The handles a viewport callback needs to build Vulkan work of its own. Read from
	// the live device rather than cached, so a call before the engine is up returns
	// nulls instead of something stale.
	ViewportGpu viewportGpu() {
		ViewportGpu gpu;
		GPUInfo& info = getGPU();
		gpu.instance = appInstance;
		gpu.physicalDevice = info.PDevice;
		gpu.device = info.LDevice;
		gpu.graphicsQueue = info.graphicsQueue;
		gpu.graphicsFamily = info.graphicsFamily;
		gpu.allocator = getAllocator();
		gpu.framesInFlight = Renderer::MAX_FRAMES_IN_FLIGHT;
		gpu.renderPass = gRenderer.sceneRenderPass();
		gpu.colorFormat = gRenderer.sceneColorFormat();
		if (info.PDevice != VK_NULL_HANDLE) gpu.depthFormat = FrameBuffer::findDepthFormat();
		return gpu;
	}
}

RDA::Window* getMainWindow() {
	return RDA::mainWindow;
}

// Guarded like the list it filters: validationLayers and the logger only exist in a
// Debug build, and nothing calls this from a Release one.
#if _DEBUG
const std::vector<const char*>& enabledValidationLayers() {
	static const std::vector<const char*> enabled = [] {
		uint32_t count = 0;
		vkEnumerateInstanceLayerProperties(&count, nullptr);
		std::vector<VkLayerProperties> present(count);
		if (count) vkEnumerateInstanceLayerProperties(&count, present.data());

		std::vector<const char*> out;
		for (const char* wanted : validationLayers) {
			bool found = false;
			for (const VkLayerProperties& layer : present) {
				if (std::strcmp(layer.layerName, wanted) == 0) { found = true; break; }
			}
			if (found) {
				out.push_back(wanted);
			} else {
				RDA_LOG_WARNING("Validation layer " << wanted << " is not installed; running "
				                "without it. On Debian and Ubuntu it is the package "
				                "vulkan-validationlayers; elsewhere it is part of the Vulkan SDK.");
			}
		}
		return out;
	}();
	return enabled;
}
#endif

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
		const std::vector<const char*>& layers = enabledValidationLayers();
		createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
		createInfo.ppEnabledLayerNames = layers.data();
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
