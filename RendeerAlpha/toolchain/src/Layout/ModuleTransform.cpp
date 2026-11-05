#include <Layout/ModuleTransform.h>
#include <Core/Location.h>
#include <Logger/Logger.h>
// The preludes below, as the bytes of the .js files in js/, embedded by rda_embed_js.
#include <ColourPreludeSource.h>
#include <LayoutPreludeSource.h>
#include <MeshPreludeSource.h>
#include <StatePreludeSource.h>
#include <vector>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <fstream>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace RDA::Layout {

	namespace {
		// npm writes a .cmd shim on Windows and a plain executable elsewhere.
#if defined(_WIN32)
		constexpr const char* kEsbuildName = "esbuild.cmd";
		constexpr const char* kEsbuildBinary = "esbuild.exe";
#else
		constexpr const char* kEsbuildName = "esbuild";
		constexpr const char* kEsbuildBinary = "esbuild";
#endif

		// Runs a command, keeping what it wrote to stdout and what it wrote to stderr apart.
		//
		// They used to be folded together with 2>&1, because a failed transform reports on
		// stderr and that text is the error worth showing. But esbuild writes the module to
		// stdout and its *warnings* to stderr, and it exits 0 with both -- so a duplicate
		// key in an object literal put "[WARNING] Duplicate key ..." into the middle of the
		// JavaScript, and what the author was told was `SyntaxError: unexpected character`,
		// from QuickJS, with no line number and no mention of a duplicate anything.
		//
		// Measured on a mesh with one key written twice: that error before, the real warning
		// and a compiled mesh after.
		bool runCapturing(const std::string& command, std::string& output,
		                  std::string& diagnostics) {
			output.clear();
			diagnostics.clear();

			// stderr goes to a file beside the temporary directory rather than down the
			// pipe. Named per call, since two layouts may be compiled at once.
			static std::atomic<uint32_t> counter{ 0 };
			std::error_code ec;
			const std::filesystem::path errors =
				std::filesystem::temp_directory_path(ec) /
				("rda-esbuild-" + std::to_string(counter.fetch_add(1)) + "-" +
				 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
				 ".txt");

#if defined(_WIN32)
			// cmd.exe strips the outermost pair of quotes, so the whole line is wrapped in
			// one more than it looks like it needs. A shell does not do that, which is why
			// this is not shared.
			const std::string wrapped =
				"\"" + command + " 2>\"" + errors.string() + "\"\"";
			FILE* pipe = _popen(wrapped.c_str(), "r");
#else
			const std::string wrapped = command + " 2>\"" + errors.string() + "\"";
			FILE* pipe = popen(wrapped.c_str(), "r");
#endif
			if (!pipe) return false;

			std::array<char, 4096> buffer{};
			while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
				output += buffer.data();
			}
#if defined(_WIN32)
			const bool ok = _pclose(pipe) == 0;
#else
			const bool ok = pclose(pipe) == 0;
#endif

			{
				std::ifstream file(errors, std::ios::binary);
				if (file) {
					diagnostics.assign(std::istreambuf_iterator<char>(file),
					                   std::istreambuf_iterator<char>());
				}
			}
			std::filesystem::remove(errors, ec);
			return ok;
		}
	}

	std::string findEsbuild() {
		// RDA_ESBUILD wins. node_modules holds binaries for one platform, so a checkout
		// shared between Windows and Linux -- or any packager with its own copy -- needs
		// to say where a usable one is without disturbing the install already there.
		if (const char* fromEnvironment = std::getenv("RDA_ESBUILD")) {
			if (*fromEnvironment) return fromEnvironment;
		}

#ifdef RDA_ESBUILD_DEFAULT
		// What the build was configured with. Ahead of the search below because it is the
		// answer this application actually used, rather than whatever happens to sit above
		// the directory it was started from -- and an application outside the engine's
		// checkout has nothing above it to find.
		{
			std::error_code baked;
			if (std::filesystem::exists(RDA_ESBUILD_DEFAULT, baked)) return RDA_ESBUILD_DEFAULT;
		}
#endif

		std::error_code ec;
		// Beside the engine itself: a staged bin/ holds the tool, the shared library and
		// a standalone esbuild together, so a project with no node_modules anywhere --
		// a Python one, say -- still compiles. The bare binary, not npm's .cmd shim,
		// because there is no npm in that picture.
		for (const std::filesystem::path& base : { engineModuleDirectory(), executableDirectory() }) {
			if (base.empty()) continue;
			for (const char* name : { kEsbuildBinary, kEsbuildName }) {
				const std::filesystem::path candidate = base / name;
				if (std::filesystem::exists(candidate, ec)) return candidate.string();
			}
		}

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

	namespace {
		// esbuild on one file, with the flags that differ between the two callers.
		bool runEsbuild(const std::string& sourcePath, const std::string& esbuildPath,
		                const std::string& flags, std::string& js, std::string& error) {
			const std::string exe = esbuildPath.empty() ? findEsbuild() : esbuildPath;
			if (exe.empty()) {
				error = "esbuild not found. Run 'npm install' in the engine checkout or in "
				        "this project, set RDA_ESBUILD to a copy, or configure with "
				        "-DRDA_COMPILE_LAYOUTS=OFF to use the blueprints as they are.";
				return false;
			}
			const std::string command = "\"" + exe + "\" \"" + sourcePath + "\" " + flags;
			std::string diagnostics;
			if (!runCapturing(command, js, diagnostics)) {
				error = !diagnostics.empty() ? diagnostics
				                             : (js.empty() ? "esbuild failed" : js);
				return false;
			}
			if (!diagnostics.empty()) RDA_LOG_WARNING(sourcePath << ":\n" << diagnostics);
			return true;
		}

		// Base64, for the source map esbuild writes inline at the end of the bundle.
		std::string decodeBase64(std::string_view text) {
			auto value = [](char c) -> int {
				if (c >= 'A' && c <= 'Z') return c - 'A';
				if (c >= 'a' && c <= 'z') return c - 'a' + 26;
				if (c >= '0' && c <= '9') return c - '0' + 52;
				if (c == '+') return 62;
				if (c == '/') return 63;
				return -1;
			};
			std::string out;
			uint32_t bits = 0;
			int count = 0;
			for (const char c : text) {
				const int v = value(c);
				if (v < 0) continue;   // padding, line breaks
				bits = (bits << 6) | static_cast<uint32_t>(v);
				count += 6;
				if (count >= 8) {
					count -= 8;
					out.push_back(static_cast<char>((bits >> count) & 0xFF));
				}
			}
			return out;
		}

		// No sources-content: the map is for line numbers, and the files it would carry are
		// on disk beside the module anyway.
		constexpr const char* kInlineMap = " --sourcemap=inline --sources-content=false";

		// The map esbuild wrote inline at the end of `js`, taken off and decoded.
		void takeInlineMap(std::string& js, std::string& sourceMap) {
			constexpr std::string_view kMarker = "//# sourceMappingURL=data:application/json;base64,";
			const size_t at = js.rfind(kMarker);
			sourceMap.clear();
			if (at != std::string::npos) {
				size_t end = js.find('\n', at);
				if (end == std::string::npos) end = js.size();
				sourceMap = decodeBase64(std::string_view(js).substr(at + kMarker.size(), end - at - kMarker.size()));
				js.erase(at);
			}
		}
	}

	bool bundleLayout(const std::string& sourcePath, const std::string& esbuildPath,
	                  std::string& js, std::string& sourceMap, std::string& error) {
		if (!runEsbuild(sourcePath, esbuildPath,
		                std::string("--jsx-factory=h --jsx-fragment=Fragment --bundle --format=esm "
		                            "--target=es2020 --charset=utf8") + kInlineMap,
		                js, error))
			return false;
		takeInlineMap(js, sourceMap);
		return true;
	}

	bool transformModule(const std::string& sourcePath, const std::string& esbuildPath,
	                     bool withJsx, std::string& js, std::string& sourceMap, std::string& error) {
		const std::string exe = esbuildPath.empty() ? findEsbuild() : esbuildPath;
		if (exe.empty()) {
			error = "esbuild not found. Run 'npm install' in the engine checkout or in "
			        "this project, set RDA_ESBUILD to a copy, or configure with "
			        "-DRDA_COMPILE_LAYOUTS=OFF to use the blueprints as they are.";
			return false;
		}

		std::string command = "\"" + exe + "\" \"" + sourcePath + "\"";
		if (withJsx) command += " --jsx-factory=h --jsx-fragment=Fragment";
		// --bundle so a file may import another. Without it an import survives into the
		// JavaScript QuickJS evaluates, with nothing there to resolve it -- which is why
		// every helper used to be defined in the file that needed it.
		//
		// It resolves node_modules too. That is not encouraged and not supported: a
		// package written for a browser or for node will evaluate here and fail in ways
		// that have nothing to do with this engine. Import your own files.
		command += " --bundle --format=iife --global-name=__module --target=es2020 --charset=utf8";
		command += kInlineMap;

		std::string diagnostics;
		if (!runCapturing(command, js, diagnostics)) {
			error = !diagnostics.empty() ? diagnostics
			                             : (js.empty() ? "esbuild failed" : js);
			return false;
		}
		if (!diagnostics.empty()) {
			// It exited 0, so the module is good and compiling carries on. The warning is
			// still the author's business -- a key written twice is a line that does
			// nothing -- and this is the only place that ever sees the text.
			RDA_LOG_WARNING(sourcePath << ":\n" << diagnostics);
		}
		takeInlineMap(js, sourceMap);
		return true;
	}

	// What a layout gets on top of the colour helpers. Written in js/layout.js.
	//
	// signal() declares state the layout owns: a panel being open, which tab is selected.
	// It is collected while the module is evaluated and never reaches the running program
	// as a function -- what reaches it is a name, a type and an initial value.
	//
	// Scoped to the layout by default, so two files may both call something `open`. A
	// signal meant to be shared says so, and then it is the same signal C++ and every
	// other layout see.
	const char* layoutPrelude() {
		return reinterpret_cast<const char*>(kLayoutPreludeSource);
	}


	// What state.ts is evaluated with. See the header. Written in js/state.js.
	//
	// The evaluator works on the declaration objects themselves, in place: a computed value's
	// function closes over the module's own `state`, so the only way it can read starting
	// values is for that very object to hand them out. Called after the declarations have
	// been read, which is the last thing that needed them as they were written.
	const char* statePrelude() {
		return reinterpret_cast<const char*>(kStatePreludeSource);
	}

	// What a mesh is written with. Written in js/mesh.js.
	//
	// All of it runs at build time and none of it survives: what reaches the .rdam is
	// flat arrays of numbers. The functions here exist so that an author writes shapes
	// rather than vertex tables, and so that the C++ that reads the result never has to
	// walk a nested object.
	//
	// A vertex is eight numbers -- anchor x, anchor y, offset x, offset y, u, v, groups,
	// param -- because the alternative is an object per vertex and a mesh has hundreds.
	const char* meshPrelude() {
		return reinterpret_cast<const char*>(kMeshPreludeSource);
	}

	// Colour helpers, available to every theme and layout without importing anything.
	// Written in js/colour.js.
	//
	// Ambient globals rather than a module, for the same reason `h` and `state` are: the
	// transform step does not bundle, so an import would survive into the evaluated
	// JavaScript with nothing to resolve it. These run while the theme is compiled and
	// are gone afterwards -- what reaches the blueprint is the string they returned.
	//
	// Every one of them returns "#RRGGBB" or "#RRGGBBAA", which is what the theme reader
	// already parses, so nothing downstream had to learn about them.
	const char* colourPrelude() {
		return reinterpret_cast<const char*>(kColourPreludeSource);
	}
}
