#include <RendeerAlpha.h>
#include <Core/Core.h>
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
	std::atomic<bool> gGlfwReady{ false }; // guards glfwPostEmptyEvent() before/after init
	uint64_t gFramesRendered = 0, gFramesSkipped = 0; // loop-thread only

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

	Material createForwardMaterial() { return gRenderer.forwardMaterial(); }
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
				if (!gRunning.load() || gRedrawRequested.load()) break;

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
		return anyAlive;
	}

	// Full engine bring-up on whichever thread owns the loop. GLFW must be initialized
	// here (not by the caller) so it lives on the same thread that will pump its events.
	static void engineBringUp(const AppConfig& config) {
		applicationInfo = config.app;
		InitGlfw();
		gGlfwReady.store(true); // from here on, another thread may post a wake-up event
		if (!vulkan_Instance_Init(applicationInfo)) {
			RDA_RUNTIME_ERROR("Failed to create instance!");
		}
		if (!initDeviceHandler()) {
			RDA_RUNTIME_ERROR("Failed to init the device handler");
		}
		if (config.app.windowDependent) {
			WindowInfo createInfo;
			createInfo.Height = 480;
			createInfo.Width = 640;
			createInfo.name = config.app.name;
			createInfo.vsync = config.vsync;
			mainWindow = new Window(createInfo);
			mainWindow->input().setCallbacks(config.input);

			if (!gRenderer.init(*mainWindow, config.gui.fontPath, config.gui.fontHeight, config.viewportMode)) {
				RDA_RUNTIME_ERROR("Failed to init the renderer");
			}
			gRenderer.setGuiLayerCaching(config.cacheGuiLayer);
			// Share the baked font's CPU metrics with the window's GUI frontend.
			mainWindow->gui().init(&gRenderer.fontAtlas());

			// Load the optional XML widget theme + syntax languages. Languages first, so
			// a theme variant can reference a language this file defines.
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
	static void engineMain(AppConfig config) {
		engineBringUp(config);

		if (config.onStart) config.onStart();

		using Clock = std::chrono::steady_clock;
		Clock::time_point last = Clock::now();

		// In OnDemand, an idle iteration blocks in the event pump for at most this long
		// rather than spinning. Short enough that time-based UI (the caret blink) still
		// ticks, long enough that an idle app costs almost no CPU.
		constexpr double kIdleWaitSeconds = 0.05;
		bool renderedLastFrame = true;

		// Reused across frames: its `typed` string and `editKeys` vector keep their
		// capacity, so a frame with keyboard activity does not allocate.
		GuiInput gi;

		gRunning.store(true);
		while (gRunning.load()) {
			// Only idle the pump once a frame has been skipped: while frames are being
			// produced the loop stays uncapped here and is paced by vsync at present.
			const double wait = (config.redrawMode == RedrawMode::OnDemand && !renderedLastFrame)
				? kIdleWaitSeconds : 0.0;
			if (!pumpEvents(wait)) break; // every window closed

			Clock::time_point now = Clock::now();
			float dtSeconds = std::chrono::duration<float>(now - last).count();
			last = now;

			// Feed the main window's input into its GUI. On-screen, GUI-space is just
			// window pixels; the in-world plane path would substitute a raycast here.
			if (mainWindow) {
				// Widget viewport: size the offscreen scene target to the Viewport widget's
				// rect from last frame, so the scene renders at its exact resolution and
				// aspect. Then hand the GUI this frame's scene texture (null in Fullscreen).
				if (config.viewportMode == ViewportMode::Widget) {
					Rect vr = mainWindow->gui().viewportRect();
					if (vr.w >= 1.0f && vr.h >= 1.0f) {
						gRenderer.ensureSceneTarget({ static_cast<uint32_t>(vr.w), static_cast<uint32_t>(vr.h) });
					}
				}
				mainWindow->gui().setSceneTexture(gRenderer.sceneTexture());

				Input& in = mainWindow->input();
				// Reset only what is accumulated below; every other field is assigned.
				gi.typed.clear();
				gi.editKeys.clear();
				gi.copy = gi.cut = gi.paste = gi.selectAll = false;
				gi.pointer = mainWindow->cursorToFramebuffer(in.mousePosition());
				VkExtent2D ext = mainWindow->cachedExtent();
				gi.viewport = { static_cast<float>(ext.width), static_cast<float>(ext.height) };
				gi.down = in.isMouseButtonDown(0);      // GLFW_MOUSE_BUTTON_LEFT
				gi.pressed = in.mouseButtonPressed(0);
				gi.released = in.mouseButtonReleased(0);
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
						case GLFW_KEY_KP_ENTER:  gi.editKeys.insert(GuiEditKey::Enter);      break;
						case GLFW_KEY_TAB:       gi.editKeys.insert(GuiEditKey::Tab);        break;
						case GLFW_KEY_C: if (gi.ctrl) gi.copy = true;      break;
						case GLFW_KEY_X: if (gi.ctrl) gi.cut = true;       break;
						case GLFW_KEY_V: if (gi.ctrl) gi.paste = true;     break;
						case GLFW_KEY_A: if (gi.ctrl) gi.selectAll = true; break;
						default: break;
						}
					}
				}
				mainWindow->gui().begin(gi);
			}

			if (config.onUpdate) config.onUpdate(dtSeconds);

			if (mainWindow) mainWindow->gui().end();

			// Decide whether this frame is worth rendering. In Continuous mode it always
			// is; in OnDemand the frame is produced only when the GUI's geometry actually
			// differs from the last one, the window needs a new swapchain, or the app
			// asked for a frame. Skipping means no submit and no present, so the GPU does
			// nothing and the window keeps showing what was presented last.
			bool render = true;
			if (config.redrawMode == RedrawMode::OnDemand && mainWindow) {
				const bool requested = gRedrawRequested.exchange(false);
				render = requested || mainWindow->gui().drawChanged() || mainWindow->wasResized();
			}
			renderedLastFrame = render;
			if (render) ++gFramesRendered; else ++gFramesSkipped;

			if (render && mainWindow && mainWindow->windowIsUp()) {
				gRenderer.drawWindow(*mainWindow);
			}
		}
		gRunning.store(false);
		// One line at shutdown, so the effect of OnDemand is observable without a profiler.
		if (config.redrawMode == RedrawMode::OnDemand) {
			RDA_LOG_INFO("OnDemand redraw - frames rendered: " << gFramesRendered
			             << ", skipped: " << gFramesSkipped);
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
	if (RDA::gGlfwReady.load()) glfwPostEmptyEvent();
}

void rendeerWait() {
	if (RDA::gLoopThread.joinable()) {
		RDA::gLoopThread.join();
	}
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
