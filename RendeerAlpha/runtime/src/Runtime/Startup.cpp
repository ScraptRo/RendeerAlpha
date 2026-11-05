#include "Engine.h"
#include <Layout/StyleWatch.h>
#include <Layout/ThemeLoader.h>
#include <Layout/Rebuilder.h>
#include <Core/BuildMode.h>
#include <Core/Wake.h>
#include <GraphicalObjects/Resources.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Texture.h>
#include <GraphicalObjects/Viewports.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/VulkanInstance.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <Core/Location.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <cmath>
#if !defined(_WIN32)
	#include <unistd.h>
#endif

#include <Platform/WindowSystem.h>

// What is settled once: the engine brought up and torn down, a window's interface set up,
// and the windows an application opens and closes.
namespace RDA::Runtime {

	static RDA::AppInfo applicationInfo;
	Window* mainWindow = nullptr;
	Renderer gRenderer;
	// The scene the renderer draws; owned by Scene.cpp, named here for brevity.
	static Scene& gScene = getScene();


	DeferredRendererSetup gRendererSetup;

	// Everything a window's interface needs before it can draw: the font's metrics, the
	// languages and theme, and the OS clipboard. One function for every window -- it was
	// written twice, once for the main window and once for the rest, and the second copy
	// had drifted to font metrics only, so a text field in a secondary window could not
	// copy or paste and wore the built-in look.
	static void setUpWindowGui(Window& window) {
		window.gui().init(&gRenderer.fontAtlas(), gRendererSetup.unitScale);

		// Languages first, so a theme variant can reference a language this file defines.
		if (!gRendererSetup.languagesPath.empty()) {
			window.gui().syntax().loadFromFile(gRendererSetup.languagesPath);
		}
		if (!gRendererSetup.themePath.empty()) {
			window.gui().theme().loadFromFile(gRendererSetup.themePath);
		}

		// Wire the OS clipboard for text widgets (Ctrl+C/X/V).
		Window* clipWindow = &window;
		window.gui().setClipboardHandlers(
			[clipWindow]() { return clipWindow->clipboardText(); },
			[clipWindow](const char* s) { clipWindow->setClipboardText(s ? s : ""); });
	}
	static bool gRendererReady = false;
	bool gWindowDependent = true;

	float unitScale() {
		return gRendererSetup.unitScale > 0.0f ? gRendererSetup.unitScale : 1.0f;
	}

	// Full engine bring-up on whichever thread owns the loop. GLFW must be initialized
	// here (not by the caller) so it lives on the same thread that will pump its events.
	void engineBringUp(const AppConfig& config) {
		// First, so that everything below is already at the volume the application asked
		// for. Refused where RDA_LOG has already said otherwise -- see setLogLevel.
		setLogLevel(config.logLevel);

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
		// A compiled theme is read by the blueprint layer; the style registry reaches it
		// through a slot, which is filled here, before anything loads one.
		Layout::installThemeReader();
#if RDA_ENABLE_HOT_RELOAD_BUILD
		// And what hot reload compiles with, through the same kind of slot: the watchers
		// include no compiler, and only a build with hot reload on links one.
		Layout::installRebuilder();
#endif

		initWindowSystem();
		// From here on, another thread may post a wake-up event; and "the interface
		// changed" means every window's GUI is walked again.
		Wake::setPoster(&postEmptyEvent);
		Wake::setInterfaceChangedHandler([] {
			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (window && window->windowIsUp()) window->gui().markDirty();
			}
		});
		{
			// What the window system needs the instance to support, asked of GLFW here so
			// the Vulkan layer does not have to know there is a window system.
			const std::vector<const char*> extensions = requiredInstanceExtensions();
			if (!createVulkanInstance(applicationInfo.name, applicationInfo.appVersion, extensions)) {
				RDA_RUNTIME_ERROR("Failed to create instance!");
			}
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
		gRendererSetup.languagesPath = languagesPath;
		gRendererSetup.themePath = config.gui.themePath;
		// What the application asked for, if it asked. 0 means it did not, and the display
		// is asked instead once there is a window to ask about.
		gRendererSetup.unitScale = config.gui.uiScale;

		if (config.app.windowDependent) {
			WindowInfo createInfo;
			// Sized for an editor rather than a demo: a docked left panel, a viewport and
			// a notebook along the bottom do not all fit in 640x480.
			// A window's size is in layout units like everything else an application says,
			// so it is multiplied by what a unit is worth here before GLFW sees it. The
			// display is asked directly: this is before there is a window to ask, and the
			// monitor a window opens on is the primary one unless it is placed elsewhere.
			float openingScale = config.gui.uiScale;
			if (openingScale <= 0.0f) {
				openingScale = primaryDisplayScale();
			}
			const auto inPixels = [openingScale](uint32_t units) {
				return static_cast<unsigned>(std::lround(units * openingScale));
			};
			createInfo.Width = inPixels(config.windowWidth > 0 ? config.windowWidth : 1280);
			createInfo.Height = inPixels(config.windowHeight > 0 ? config.windowHeight : 800);
			createInfo.name = config.app.name;
			createInfo.vsync = config.vsync;
			createInfo.style = config.window;
			mainWindow = new Window(createInfo);
			mainWindow->input().setCallbacks(config.input);
			watchForModalLoop(mainWindow);
			// Present only as a device anchor: hidden here rather than never created,
			// because the device and the pipelines are chosen against its surface.
			if ((config.hiddenMainWindow || config.offscreen) && mainWindow->isOpen()) {
				mainWindow->hide();
			}

			// What one layout unit is worth here. Settled once, before the font is baked,
			// because the atlas is baked at it: `uiScale = 0` means ask the display, which
			// is what an application wants unless it says otherwise.
			gRendererSetup.unitScale = config.gui.uiScale > 0.0f
				? config.gui.uiScale
				: mainWindow->contentScale();
			if (gRendererSetup.unitScale != 1.0f) {
				RDA_LOG_INFO("Interface scale " << gRendererSetup.unitScale << "x"
				             << (config.gui.uiScale > 0.0f ? " (set by the application)"
				                                           : " (from the display)"));
			}

			gRendererReady = true;
			if (!gRenderer.init(*mainWindow, fontPath, config.gui.fontHeight,
			                   config.gui.fontSizes,
			                    config.viewportMode, config.gui.enabled,
			                    gRendererSetup.unitScale)) {
				RDA_RUNTIME_ERROR("Failed to init the renderer");
			}
			gRenderer.setGuiLayerCaching(config.cacheGuiLayer);

			// Everything below is GUI-only. With config.gui.enabled false none of it
			// exists: no font metrics, no theme, no languages, no clipboard hooks.
			if (config.gui.enabled) {
				setUpWindowGui(*mainWindow);

				// And watch the theme, with the meshes it names, for the rest of the run.
				// Unlike a layout -- which the application names and polls itself -- a
				// theme and its meshes are the engine's own assets and it already knows
				// where they are, so no application has to ask for this. In a build
				// without hot reload it does nothing at all. The main window's theme is the
				// one watched; another window keeps the look it opened with.
				Layout::styleWatch().open(mainWindow->gui().theme(), config.gui.themePath);
			}
		}
	}

	// The same, for teardown. The GUI is already destroyed by the time the windows are, so
	// there are no descriptor sets left to release -- only images to free, and only while
	// there is still an allocator to free them with. A widget tree is destroyed with the
	// window that owns it, which is after the renderer has gone and before the device has.
	static void destroyRetiredTextures() {
		for (auto& texture : takeRetiredTextures()) {
			if (texture) texture->destroy();
		}
	}

	// Reverse of engineBringUp, on the same thread. Everything GPU-side is released
	// while the device and allocator are still alive.
	void engineTearDown() {
		if (mainWindow) {
			gRenderer.waitIdle();
		}

		gScene.clear();

		// Latch the pools closed and destroy every pooled resource now: any obj_ref the
		// application still holds becomes an inert handle instead of a dangling one.
		releaseResourcePools();

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

		destroyVulkanInstance();
		Wake::setPoster(nullptr); // no more wake-ups past this point
		Wake::setInterfaceChangedHandler(nullptr);
		endWindowSystem();
	}
}

namespace RDA {
	Material createForwardMaterial() { return Runtime::gRenderer.forwardMaterial(); }
	Material createForwardMaterial(const MaterialTextures& textures) {
		return Runtime::gRenderer.forwardMaterial(textures);
	}

	void releaseForwardMaterial(Material& material) { Runtime::gRenderer.releaseMaterial(material); }

	// The handles a viewport callback needs to build Vulkan work of its own. Read from
	// the live device rather than cached, so a call before the engine is up returns
	// nulls instead of something stale.
	ViewportGpu viewportGpu() {
		ViewportGpu gpu;
		GPUInfo& info = getGPU();
		gpu.instance = vulkanInstance();
		gpu.physicalDevice = info.PDevice;
		gpu.device = info.LDevice;
		gpu.graphicsQueue = info.graphicsQueue;
		gpu.graphicsFamily = info.graphicsFamily;
		gpu.allocator = getAllocator();
		gpu.framesInFlight = Renderer::MAX_FRAMES_IN_FLIGHT;
		gpu.renderPass = Runtime::gRenderer.sceneRenderPass();
		gpu.colorFormat = Runtime::gRenderer.sceneColorFormat();
		if (info.PDevice != VK_NULL_HANDLE) gpu.depthFormat = FrameBuffer::findDepthFormat();
		return gpu;
	}
}

RDA::Window* rendeerCreateWindow(uint32_t width, uint32_t height, const char* title,
                                 bool vsync) {
	using namespace RDA;
	using namespace RDA::Runtime;
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
		// The same question the first window answered, asked of whichever window is first
		// to exist: the atlas is baked against this scale, so it has to be known now. Only
		// when the application said nothing -- an explicit 1.0 means physical pixels and is
		// not an absence of an answer.
		if (gRendererSetup.unitScale <= 0.0f) {
			gRendererSetup.unitScale = window->contentScale();
		}
		if (!gRenderer.init(*window, gRendererSetup.fontPath, gRendererSetup.fontHeight,
			                   gRendererSetup.fontSizes,
		                    gRendererSetup.viewportMode, gRendererSetup.guiEnabled,
		                    gRendererSetup.unitScale)) {
			RDA_LOG_ERROR("Failed to init the renderer against the first window");
			window->closeWindow();
			delete window;
			return nullptr;
		}
		gRendererReady = true;
		RDA_LOG_INFO("Renderer brought up by the first window");
	}

	// Same footing as the main window, through the same function: font metrics, the
	// languages and theme, and the clipboard.
	if (gRenderer.guiEnabled()) setUpWindowGui(*window);
	return window;
}

void rendeerDestroyWindow(RDA::Window* window) {
	using namespace RDA;
	using namespace RDA::Runtime;
	if (!window || window == mainWindow) return; // the main window is the engine's

	// Its fences and semaphores may still be in use by a frame that has not finished.
	gRenderer.waitIdle();
	gRenderer.forgetWindow(window);
	forgetWindowInput(window);
	window->closeWindow();
	delete window;
}

RDA::Window* getMainWindow() {
	return RDA::Runtime::mainWindow;
}
