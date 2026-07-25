#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/WindownSurface.h>
#include <GraphicalSrc/Swapchain.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalObjects/Input.h>
#include <GraphicalObjects/Gui.h>
struct GLFWwindow;
namespace RDA {
	// A window owns its GLFW handle, the Vulkan surface, the swapchain, and a surface
	// FrameBuffer built over that swapchain — its default render target. The renderer
	// draws into getFrameBuffer() unless it is pointed at another target (e.g. an
	// offscreen FrameBuffer), which is how a GUI can be rendered into the world and
	// then swapped back to the window with a single reference change.
	//
	// GLFW is main-thread only, so the OS-facing state (framebuffer size, close
	// request) is read once per pump by syncOSState() and cached here. Everything
	// else reads the cached values rather than calling GLFW itself.
	class Window
	{
		stack_element mID;
		GLFWwindow* mWindow = nullptr;

		WindowInfo mInfo;
		WindownSurface mSurface;
		Swapchain mSwapchain;
		FrameBuffer mFrameBuffer;
		Input mInput;
		Gui mGui;

		bool     mFramebufferResized = false;
		bool     mShouldClose = false;
		uint32_t mCachedWidth = 0;       // framebuffer size, in pixels
		uint32_t mCachedHeight = 0;
		uint32_t mCachedWinWidth = 0;    // window content size, in screen coords
		uint32_t mCachedWinHeight = 0;
	public:
		Window(WindowInfo&);
		~Window();

		void closeWindow();               // destroys the GLFW window
		bool windowIsUp() const;

		// Copies GLFW state into the cached values above. Main thread only.
		void syncOSState();

		// Rebuilds the swapchain at the current cached size.
		// Returns false when the window is minimized (nothing to size a swapchain to).
		bool recreateSwapchain();

		void markResized() { mFramebufferResized = true; }
		bool wasResized() const { return mFramebufferResized; }

		// Last framebuffer size seen by the event pump, in pixels.
		VkExtent2D cachedExtent() const { return { mCachedWidth, mCachedHeight }; }

		// Maps a cursor position (GLFW screen coords) into framebuffer pixels, so GUI
		// hit-testing lines up with what was rendered on a HiDPI display.
		glm::vec2 cursorToFramebuffer(glm::vec2 screenPos) const {
			if (mCachedWinWidth == 0 || mCachedWinHeight == 0) return screenPos;
			return { screenPos.x * static_cast<float>(mCachedWidth) / mCachedWinWidth,
			         screenPos.y * static_cast<float>(mCachedHeight) / mCachedWinHeight };
		}

		GLFWwindow*    getGLFW() const { return mWindow; }
		VkSurfaceKHR   getSurface() const { return mSurface.Get(); }
		Swapchain&     getSwapchain() { return mSwapchain; }
		// The window's default render target — a surface FrameBuffer over the swapchain.
		FrameBuffer&   getFrameBuffer() { return mFrameBuffer; }
		// This window's input state and event stream.
		Input&         input() { return mInput; }
		// This window's immediate-mode GUI.
		Gui&           gui() { return mGui; }
		const WindowInfo& getInfo() const { return mInfo; }

		static Window* getRefFromNode(stack_element&);
	};
}
