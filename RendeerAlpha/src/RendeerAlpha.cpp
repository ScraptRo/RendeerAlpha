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
	static bool pumpEvents() {
		// Roll each window's input forward before the poll, so this frame's events form
		// clean rising/falling edges against last frame's state.
		for (auto& node : windowList) {
			Window::getRefFromNode(node)->input().newFrame();
		}

		glfwPollEvents(); // fires the input trampolines, which fill each window's Input

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
			mainWindow = new Window(createInfo);
			mainWindow->input().setCallbacks(config.input);

			if (!gRenderer.init(*mainWindow, config.gui.fontPath, config.gui.fontHeight)) {
				RDA_RUNTIME_ERROR("Failed to init the renderer");
			}
			// Share the baked font's CPU metrics with the window's GUI frontend.
			mainWindow->gui().init(&gRenderer.fontAtlas());
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
		endGlfw();
	}

	// The loop itself: bring up, run the user's callbacks + render each frame, tear
	// down. Runs on the caller's thread (Caller) or the spawned thread (Owned).
	static void engineMain(AppConfig config) {
		engineBringUp(config);

		if (config.onStart) config.onStart();

		using Clock = std::chrono::steady_clock;
		Clock::time_point last = Clock::now();

		gRunning.store(true);
		while (gRunning.load()) {
			if (!pumpEvents()) break; // every window closed

			Clock::time_point now = Clock::now();
			float dtSeconds = std::chrono::duration<float>(now - last).count();
			last = now;

			// Feed the main window's input into its GUI. On-screen, GUI-space is just
			// window pixels; the in-world plane path would substitute a raycast here.
			if (mainWindow) {
				Input& in = mainWindow->input();
				GuiInput gi;
				gi.pointer = mainWindow->cursorToFramebuffer(in.mousePosition());
				gi.down = in.isMouseButtonDown(0);      // GLFW_MOUSE_BUTTON_LEFT
				gi.pressed = in.mouseButtonPressed(0);
				gi.released = in.mouseButtonReleased(0);
				gi.scroll = in.scroll().y;
				mainWindow->gui().begin(gi);
			}

			if (config.onUpdate) config.onUpdate(dtSeconds);

			if (mainWindow) mainWindow->gui().end();

			if (mainWindow && mainWindow->windowIsUp()) {
				gRenderer.drawWindow(*mainWindow);
			}
		}
		gRunning.store(false);

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
