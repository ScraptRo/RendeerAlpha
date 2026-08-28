#include "ModuleTransform.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace RDA::Layout {

	namespace {
		// npm writes a .cmd shim on Windows and a plain executable elsewhere.
#if defined(_WIN32)
		constexpr const char* kEsbuildName = "esbuild.cmd";
#else
		constexpr const char* kEsbuildName = "esbuild";
#endif

		// Runs a command and captures everything it writes. stderr is folded in because a
		// failed transform reports there, and that text is the error worth showing.
		bool runCapturing(const std::string& command, std::string& output) {
#if defined(_WIN32)
			// cmd.exe strips the outermost pair of quotes, so the whole line is wrapped in
			// one more than it looks like it needs. A shell does not do that, which is why
			// this is not shared.
			const std::string wrapped = "\"" + command + "\" 2>&1";
			FILE* pipe = _popen(wrapped.c_str(), "r");
#else
			const std::string wrapped = command + " 2>&1";
			FILE* pipe = popen(wrapped.c_str(), "r");
#endif
			if (!pipe) return false;

			std::array<char, 4096> buffer{};
			output.clear();
			while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
				output += buffer.data();
			}
#if defined(_WIN32)
			return _pclose(pipe) == 0;
#else
			return pclose(pipe) == 0;
#endif
		}
	}

	std::string findEsbuild() {
		// RDA_ESBUILD wins. node_modules holds binaries for one platform, so a checkout
		// shared between Windows and Linux -- or any packager with its own copy -- needs
		// to say where a usable one is without disturbing the install already there.
		if (const char* fromEnvironment = std::getenv("RDA_ESBUILD")) {
			if (*fromEnvironment) return fromEnvironment;
		}

		std::error_code ec;
		std::filesystem::path dir = std::filesystem::current_path(ec);
		if (ec) return {};
		for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
			const std::filesystem::path candidate = dir / "node_modules" / ".bin" / kEsbuildName;
			if (std::filesystem::exists(candidate, ec)) return candidate.string();
			if (!dir.has_parent_path() || dir.parent_path() == dir) break;
			dir = dir.parent_path();
		}
		return {};
	}

	bool transformModule(const std::string& sourcePath, const std::string& esbuildPath,
	                     bool withJsx, std::string& js, std::string& error) {
		const std::string exe = esbuildPath.empty() ? findEsbuild() : esbuildPath;
		if (exe.empty()) {
			error = "esbuild not found. Run 'npm install' in the project root, set "
			        "RDA_ESBUILD, or configure with -DRDA_COMPILE_LAYOUTS=OFF.";
			return false;
		}

		std::string command = "\"" + exe + "\" \"" + sourcePath + "\"";
		if (withJsx) command += " --jsx-factory=h --jsx-fragment=Fragment";
		command += " --format=iife --global-name=__module --target=es2020";

		if (!runCapturing(command, js)) {
			error = js.empty() ? "esbuild failed" : js;
			return false;
		}
		return true;
	}
}
