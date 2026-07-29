#include <Logger/Logger.h>

// The Logger type and the validation-layer list only exist in debug builds (see the
// #if _DEBUG in Logger.h), so their definitions have to be guarded the same way —
// otherwise a release build tries to define an object of an undeclared type.
#if _DEBUG
Logger logger("RDA_DEBUG.txt");
const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation"
};
#endif

RDA_DEBUG_FUNC(void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
})