#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/Swapchain.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <limits>

namespace RDA {

	void Swapchain::create(VkSurfaceKHR surface, VkExtent2D fallbackExtent) {
		createSwapchain(surface, fallbackExtent);
		createImageViews();
		createSyncObjects();
	}

	void Swapchain::recreate(VkSurfaceKHR surface, VkExtent2D fallbackExtent) {
		destroy();
		create(surface, fallbackExtent);
	}

	void Swapchain::destroy() {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) return;

		for (VkSemaphore semaphore : mRenderFinished) {
			vkDestroySemaphore(device, semaphore, nullptr);
		}
		mRenderFinished.clear();
		mImagesInFlight.clear();

		for (VkImageView view : mImageViews) {
			vkDestroyImageView(device, view, nullptr);
		}
		mImageViews.clear();

		if (mSwapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(device, mSwapchain, nullptr);
			mSwapchain = VK_NULL_HANDLE;
		}
		mImages.clear();
	}

	void Swapchain::createSwapchain(VkSurfaceKHR surface, VkExtent2D fallbackExtent) {
		VkPhysicalDevice physicalDevice = getPhysicalDevice();
		SwapChainSupportDetails support = querySwapChainSupport(physicalDevice, surface);

		VkSurfaceFormatKHR surfaceFormat = chooseFormat(support.formats);
		VkPresentModeKHR   presentMode   = choosePresentMode(support.presentModes);
		VkExtent2D         extent        = chooseExtent(support.capabilities, fallbackExtent);

		uint32_t imageCount = support.capabilities.minImageCount + 1;
		if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
			imageCount = support.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
		uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };
		if (indices.graphicsFamily != indices.presentFamily) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		} else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		createInfo.preTransform = support.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VkDevice device = getDevice();
		if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &mSwapchain) != VK_SUCCESS) {
			RDA_RUNTIME_ERROR("Failed to create swapchain");
		}

		uint32_t actualImageCount = 0;
		vkGetSwapchainImagesKHR(device, mSwapchain, &actualImageCount, nullptr);
		mImages.resize(actualImageCount);
		vkGetSwapchainImagesKHR(device, mSwapchain, &actualImageCount, mImages.data());

		mFormat = surfaceFormat.format;
		mExtent = extent;
	}

	void Swapchain::createImageViews() {
		VkDevice device = getDevice();
		mImageViews.resize(mImages.size());
		for (size_t i = 0; i < mImages.size(); i++) {
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = mImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = mFormat;
			viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;
			if (vkCreateImageView(device, &viewInfo, nullptr, &mImageViews[i]) != VK_SUCCESS) {
				RDA_RUNTIME_ERROR("Failed to create swapchain image view");
			}
		}
	}

	void Swapchain::createSyncObjects() {
		VkDevice device = getDevice();
		mRenderFinished.resize(mImages.size());
		mImagesInFlight.assign(mImages.size(), VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (size_t i = 0; i < mRenderFinished.size(); i++) {
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &mRenderFinished[i]) != VK_SUCCESS) {
				RDA_RUNTIME_ERROR("Failed to create present semaphore");
			}
		}
	}

	VkSurfaceFormatKHR Swapchain::chooseFormat(const std::vector<VkSurfaceFormatKHR>& available) {
		for (const auto& format : available) {
			if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return format;
			}
		}
		return available[0];
	}

	VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& available) {
		for (const auto& mode : available) {
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return mode;
			}
		}
		return VK_PRESENT_MODE_FIFO_KHR; // guaranteed to be available
	}

	VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D fallback) {
		// Most drivers report the real size here; the fallback only matters when the
		// surface says "match the window", which is why callers pass their cached
		// framebuffer size instead of us asking GLFW (main-thread only).
		if (caps.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
			return caps.currentExtent;
		}
		VkExtent2D extent = fallback;
		extent.width  = std::clamp(extent.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
		extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
		return extent;
	}
}
