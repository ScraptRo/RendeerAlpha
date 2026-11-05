#include <Logger/Logger.h>

#include <cstring>
#ifndef _WIN32
// strcasecmp. glibc usually reaches it through <string.h>, but that is a feature-macro
// away from not being true, and this is a one-line insurance policy.
#include <strings.h>
#endif

namespace RDA {
	namespace {
		// 0xFF is "nobody has asked yet". Resolved on the first log of the run, which is
		// early enough to catch the validation layers loading and late enough that a
		// static initialisation order problem cannot reach it.
		constexpr uint8_t kUnresolved = 0xFFu;
		std::atomic<uint8_t> gLevel{ kUnresolved };
		// Whether RDA_LOG decided it. An environment variable is somebody standing at the
		// machine; what the application compiled in does not get to argue.
		std::atomic<bool> gPinned{ false };

		bool sameWord(const char* text, const char* word) {
#ifdef _WIN32
			return _stricmp(text, word) == 0;
#else
			return strcasecmp(text, word) == 0;
#endif
		}

		uint8_t resolve() {
			uint8_t level = static_cast<uint8_t>(LogLevel::Warning);
			if (const char* asked = std::getenv("RDA_LOG")) {
				if (*asked) {
					if      (sameWord(asked, "off"))     level = static_cast<uint8_t>(LogLevel::Off);
					else if (sameWord(asked, "error"))   level = static_cast<uint8_t>(LogLevel::Error);
					else if (sameWord(asked, "warning")) level = static_cast<uint8_t>(LogLevel::Warning);
					else if (sameWord(asked, "warn"))    level = static_cast<uint8_t>(LogLevel::Warning);
					else if (sameWord(asked, "info"))    level = static_cast<uint8_t>(LogLevel::Info);
					else if (sameWord(asked, "all"))     level = static_cast<uint8_t>(LogLevel::Info);
					// Anything else is left at the default rather than refused: this is
					// read before there is anywhere sensible to complain to.
					gPinned.store(true, std::memory_order_relaxed);
				}
			}
			return level;
		}
	}

	LogLevel logLevel() {
		uint8_t now = gLevel.load(std::memory_order_relaxed);
		if (now == kUnresolved) {
			now = resolve();
			gLevel.store(now, std::memory_order_relaxed);
		}
		return static_cast<LogLevel>(now);
	}

	void setLogLevel(LogLevel level) {
		// Resolve first, so an application setting this before anything has logged does
		// not quietly win over an RDA_LOG that had not been looked at yet.
		(void)logLevel();
		if (gPinned.load(std::memory_order_relaxed)) return;
		gLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
	}
}

// A Debug build keeps the whole trace in a file; a Release build has no file at all, and
// an empty name is how Logger is told so.
#if _DEBUG
Logger logger("RDA_DEBUG.txt");
#else
Logger logger("");
#endif


