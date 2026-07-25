#pragma once
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

		void create(VkSurfaceKHR surface, VkExtent2D fallbackExtent);
		void recreate(VkSurfaceKHR surface, VkExtent2D fallbackExtent);
		void destroy();

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

		void createSwapchain(VkSurfaceKHR surface, VkExtent2D fallbackExtent);
		void createImageViews();
		void createSyncObjects();

		static VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& available);
		static VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>& available);
		static VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D fallback);
	};
}
