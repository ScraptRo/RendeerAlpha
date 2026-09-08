#include "ModuleTransform.h"
#include <Core/Location.h>
#include <vector>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <fstream>

#include <array>
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

	bool transformModule(const std::string& sourcePath, const std::string& esbuildPath,
	                     bool withJsx, std::string& js, std::string& error) {
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
		command += " --bundle --format=iife --global-name=__module --target=es2020";

		if (!runCapturing(command, js)) {
			error = js.empty() ? "esbuild failed" : js;
			return false;
		}
		return true;
	}

	// Colour helpers, available to every theme and layout without importing anything.
	//
	// Ambient globals rather than a module, for the same reason `h` and `state` are: the
	// transform step does not bundle, so an import would survive into the evaluated
	// JavaScript with nothing to resolve it. These run while the theme is compiled and
	// are gone afterwards -- what reaches the blueprint is the string they returned.
	//
	// Every one of them returns "#RRGGBB" or "#RRGGBBAA", which is what the theme reader
	// already parses, so nothing downstream had to learn about them.
	// What a layout gets on top of the colour helpers.
	//
	// signal() declares state the layout owns: a panel being open, which tab is selected.
	// It is collected while the module is evaluated and never reaches the running program
	// as a function -- what reaches it is a name, a type and an initial value.
	//
	// Scoped to the layout by default, so two files may both call something `open`. A
	// signal meant to be shared says so, and then it is the same signal C++ and every
	// other layout see.
	const char* layoutPrelude() {
		return R"JS(
var __rdaSignals = [];
function signal(name, value, options) {
  if (typeof name !== "string" || name.length === 0) {
    throw new Error("signal() needs a name: signal(\"open\", false)");
  }
  __rdaSignals.push({ name: name, value: value, global: !!(options && options.global) });
}
)JS";
	}

	const char* colourPrelude() {
		return R"JS(
function __rdaByte(n) {
  n = Math.round(n);
  return n < 0 ? 0 : (n > 255 ? 255 : n);
}
function __rdaHex(n) {
  return __rdaByte(n).toString(16).padStart(2, "0").toUpperCase();
}
function __rdaParse(colour) {
  if (typeof colour !== "string") throw new Error("expected a colour string, got " + typeof colour);
  var s = colour.trim();
  if (s.charAt(0) === "#") s = s.slice(1);
  if (s.length === 3) s = s.charAt(0) + s.charAt(0) + s.charAt(1) + s.charAt(1) + s.charAt(2) + s.charAt(2);
  if (s.length === 6) s = s + "FF";
  if (s.length !== 8 || /[^0-9a-fA-F]/.test(s)) {
    throw new Error("'" + colour + "' is not a colour: expected #RGB, #RRGGBB or #RRGGBBAA");
  }
  return [parseInt(s.slice(0, 2), 16), parseInt(s.slice(2, 4), 16),
          parseInt(s.slice(4, 6), 16), parseInt(s.slice(6, 8), 16)];
}
function rgb(r, g, b) {
  return "#" + __rdaHex(r) + __rdaHex(g) + __rdaHex(b);
}
function rgba(r, g, b, a) {
  return rgb(r, g, b) + __rdaHex(a * 255);
}
function hsl(h, s, l) {
  h = ((h % 360) + 360) % 360 / 360;
  var q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  var p = 2 * l - q;
  function channel(t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  }
  if (s === 0) return rgb(l * 255, l * 255, l * 255);
  return rgb(channel(h + 1 / 3) * 255, channel(h) * 255, channel(h - 1 / 3) * 255);
}
function fade(colour, alpha) {
  var c = __rdaParse(colour);
  return "#" + __rdaHex(c[0]) + __rdaHex(c[1]) + __rdaHex(c[2]) + __rdaHex(alpha * 255);
}
function mix(a, b, t) {
  var x = __rdaParse(a), y = __rdaParse(b);
  var k = t < 0 ? 0 : (t > 1 ? 1 : t);
  return "#" + __rdaHex(x[0] + (y[0] - x[0]) * k)
             + __rdaHex(x[1] + (y[1] - x[1]) * k)
             + __rdaHex(x[2] + (y[2] - x[2]) * k)
             + __rdaHex(x[3] + (y[3] - x[3]) * k);
}
function lighten(colour, amount) { return mix(colour, "#FFFFFFFF", amount); }
function darken(colour, amount)  { return mix(colour, "#000000FF", amount); }
function alphaOf(colour) { return __rdaParse(colour)[3] / 255; }
)JS";
	}

	// Every file an entry pulls in, transitively, itself included.
	//
	// Read out of the source rather than asked of esbuild: --metafile refuses to run
	// without an output path, so getting the list from the bundler would mean writing the
	// bundle to a temporary file and parsing JSON to learn something the import lines
	// already say.
	//
	// Static relative imports only. A bare specifier names a package, which is not
	// supported, and a dynamic import is not something the compiler accepts either.
	std::vector<std::string> importedFiles(const std::string& entry) {
		namespace fs = std::filesystem;
		std::vector<std::string> found;
		std::vector<std::string> pending{ entry };
		std::unordered_set<std::string> seen;

		auto resolve = [](const fs::path& from, const std::string& specifier) -> std::string {
			const fs::path base = from.parent_path() / specifier;
			std::error_code ec;
			// The extension is usually left off, the way TypeScript is written.
			for (const char* suffix : { "", ".ts", ".tsx", "/index.ts", "/index.tsx" }) {
				const fs::path candidate = base.string() + suffix;
				if (fs::is_regular_file(candidate, ec)) return candidate.lexically_normal().string();
			}
			return {};
		};

		while (!pending.empty()) {
			const std::string path = pending.back();
			pending.pop_back();
			if (path.empty() || !seen.insert(path).second) continue;
			found.push_back(path);

			std::ifstream file(path, std::ios::binary);
			if (!file) continue;
			const std::string text((std::istreambuf_iterator<char>(file)),
			                       std::istreambuf_iterator<char>());

			// Walk the keywords rather than every quoted string, so a path-shaped literal
			// in ordinary code is not mistaken for a dependency.
			for (const char* keyword : { "from", "import" }) {
				const size_t length = std::strlen(keyword);
				size_t at = 0;
				while ((at = text.find(keyword, at)) != std::string::npos) {
					size_t cursor = at + length;
					at = cursor;
					// A keyword has to stand alone: `fromage` is not `from`.
					if (cursor < text.size() && (std::isalnum(static_cast<unsigned char>(text[cursor])) ||
					                             text[cursor] == '_')) continue;
					while (cursor < text.size() &&
					       std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
					if (cursor >= text.size()) break;
					const char quote = text[cursor];
					if (quote != '"' && quote != '\'') continue;
					const size_t start = ++cursor;
					while (cursor < text.size() && text[cursor] != quote) ++cursor;
					if (cursor >= text.size()) break;

					const std::string specifier = text.substr(start, cursor - start);
					if (specifier.rfind("./", 0) != 0 && specifier.rfind("../", 0) != 0) continue;
					const std::string resolved = resolve(fs::path(path), specifier);
					if (!resolved.empty()) pending.push_back(resolved);
				}
			}
		}
		return found;
	}
}
