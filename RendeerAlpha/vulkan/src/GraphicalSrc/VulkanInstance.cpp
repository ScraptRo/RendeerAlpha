#include <GraphicalSrc/VulkanInstance.h>
#include <Logger/Logger.h>

#include <cstring>

namespace RDA {

	namespace {
		VkInstance gInstance = VK_NULL_HANDLE;

#if _DEBUG
		VkDebugUtilsMessengerEXT gMessenger = VK_NULL_HANDLE;

		// The validation layers a Debug build asks for.
		const std::vector<const char*> kValidationLayers = {
			"VK_LAYER_KHRONOS_validation"
		};

		VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		                                             VkDebugUtilsMessageTypeFlagsEXT,
		                                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		                                             void*) {
			// The Vulkan loader reports a begin "error" when an implicit layer's manifest
			// can't be opened (e.g. a stale NVIDIA Nsight install left a dangling JSON path
			// in the registry). It's an environment issue, not ours, so swallow it.
			if (pCallbackData && pCallbackData->pMessage) {
				std::string msg = pCallbackData->pMessage;
				if (msg.find("loader_get_json") != std::string::npos ||
					msg.find("Failed to open JSON file") != std::string::npos) {
					return VK_FALSE;
				}
			}
			switch (messageSeverity)
			{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				RDA_LOG_ERROR("validation layer: " << pCallbackData->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				RDA_LOG_WARNING("validation layer: " << pCallbackData->pMessage);
				break;
			default:
				RDA_LOG_INFO("validation layer: " << pCallbackData->pMessage);
				break;
			}
			return VK_FALSE;
		}

		void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
			createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			createInfo.pfnUserCallback = debugCallback;
		}

		VkResult createDebugMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		                              VkDebugUtilsMessengerEXT* pDebugMessenger) {
			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
			return func ? func(instance, pCreateInfo, nullptr, pDebugMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
		}

		void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
			if (func) func(instance, messenger, nullptr);
		}
#endif
	}

	const std::vector<const char*>& enabledValidationLayers() {
#if _DEBUG
		static const std::vector<const char*> enabled = [] {
			uint32_t count = 0;
			vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> present(count);
			if (count) vkEnumerateInstanceLayerProperties(&count, present.data());

			std::vector<const char*> out;
			for (const char* wanted : kValidationLayers) {
				bool found = false;
				for (const VkLayerProperties& layer : present) {
					if (std::strcmp(layer.layerName, wanted) == 0) { found = true; break; }
				}
				if (found) {
					out.push_back(wanted);
				} else {
					RDA_LOG_WARNING("Validation layer " << wanted << " is not installed; running "
					                "without it. On Debian and Ubuntu it is the package "
					                "vulkan-validationlayers; elsewhere it is part of the Vulkan SDK.");
				}
			}
			return out;
		}();
		return enabled;
#else
		static const std::vector<const char*> none;
		return none;
#endif
	}

	bool createVulkanInstance(const std::string& applicationName, uint32_t applicationVersion,
	                          const std::vector<const char*>& windowExtensions) {
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = applicationName.c_str();
		appInfo.applicationVersion = applicationVersion;
		appInfo.pEngineName = "Rendeer Alpha";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;

		std::vector<const char*> extensions = windowExtensions;
#if _DEBUG
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
		createInfo.ppEnabledExtensionNames = extensions.data();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
#if _DEBUG
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		const std::vector<const char*>& layers = enabledValidationLayers();
		createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
		createInfo.ppEnabledLayerNames = layers.data();
		populateDebugMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = &debugCreateInfo;
#endif
		if (vkCreateInstance(&createInfo, nullptr, &gInstance) != VK_SUCCESS) {
			gInstance = VK_NULL_HANDLE;
			RDA_LOG_ERROR("Failed to create vulkan instance!");
			return false;
		}
#if _DEBUG
		if (createDebugMessenger(gInstance, &debugCreateInfo, &gMessenger) != VK_SUCCESS) {
			RDA_RUNTIME_ERROR("Failed to set up debug messenger!");
		}
#endif
		return true;
	}

	void destroyVulkanInstance() {
		if (gInstance == VK_NULL_HANDLE) return;
#if _DEBUG
		if (gMessenger != VK_NULL_HANDLE) destroyDebugMessenger(gInstance, gMessenger);
		gMessenger = VK_NULL_HANDLE;
#endif
		vkDestroyInstance(gInstance, nullptr);
		gInstance = VK_NULL_HANDLE;
	}

	VkInstance vulkanInstance() { return gInstance; }
}
