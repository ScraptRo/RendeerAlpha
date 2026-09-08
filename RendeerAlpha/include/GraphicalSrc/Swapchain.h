#pragma once
#include <cstdint>
#include <Core/Datatypes.h>

namespace RDA {
	// Owns the VkSwapchainKHR, its images/views, and the per-image present sync.
	// The render pass and framebuffers live in GBuffer, which attaches these
	// image views as its final color attachment.
	// The surface is borrowed (owned by the Window), never destroyed here.
	//
	// Deliberately GLFW-free: it takes a fallback extent rather than a window, so
	// it can be created/recreated from a render thread (GLFW is main-thread only).
	class Swapchain {
	public:
		Swapchain() = default;

		// Both answer false when the surface has no area to draw into -- a minimised
		// window -- and recreate() then leaves the swapchain it already had alone.
		// Vulkan refuses a zero extent outright, so this is the difference between
		// "try again when it is back" and a validation error followed by a 0x0 depth
		// attachment that cannot be allocated.
		bool create(VkSurfaceKHR surface, VkExtent2D fallbackExtent);
		bool recreate(VkSurfaceKHR surface, VkExtent2D fallbackExtent);
		void destroy();

		// Whether a swapchain could be made for this surface right now. Asked before
		// anything is torn down, because the caller's cached window size is one event
		// pump behind the surface itself: minimise a window between the last poll and
		// the present that notices, and the cache still says 1280x800 while the
		// surface already says nothing at all.
		bool surfaceIsDrawable(VkSurfaceKHR surface, VkExtent2D fallbackExtent) const;

		// Selects the present mode used by (re)create: true = FIFO (vsync, cap to
		// refresh), false = prefer Mailbox (uncapped). Set once before create();
		// recreate() reuses it, so vsync survives a resize.
		void setVsync(bool vsync) { mVsync = vsync; }

		VkSwapchainKHR handle()     const { return mSwapchain; }
		VkExtent2D     extent()     const { return mExtent; }
		VkFormat       format()     const { return mFormat; }
		uint32_t       imageCount() const { return static_cast<uint32_t>(mImages.size()); }
		VkImageView    imageView(uint32_t index) const { return mImageViews[index]; }
		bool           isValid()    const { return mSwapchain != VK_NULL_HANDLE; }

		// Per-image present semaphore (signaled by the draw submit, waited on by present).
		VkSemaphore renderFinishedSemaphore(uint32_t index) const { return mRenderFinished[index]; }
		// Tracks which in-flight fence (if any) is currently using a given image.
		VkFence& imageInFlight(uint32_t index) { return mImagesInFlight[index]; }

	private:
		VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
		std::vector<VkImage>     mImages;
		std::vector<VkImageView> mImageViews;
		std::vector<VkSemaphore> mRenderFinished;   // one per image (owned)
		std::vector<VkFence>     mImagesInFlight;    // borrowed renderer fences, not owned
		VkFormat   mFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D mExtent{};
		bool       mVsync = true; // FIFO when set, Mailbox-if-available when not

		bool createSwapchain(VkSurfaceKHR surface, VkExtent2D fallbackExtent);
		void createImageViews();
		void createSyncObjects();

		static VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& available);
		VkPresentModeKHR          choosePresentMode(const std::vector<VkPresentModeKHR>& available) const;
		static VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D fallback);
	};
}
