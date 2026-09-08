#include <Core/Location.h>

#include <system_error>

#if defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else
	#include <dlfcn.h>
	#include <unistd.h>
#endif

namespace RDA {

	namespace {
		// Any function in this translation unit: what matters is that its address is in
		// the engine's own module, whichever module that turns out to be.
		void anchor() {}

		std::filesystem::path parentOf(const std::filesystem::path& file) {
			std::error_code ec;
			const std::filesystem::path absolute = std::filesystem::absolute(file, ec);
			if (ec || absolute.empty()) return {};
			return absolute.parent_path();
		}
	}

	std::filesystem::path executablePath() {
#if defined(_WIN32)
		char buffer[MAX_PATH];
		const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH) return {};
		std::error_code ec;
		const std::filesystem::path absolute = std::filesystem::absolute(buffer, ec);
		return ec ? std::filesystem::path() : absolute;
#else
		std::error_code ec;
		const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
		return ec ? std::filesystem::path() : self;
#endif
	}

	std::filesystem::path executableDirectory() {
		const std::filesystem::path self = executablePath();
		return self.empty() ? self : self.parent_path();
	}

	std::filesystem::path engineModuleDirectory() {
#if defined(_WIN32)
		HMODULE module = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                        reinterpret_cast<LPCSTR>(&anchor), &module)) {
			return executableDirectory();
		}
		char buffer[MAX_PATH];
		const DWORD length = GetModuleFileNameA(module, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH) return executableDirectory();
		return parentOf(buffer);
#else
		Dl_info info{};
		if (dladdr(reinterpret_cast<void*>(&anchor), &info) == 0 || !info.dli_fname) {
			return executableDirectory();
		}
		// For the main program dladdr reports whatever argv[0] was, which may be a bare
		// name; the kernel's link is exact. A shared object is reported by the path it
		// was opened with, which is absolute or relative to the working directory, and
		// either resolves.
		const std::filesystem::path reported = info.dli_fname;
		if (!reported.has_parent_path()) return executableDirectory();
		return parentOf(reported);
#endif
	}

	std::string resolveEngineAsset(const std::string& path) {
		if (path.empty()) return path;
		std::error_code ec;
		if (std::filesystem::exists(path, ec)) return path;
		const std::filesystem::path given = path;
		if (given.is_absolute()) return path;

		const std::filesystem::path module = engineModuleDirectory();
		if (!module.empty()) {
			const std::filesystem::path beside = module / given;
			if (std::filesystem::exists(beside, ec)) return beside.string();
		}
		return path;
	}
}
