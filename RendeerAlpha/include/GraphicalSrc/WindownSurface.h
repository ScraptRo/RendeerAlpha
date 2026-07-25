#pragma once
#include <Core/Datatypes.h>
namespace RDA{
    struct WindownSurface
    {
        // The GLFW window handle this surface was created from.
        void* nativeWindow = nullptr;

        // Create the Vulkan surface from a GLFWwindow* passed as windowHandle.
        bool Create(VkInstance vkInstance, void* windowHandle);

        // Destroy the Vulkan surface.
        void Destroy();

        VkSurfaceKHR Get() const { return surface; }

        // Query surface capabilities, formats, etc. (optional)
        VkSurfaceCapabilitiesKHR GetCapabilities(VkPhysicalDevice physicalDevice) const;
        std::vector<VkSurfaceFormatKHR> GetFormats(VkPhysicalDevice physicalDevice) const;
        std::vector<VkPresentModeKHR> GetPresentModes(VkPhysicalDevice physicalDevice) const;
    private:
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkInstance instance = VK_NULL_HANDLE;
    };
}
