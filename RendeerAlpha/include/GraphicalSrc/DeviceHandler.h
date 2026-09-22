#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <functional>

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

	// One-shot GPU work on the graphics queue, through a transient pool, waited for
	// before returning. Uploading a texture, generating its mips, and running a filter
	// over a picture are all this; it lives here rather than in any one of them because
	// it is a fact about the device and not about what is being submitted.
	//
	// Blocking, and on the loop thread. Fine for what it is used for -- none of which is
	// per-frame -- and the alternative is a fence somebody has to remember to wait on.
	//
	// Compute is submitted here too: a queue family with VK_QUEUE_GRAPHICS_BIT is
	// required by the specification to have VK_QUEUE_COMPUTE_BIT as well, so the graphics
	// queue is a compute queue and there is no second one to find.
	void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

	// Re-usable queries (also used by the swapchain).
	QueueFamilyIndices      findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
}
