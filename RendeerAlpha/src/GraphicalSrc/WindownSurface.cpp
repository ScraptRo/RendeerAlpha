#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GraphicalSrc/WindownSurface.h>
#include <Logger/Logger.h>

namespace RDA {
    bool WindownSurface::Create(VkInstance vkInstance, void* windowHandle) {
        instance = vkInstance;
        nativeWindow = windowHandle;
        if (glfwCreateWindowSurface(vkInstance, reinterpret_cast<GLFWwindow*>(windowHandle), nullptr, &surface) != VK_SUCCESS) {
            RDA_LOG_ERROR("Failed to create window surface");
            return false;
        }
        return true;
    }

    void WindownSurface::Destroy() {
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        surface = VK_NULL_HANDLE;
    }

    VkSurfaceCapabilitiesKHR WindownSurface::GetCapabilities(VkPhysicalDevice physicalDevice) const {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
        return capabilities;
    }

    std::vector<VkSurfaceFormatKHR> WindownSurface::GetFormats(VkPhysicalDevice physicalDevice) const {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        if (count != 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
        }
        return formats;
    }

    std::vector<VkPresentModeKHR> WindownSurface::GetPresentModes(VkPhysicalDevice physicalDevice) const {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        if (count != 0) {
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
        }
        return modes;
    }
}
