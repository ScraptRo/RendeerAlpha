#pragma once
#include <Core/Framework.h>
#include <atomic>
#include <mutex>
// strrchr, for RDA__FILENAME__ below. It arrived transitively before and the macro only
// expanded in Debug; now it expands everywhere, which is too much to rest on a chain of
// includes nobody wrote down.
#include <cstring>

// How much of the engine's own talking reaches the console.
//
// An application's terminal belongs to the application. Before this, a Debug build put
// every line the engine had -- every validation-layer probe, every "initialized", every
// route change -- into the same stream the program's own output goes to, and there was
// no way to ask it not to. Fifty lines of somebody else's start-up is not a diagnostic,
// it is a thing to scroll past.
//
// So the console is gated and the default is `Warning`: the engine says nothing unless
// something is wrong. The *file* is not gated at all -- a Debug build still writes every
// line into RDA_DEBUG.txt, because a file is nobody's terminal and the whole trace is
// what you want the moment something does go wrong.
//
// `RDA_LOG=off|error|warning|info` in the environment overrides whatever the application
// asked for, which is the escape hatch on a binary you cannot rebuild.
namespace RDA {

	enum class LogLevel : uint8_t { Off = 0, Error = 1, Warning = 2, Info = 3 };

	// Resolved once, from RDA_LOG, and `Warning` where that says nothing.
	LogLevel logLevel();

	// What an application asks for, through AppConfig::logLevel. Ignored when RDA_LOG
	// has already spoken: an environment variable is somebody standing at the machine,
	// and they outrank a value compiled in months ago.
	void setLogLevel(LogLevel level);

	// The check every log site makes. Cheap enough to sit in front of building the
	// message, which is what keeps a suppressed line free in a Release build.
	inline bool logs(LogLevel at) {
		return static_cast<uint8_t>(at) <= static_cast<uint8_t>(logLevel());
	}
}

#if _DEBUG

// The validation layers and the messenger that reports them are the Vulkan layer's, in
// GraphicalSrc/VulkanInstance -- not the logger's, which every file includes and which
// therefore must not bring Vulkan with it.

#define RDA_DEBUG_FUNC(x) x
// A Debug build has a file, and the file takes everything whatever the console level
// says. So the message is built either way, and only the console write is gated.
#define RDA_LOG_TO_FILE 1

#else

#define RDA_DEBUG_FUNC(x)
// A Release build has no file, so a message nobody will print is a message nobody
// should build. The level check in front of each macro is what makes that true.
#define RDA_LOG_TO_FILE 0

#endif // _DEBUG

// Exists in both configurations now. It used to be Debug-only, which meant a Release
// build could not report a failure at all -- not a theme that would not load, not a
// swapchain that would not be created. That is not "quiet", it is deaf.
class Logger {
private:
	std::ofstream logFile;
	// One line at a time, whichever thread writes it. The loop runs on its own thread, and
	// a backend, a stream producer and the accessibility threads log from theirs; two
	// unguarded writes to one stream interleave at best and corrupt it at worst.
	std::mutex lock;

	// Every line goes to the file; only some of them go to the console. `colour` is an
	// ANSI prefix, or nullptr for plain.
	void write(RDA::LogLevel level, const char* tag, const char* colour,
	           const std::string& file, const std::string& line, const std::string& message) {
		const std::string formatted =
			std::string("[") + tag + "] (" + file + "),(LINE= " + line + "): " + message;
		const std::lock_guard<std::mutex> held(lock);

		// Flushed every line: a debug log that buffers loses exactly the messages you
		// need when the process crashes instead of exiting cleanly.
		if (logFile.is_open()) logFile << formatted << std::endl;

		if (!RDA::logs(level)) return;
		if (colour) std::cout << colour << formatted << "\033[0m\n";
		else        std::cout << formatted << "\n";
	}

public:
	// An empty name opens nothing and says nothing about it: that is a Release build,
	// where there is no log file by design.
	explicit Logger(const std::string& filename) {
		if (filename.empty()) return;
		logFile.open(filename);
		if (!logFile) {
			std::cerr << "Failed to open log file: " << filename << std::endl;
		}
	}

	~Logger() {
		if (logFile.is_open()) logFile.close();
	}

	void logError(const std::string& file, const std::string& line, const std::string& message) {
		write(RDA::LogLevel::Error, "ERROR", "\033[1;31m", file, line, message);
	}
	void logWarning(const std::string& file, const std::string& line, const std::string& message) {
		write(RDA::LogLevel::Warning, "WARNING", "\033[1;33m", file, line, message);
	}
	void logSuccess(const std::string& file, const std::string& line, const std::string& message) {
		write(RDA::LogLevel::Info, "SUCCESS", "\033[1;32m", file, line, message);
	}
	void logInfo(const std::string& file, const std::string& line, const std::string& message) {
		write(RDA::LogLevel::Info, "INFO", nullptr, file, line, message);
	}
};

extern Logger logger;

#define RDA_STRINGIFY2(x) #x
#define RDA_STRINGIFY(x) RDA_STRINGIFY2(x)

// Strip the directory from __FILE__ for both Windows ('\\') and POSIX ('/') paths.
#define RDA__FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))

// `RDA_LOG_TO_FILE ||` is what makes the message still get built in a Debug build when
// the console is quiet -- the file wants it. In Release it folds to the level check
// alone, and a suppressed line costs one comparison.
#define RDA_LOG_AT(level, tag, x) do { \
		if (RDA_LOG_TO_FILE || RDA::logs(level)) { \
			std::ostringstream oss; \
			oss << x; \
			logger.log##tag(RDA__FILENAME__, std::to_string(__LINE__), oss.str()); \
		} \
	} while (0)

#define RDA_LOG_ERROR(x)   RDA_LOG_AT(RDA::LogLevel::Error,   Error,   x)
#define RDA_LOG_WARNING(x) RDA_LOG_AT(RDA::LogLevel::Warning, Warning, x)
#define RDA_LOG_SUCCESS(x) RDA_LOG_AT(RDA::LogLevel::Info,    Success, x)
// The name this macro was first given, misspelled. It stays as an alias so that code written
// against it keeps building; new code uses RDA_LOG_SUCCESS.
#define RDA_LOG_SUCCES(x)  RDA_LOG_SUCCESS(x)
#define RDA_LOG_INFO(x)    RDA_LOG_AT(RDA::LogLevel::Info,    Info,    x)

// Fatal, in both configurations.
//
// This used to expand to nothing in Release, which did not make a release build quiet --
// it made it carry on. `Failed to create GLFW window` went from a thrown exception to a
// null handle that the next line called through, and the report was a crash with no
// message anywhere. An error the engine cannot continue past is exactly the thing that
// must survive into the build people actually ship.
//
// The message is logged at Error, which the default level lets through, and then thrown.
// No destructing the logger first: the file is flushed per line, so there was nothing to
// force -- and calling ~Logger() by hand left the static destructor to run a second time
// over an object that had already gone.
#define RDA_RUNTIME_ERROR(x) do { \
		RDA_LOG_ERROR(x); \
		throw std::runtime_error(x); \
	} while (0)

