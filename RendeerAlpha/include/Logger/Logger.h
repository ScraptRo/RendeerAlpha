#pragma once
#include <Core/Framework.h>


#if _DEBUG

// The validation layers a Debug build asks for.
extern const std::vector<const char*> validationLayers;

// The ones it gets: those of the above that are actually installed, enumerated once on
// first use. A layer is a separate package on Linux (vulkan-validationlayers) and a
// separate SDK component elsewhere, and a Debug build on a machine without it used to
// stop at "Failed to create vulkan instance" -- which named neither the layer nor the
// fix. Now it runs without validation and says which layer it went without. Defined in
// RendeerAlpha.cpp rather than beside the list, because the test runner compiles
// Logger.cpp with no Vulkan loader to enumerate anything from.
const std::vector<const char*>& enabledValidationLayers();

class Logger {
private:
   std::ofstream logFile;

public:
   Logger(const std::string& filename) {
       logFile.open(filename);
       if (!logFile) {
           std::cerr << "Failed to open log file: " << filename << std::endl;
       }
   }

   ~Logger() {
       if (logFile.is_open()) {
           logFile.close();
       }
   }

   void logError(const std::string& file, const std::string& line, const std::string& message) {
       std::string formattedMessage = "[ERROR] (" + file + "),(LINE= " + line + "): " + message;

       // Write to file
       // Flushed every line: a debug log that buffers loses exactly the messages
       // you need when the process crashes instead of exiting cleanly.
       if (logFile.is_open()) {
           logFile << formattedMessage << std::endl;
       }

       // Write to console with color
       std::cout << "\033[1;31m" << formattedMessage << "\033[0m\n";
   }

   void logInfo(const std::string& file, const std::string& line, const std::string& message) {
       std::string formattedMessage = "[INFO] (" + file + "),(LINE= " + line + "): " + message;

       // Write to file
       // Flushed every line: a debug log that buffers loses exactly the messages
       // you need when the process crashes instead of exiting cleanly.
       if (logFile.is_open()) {
           logFile << formattedMessage << std::endl;
       }

       // Write to console with color
       std::cout << formattedMessage << "\n";
   }

   void logWarning(const std::string& file, const std::string& line, const std::string& message) {
       std::string formattedMessage = "[WARNING] (" + file + "),(LINE= " + line + "): " + message;
       // Write to file
       // Flushed every line: a debug log that buffers loses exactly the messages
       // you need when the process crashes instead of exiting cleanly.
       if (logFile.is_open()) {
           logFile << formattedMessage << std::endl;
       }
       // Write to console with color
       std::cout << "\033[1;33m" << formattedMessage << "\033[0m\n";
   }
   void logSuccess(const std::string& file, const std::string& line, const std::string& message) {
       std::string formattedMessage = "[SUCCESS] (" + file + "),(LINE= " + line + "): " + message;
       // Write to file
       // Flushed every line: a debug log that buffers loses exactly the messages
       // you need when the process crashes instead of exiting cleanly.
       if (logFile.is_open()) {
           logFile << formattedMessage << std::endl;
       }
       // Write to console with color
       std::cout << "\033[1;32m" << formattedMessage << "\033[0m\n";
   }
};

extern Logger logger;

#define RDA_DEBUG_FUNC(x) x

#define RDA_STRINGIFY2(x) #x
#define RDA_STRINGIFY(x) RDA_STRINGIFY2(x)

// Strip the directory from __FILE__ for both Windows ('\\') and POSIX ('/') paths.
#define RDA__FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))

#define RDA_LOG_ERROR(x) \
   do { \
       std::ostringstream oss; \
       oss << x; \
       logger.logError(RDA__FILENAME__,  std::to_string(__LINE__), oss.str()); \
   } while (0)
#define RDA_LOG_WARNING(x)  do { \
       std::ostringstream oss; \
       oss << x; \
       logger.logWarning(RDA__FILENAME__,  std::to_string(__LINE__), oss.str()); \
   } while (0)
#define RDA_LOG_SUCCES(x)   do { \
       std::ostringstream oss; \
       oss << x; \
       logger.logSuccess(RDA__FILENAME__,  std::to_string(__LINE__), oss.str()); \
   } while (0)
#define RDA_LOG_INFO(x)      do { \
       std::ostringstream oss; \
       oss << x; \
       logger.logInfo(RDA__FILENAME__,  std::to_string(__LINE__), oss.str()); \
   } while (0)
#define RDA_RUNTIME_ERROR(x) logger.~Logger();throw std::runtime_error(x);


// Defined in a header, so every translation unit that logs gets a copy and only the
// one that installs the messenger uses it. GCC points that out per file; it is meant.
[[maybe_unused]] static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
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


RDA_DEBUG_FUNC(void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo));


#else

#define RDA_DEBUG_FUNC(x)

#define RDA_LOG_ERROR(x)
#define RDA_LOG_WARNING(x)
#define RDA_LOG_SUCCES(x)
#define RDA_LOG_INFO(x)
#define RDA_RUNTIME_ERROR(x)

#endif // _DEBUG
