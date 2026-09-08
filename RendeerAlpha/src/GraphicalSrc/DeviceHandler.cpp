#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <Core/BackendConnector.h>
#include <vendor/vma/vma.h>

namespace RDA {

    // ---- Engine-owned device state -------------------------------------------------
    static GPUInfo      gGPU;
    static VmaAllocator gAllocator = nullptr;
    static bool         gDeviceInited = false;
    static std::vector<VkPhysicalDevice> gPhysicalDevices;

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // ---- Forward declarations ------------------------------------------------------
    static bool             createLogicalDevice(VkSurfaceKHR surface);
    static VkPhysicalDevice bestDeviceForGraphics(VkSurfaceKHR surface);
    static bool             isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    static bool             checkDeviceExtensionSupport(VkPhysicalDevice device);

    // ---- Accessors -----------------------------------------------------------------
    GPUInfo&         getGPU() { return gGPU; }
    VkDevice         getDevice() { return gGPU.LDevice; }
    VkPhysicalDevice getPhysicalDevice() { return gGPU.PDevice; }
    VmaAllocator     getAllocator() { return gAllocator; }

    bool initDeviceHandler() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(appInstance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            RDA_LOG_ERROR("No Vulkan-capable devices found");
            return false;
        }
        gPhysicalDevices.resize(deviceCount);
        vkEnumeratePhysicalDevices(appInstance, &deviceCount, gPhysicalDevices.data());

        // Device selection needs a surface. We spin up a hidden, throw-away window
        // purely to probe surface support, then create the logical device from it.
        // Real windows are created afterwards and reuse this device.
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* probeWindow = glfwCreateWindow(1, 1, "RDA_DeviceProbe", nullptr, nullptr);
        glfwDefaultWindowHints();
        if (!probeWindow) {
            RDA_LOG_ERROR("Failed to create device-probe window");
            return false;
        }
        VkSurfaceKHR probeSurface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(appInstance, probeWindow, nullptr, &probeSurface) != VK_SUCCESS) {
            RDA_LOG_ERROR("Failed to create device-probe surface");
            glfwDestroyWindow(probeWindow);
            return false;
        }

        bool ok = createLogicalDevice(probeSurface);

        vkDestroySurfaceKHR(appInstance, probeSurface, nullptr);
        glfwDestroyWindow(probeWindow);

        if (!ok) return false;

        // ---- VMA allocator (hybrid backend: VMA owns the device memory) ----
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice   = gGPU.PDevice;
        allocatorInfo.device           = gGPU.LDevice;
        allocatorInfo.instance         = appInstance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
        if (vmaCreateAllocator(&allocatorInfo, &gAllocator) != VK_SUCCESS) {
            RDA_LOG_ERROR("Failed to create VMA allocator");
            return false;
        }

        gDeviceInited = true;
        RDA_LOG_SUCCES("Device handler initialized");
        return true;
    }

    void destroyDeviceHandler() {
        if (!gDeviceInited) return;
        if (gAllocator) {
            vmaDestroyAllocator(gAllocator);
            gAllocator = nullptr;
        }
        if (gGPU.LDevice != VK_NULL_HANDLE) {
            vkDestroyDevice(gGPU.LDevice, nullptr);
            gGPU.LDevice = VK_NULL_HANDLE;
        }
        gDeviceInited = false;
    }

    static bool createLogicalDevice(VkSurfaceKHR surface) {
        VkPhysicalDevice best = bestDeviceForGraphics(surface);
        if (best == VK_NULL_HANDLE) {
            RDA_LOG_ERROR("No suitable GPU found");
            return false;
        }
        gGPU.PDevice = best;
        gGPU.enabledGraphics = true;

        QueueFamilyIndices indices = findQueueFamilies(best, surface);
        gGPU.graphicsFamily = indices.graphicsFamily.value();
        gGPU.presentFamily  = indices.presentFamily.value();

        std::set<uint32_t> uniqueQueueFamilies = { gGPU.graphicsFamily, gGPU.presentFamily };
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.sampleRateShading = VK_TRUE;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        RDA_DEBUG_FUNC(
            const std::vector<const char*>& layers = enabledValidationLayers();
            createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
            createInfo.ppEnabledLayerNames = layers.data();
        )

        if (vkCreateDevice(best, &createInfo, nullptr, &gGPU.LDevice) != VK_SUCCESS) {
            RDA_LOG_ERROR("Failed to create logical device");
            return false;
        }

        vkGetDeviceQueue(gGPU.LDevice, gGPU.graphicsFamily, 0, &gGPU.graphicsQueue);
        vkGetDeviceQueue(gGPU.LDevice, gGPU.presentFamily, 0, &gGPU.presentQueue);
        return true;
    }

    static VkPhysicalDevice bestDeviceForGraphics(VkSurfaceKHR surface) {
        VkPhysicalDevice fallback = VK_NULL_HANDLE;
        for (auto device : gPhysicalDevices) {
            if (!isDeviceSuitable(device, surface)) continue;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            // Prefer a discrete GPU, otherwise keep the first suitable one.
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                return device;
            }
            if (fallback == VK_NULL_HANDLE) {
                fallback = device;
            }
        }
        return fallback;
    }

    static bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
        QueueFamilyIndices indices = findQueueFamilies(device, surface);
        bool extensionsSupported = checkDeviceExtensionSupport(device);
        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }
        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
        return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
        uint32_t i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
            if (indices.isComplete()) {
                break;
            }
            i++;
        }
        return indices;
    }

    static bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }
        return requiredExtensions.empty();
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }
        return details;
    }
}
