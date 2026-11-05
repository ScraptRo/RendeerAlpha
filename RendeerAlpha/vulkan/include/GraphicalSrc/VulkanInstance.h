#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

// The process's VkInstance, and in a Debug build the validation layers and the messenger
// that reports what they say.
//
// It used to be a global defined in the loop file and reached by the device code through
// an `extern` in a Core header, with the validation-layer list in the logger and the
// messenger's callback in the logger's header -- so the bottom of the stack depended on
// the top for the one object everything Vulkan starts from. It lives here now, with the
// rest of the Vulkan layer, and the loop only asks for it to be made and destroyed.
namespace RDA {

	// Creates the instance with the extensions the window system needs (the loop asks GLFW
	// for them, so nothing here knows about windows). In a Debug build it also enables
	// whichever validation layers are installed and sets up the messenger. False, having
	// logged why, when the instance cannot be created.
	bool createVulkanInstance(const std::string& applicationName, uint32_t applicationVersion,
	                          const std::vector<const char*>& extensions);

	// Destroys the messenger and the instance. After every device object is gone.
	void destroyVulkanInstance();

	// VK_NULL_HANDLE before createVulkanInstance and after destroyVulkanInstance.
	VkInstance vulkanInstance();

	// The validation layers a Debug build actually gets: those it asks for that are
	// installed, enumerated once. A layer is a separate package on Linux
	// (vulkan-validationlayers) and a separate SDK component elsewhere, and a Debug build on
	// a machine without it used to stop at "Failed to create vulkan instance" -- which named
	// neither the layer nor the fix. Now it runs without validation and says which layer it
	// went without. Empty in a Release build. The device asks for the same list.
	const std::vector<const char*>& enabledValidationLayers();
}
