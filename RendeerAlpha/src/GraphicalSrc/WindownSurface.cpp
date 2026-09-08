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

}
