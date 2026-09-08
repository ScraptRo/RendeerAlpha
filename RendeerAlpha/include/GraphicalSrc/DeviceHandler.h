#pragma once
#include <cstdint>
#include <Core/Datatypes.h>

// Forward declare the VMA allocator handle so we don't pull the heavy
// vk_mem_alloc.h header into everything that needs the device.
typedef struct VmaAllocator_T* VmaAllocator;

namespace RDA{
	// Basic shared information about the active graphics device.
	struct GPUInfo {
		VkPhysicalDevice PDevice = VK_NULL_HANDLE;
		VkDevice         LDevice = VK_NULL_HANDLE;
		VkQueue          graphicsQueue = VK_NULL_HANDLE;
		VkQueue          presentQueue = VK_NULL_HANDLE;
		uint32_t         graphicsFamily = 0;
		uint32_t         presentFamily = 0;
		bool             enabledGraphics = false;
	};

	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool isComplete() const {
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	struct SwapChainSupportDetails {
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	// Picks a GPU, creates the logical device + queues and the VMA allocator.
	bool initDeviceHandler();
	// Destroys the allocator and the logical device. Call after every GPU resource is gone.
	void destroyDeviceHandler();

	GPUInfo&         getGPU();
	VkDevice         getDevice();
	VkPhysicalDevice getPhysicalDevice();
	VmaAllocator     getAllocator();

	// Re-usable queries (also used by the swapchain).
	QueueFamilyIndices      findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
}
