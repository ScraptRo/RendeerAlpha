#pragma once
#include <vulkan/vulkan.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalSrc/Swapchain.h>
#include <GraphicalSrc/WindownSurface.h>

struct GLFWwindow;

namespace RDA {

	// How a window's pictures reach the screen: the Vulkan surface over the OS window, the
	// swapchain on it, and the surface FrameBuffer built over that swapchain -- the window's
	// default render target.
	//
	// A class of its own, in the engine layer, because none of it is about the window. It is
	// how a Vulkan image gets presented, and it was the part of Window the renderer needed. A
	// window owns one and decides when it is rebuilt; this knows how.
	class PresentationSurface {
	public:
		enum class Created {
			Ready,
			// The window has no area yet -- a session restoring its windows minimised, or a
			// compositor that has not given it a size. Not fatal: the first frame that finds
			// it restored builds the swapchain.
			Deferred,
			NoSurface,
			NoFrameBuffer,
		};

		// Makes the surface, then the swapchain and its FrameBuffer at `extent`.
		Created create(VkInstance instance, GLFWwindow* window, VkExtent2D extent, bool vsync);

		// Rebuilds the swapchain and its FrameBuffer at `extent`. False when there is nothing
		// to size them to -- a minimised window -- and then nothing was destroyed.
		bool recreate(VkExtent2D extent);

		// Waits for the device, then destroys all three, newest first.
		void destroy();

		Swapchain&   swapchain() { return mSwapchain; }
		FrameBuffer& framebuffer() { return mFrameBuffer; }
		VkSurfaceKHR surface() const { return mSurface.Get(); }

	private:
		WindownSurface mSurface;
		Swapchain      mSwapchain;
		FrameBuffer    mFrameBuffer;
	};
}
