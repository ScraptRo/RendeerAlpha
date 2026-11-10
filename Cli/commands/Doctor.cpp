// `rda doctor`: what the tool found, so that a person can see what is wrong.
//
// Most of what goes wrong with a first project is not in the project: it is an esbuild that
// was not where the tool looked, a python that cannot import the package, a machine with no
// Vulkan loader. Each of those has a message of its own somewhere, at the moment it bites,
// and none of them says what else is in the same state. This asks every one of the questions
// at once and answers each in a line that is either ok or says what to do.
//
// The tool links no GPU code and this keeps it so: the Vulkan loader is looked for by opening
// the library at run time and closing it again, never linked against.
#include "Command.h"

#include <Core/FileStamp.h>
#include <Core/Location.h>
#include <Layout/ModuleTransform.h>
#include <Layout/ThemeCompiler.h>

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

#ifndef RDA_VERSION_STRING
	#define RDA_VERSION_STRING "unknown"
#endif

namespace Cli {

	namespace {

		// ---- what is printed ----------------------------------------------------------------

		struct Report {
			int  problems = 0;
			// Something the project or the build cannot do without. Problems that are not this
			// are still said, but do not fail the command.
			bool fatal = false;

			void ok(const std::string& what, const std::string& detail) {
				std::printf("  ok       %-16s %s\n", what.c_str(), detail.c_str());
			}
			void problem(const std::string& what, const std::string& detail, const std::string& fix,
			             bool needed) {
				++problems;
				fatal = fatal || needed;
				std::printf("  PROBLEM  %-16s %s\n", what.c_str(), detail.c_str());
				std::printf("           %-16s -> %s\n", "", fix.c_str());
			}
		};

		// ---- running something to ask it a question -----------------------------------------

		// The first line of what `command` prints, both streams; false if it could not be run
		// or did not exit 0. A shell is how a program on the PATH is found, here and in the
		// compiler's esbuild call alike.
		bool firstLineOf(const std::string& command, std::string& line) {
			line.clear();
			const std::string full = command + " 2>&1";
#if defined(_WIN32)
			// cmd.exe strips the outermost pair of quotes, so the line is wrapped in one more.
			FILE* pipe = _popen(("\"" + full + "\"").c_str(), "r");
#else
			FILE* pipe = popen(full.c_str(), "r");
#endif
			if (!pipe) return false;
			char buffer[1024];
			std::string text;
			while (std::fgets(buffer, sizeof(buffer), pipe)) text += buffer;
#if defined(_WIN32)
			const bool ran = _pclose(pipe) == 0;
#else
			const bool ran = pclose(pipe) == 0;
#endif
			const size_t end = text.find_first_of("\r\n");
			line = text.substr(0, end);
			return ran && !line.empty();
		}

		// ---- the Vulkan loader --------------------------------------------------------------

		// The name of the library, and whether it opens. Opened and let go: finding out
		// whether a window could be made is `prototype`'s business, and this only asks whether
		// the loader is there for it to ask.
		bool vulkanLoader(std::string& name) {
#if defined(_WIN32)
			name = "vulkan-1.dll";
			if (HMODULE library = LoadLibraryA(name.c_str())) {
				const bool entry = GetProcAddress(library, "vkGetInstanceProcAddr") != nullptr;
				FreeLibrary(library);
				return entry;
			}
			return false;
#else
			for (const char* candidate : { "libvulkan.so.1", "libvulkan.1.dylib", "libvulkan.dylib" }) {
				if (void* library = dlopen(candidate, RTLD_LAZY | RTLD_LOCAL)) {
					const bool entry = dlsym(library, "vkGetInstanceProcAddr") != nullptr;
					dlclose(library);
					name = candidate;
					return entry;
				}
			}
			name = "libvulkan.so.1";
			return false;
#endif
		}

		// ---- the tool and what it needs to run ----------------------------------------------

		void checkTool(Report& report) {
			const fs::path self = RDA::executablePath();
			report.ok("rda", shown(self) + "  (engine " RDA_VERSION_STRING ")");

			const std::string esbuild = RDA::Layout::findEsbuild();
			if (esbuild.empty()) {
				report.problem("esbuild", "not found",
				               "set RDA_ESBUILD to its path, run `npm install` where rda is, or put it "
				               "beside rda; nothing can be compiled without it", true);
			} else {
				std::string version;
				if (firstLineOf("\"" + esbuild + "\" --version", version)) {
					report.ok("esbuild", esbuild + "  (" + version + ")");
				} else {
					report.problem("esbuild", esbuild + " would not run",
					               "it is the wrong file for this system, or not executable; point "
					               "RDA_ESBUILD at one that is", true);
				}
			}

#if defined(_WIN32)
			const fs::path preview = RDA::executableDirectory() / "rda-preview.exe";
#else
			const fs::path preview = RDA::executableDirectory() / "rda-preview";
#endif
			std::error_code ec;
			if (fs::is_regular_file(preview, ec)) {
				report.ok("rda-preview", shown(preview));
			} else {
				report.problem("rda-preview", "not beside rda (looked for " + shown(preview) + ")",
				               "`prototype` and `shot` need it; build the rda-preview target and stage "
				               "bin/ with the rest", false);
			}

			std::string loader;
			if (vulkanLoader(loader)) {
				report.ok("Vulkan loader", loader);
			} else {
				report.problem("Vulkan loader", "no " + loader + " could be opened",
				               "install the GPU driver or the Vulkan runtime; `prototype` and any backend "
				               "that opens a window need it, building does not", false);
			}
		}

		// What a backend's language needs, said as a line each. `needed` is whether this project
		// has that backend: then a missing piece is a failure, otherwise it is only information.
		void checkRuntimes(Report& report, const Languages& project, bool inProject) {
			const auto runtime = [&](const char* what, const std::vector<std::string>& names,
			                         const char* flag, bool needed, std::string& foundName) {
				for (const std::string& name : names) {
					std::string version;
					if (firstLineOf(name + " " + flag, version)) {
						foundName = name;
						report.ok(what, version);
						return true;
					}
				}
				if (needed) {
					report.problem(what, "not found", std::string("this project has a ") + what +
					               " backend and cannot run without it; install it and put it on the PATH", true);
				} else {
					report.ok(what, std::string("not installed (only a ") + what + " backend needs it)");
				}
				return false;
			};

			std::string python, node, dotnet;
#if defined(_WIN32)
			const std::vector<std::string> pythons = { "python", "python3" };
#else
			const std::vector<std::string> pythons = { "python3", "python" };
#endif
			const bool pythonNeeded = inProject && !project.python.empty();
			if (runtime("python", pythons, "--version", pythonNeeded, python)) {
				std::string where;
				if (firstLineOf(python + " -c \"import sys;print(sys.executable)\"", where)) {
					report.ok("python is", where);
				}
				// Where `import rda` lands for that interpreter, which is the question people
				// have when a Python backend says it cannot find the engine.
				std::string package;
				if (firstLineOf(python + " -c \"import rda;print(rda.__file__)\"", package)) {
					report.ok("import rda", package);
				} else {
					report.problem("import rda", "that python cannot import the rda package",
					               "install it (pip install bindings/python from the engine checkout) or "
					               "put that folder on PYTHONPATH", pythonNeeded);
				}
			}
			runtime("node", { "node" }, "--version", inProject && !project.node.empty(), node);
			runtime("dotnet", { "dotnet" }, "--version", inProject && !project.csharp.empty(), dotnet);
		}

		// ---- a project ----------------------------------------------------------------------

		std::string count(size_t n, const char* noun) {
			return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
		}

		// Outputs that are not there, and outputs that are older than what they come from, by the
		// rule the build uses -- the same stamps, and the tool itself counting as an input.
		void checkBuilt(Report& report, const fs::path& project, const Languages& found,
		                const std::vector<fs::path>& themes, const std::vector<fs::path>& layouts) {
			const int64_t tool = RDA::fileStamp(RDA::executablePath().string());
			std::vector<std::string> missing, old;
			const auto consider = [&](const fs::path& output, const std::vector<fs::path>& inputs) {
				if (RDA::fileStamp(output.string()) == 0) missing.push_back(shown(output));
				else if (stale(output, inputs, tool)) old.push_back(shown(output));
			};

			const std::set<fs::path> themeParts = importedByAnother(themes);
			const std::set<fs::path> layoutParts = importedByAnother(layouts);
			for (const fs::path& theme : themes) {
				if (!isImported(themeParts, theme)) consider(RDA::Layout::themePathFor(theme.string()), { theme });
			}
			for (const fs::path& layout : layouts) {
				if (isImported(layoutParts, layout)) continue;
				std::vector<fs::path> inputs;
				for (const std::string& one : RDA::Layout::importedFiles(layout.string())) inputs.emplace_back(one);
				if (inputs.empty()) inputs.push_back(layout);
				consider(RDA::Layout::blueprintPathFor(layout.string()), inputs);
			}

			const fs::path state = project / "res" / "state.ts";
			std::error_code ec;
			if (fs::is_regular_file(state, ec)) {
				std::vector<fs::path> inputs;
				for (const std::string& one : RDA::Layout::importedFiles(state.string())) inputs.emplace_back(one);
				if (inputs.empty()) inputs.push_back(state);
				if (!themes.empty() || !layouts.empty()) consider(project / "res" / "layouts" / "rda.d.ts", inputs);
				if (!found.python.empty()) consider(project / "state.py", inputs);
				if (!found.node.empty())   consider(project / "state.mjs", inputs);
				if (!found.csharp.empty()) consider(project / "state.cs", inputs);
			}

			if (missing.empty() && old.empty()) {
				report.ok("build", "everything `rda build` writes is there and current");
				return;
			}
			std::string detail;
			if (!missing.empty()) detail += count(missing.size(), "file") + " not built yet (" + missing.front() +
			                                (missing.size() > 1 ? ", ..." : "") + ")";
			if (!old.empty()) detail += std::string(detail.empty() ? "" : "; ") + count(old.size(), "file") +
			                            " older than their source or than rda (" + old.front() +
			                            (old.size() > 1 ? ", ..." : "") + ")";
			report.problem("build", detail, "run `rda build " + shown(project) + "`", !missing.empty());
		}

		void checkProject(Report& report, const fs::path& project) {
			std::printf("\nproject %s\n", shown(project).c_str());
			std::error_code ec;
			const fs::path res = project / "res";
			if (!fs::is_directory(project, ec)) {
				report.problem("project", "no such folder", "check the path; `rda init <dir>` starts a project", true);
				return;
			}
			if (!fs::is_directory(res, ec)) {
				report.problem("res/", "no res/ directory",
				               "a project keeps its declaration, theme and layouts there; `rda init` writes them", true);
				return;
			}
			report.ok("res/", shown(res));

			if (fs::is_regular_file(res / "state.ts", ec)) report.ok("state.ts", shown(res / "state.ts"));
			else report.problem("state.ts", "missing",
			                    "the declaration the interface and the backend agree on; `rda init` writes one", true);

			const std::vector<fs::path> themes = filesIn(res / "themes", ".ts");
			const std::vector<fs::path> layouts = filesIn(res / "layouts", ".tsx");
			if (!themes.empty()) report.ok("themes", count(themes.size(), "theme") + " in res/themes/");
			else report.problem("themes", "none in res/themes/", "a project needs a theme to be drawn with; `rda init` writes res/themes/app.ts", true);
			if (!layouts.empty()) report.ok("layouts", count(layouts.size(), "file") + " in res/layouts/");
			else report.problem("layouts", "none in res/layouts/", "an interface is a layout; `rda init` writes res/layouts/main.tsx", true);

			const Languages found = detectLanguages(project);
			if (found.any()) report.ok("backend", found.described());
			else report.ok("backend", "none found (no app.py, package.json, *.csproj or CMakeLists.txt): "
			                          "the interface alone, which `rda prototype` shows");

			checkBuilt(report, project, found, themes, layouts);
		}
	}

	static int cmdDoctor(const Args& args) {
		fs::path project;
		for (size_t i = 1; i < args.size(); ++i) {
			if (!args[i].empty() && args[i][0] == '-') {
				std::printf("rda: unknown option %s\n", args[i].c_str());
				return 1;
			}
			project = args[i];
		}

		Report report;
		std::printf("rda doctor\n");
		checkTool(report);

		// What the project's backend needs is only a failure when the project has that backend,
		// so the runtimes are checked knowing which it is.
		Languages found;
		if (!project.empty()) found = detectLanguages(project);
		checkRuntimes(report, found, !project.empty());

		if (!project.empty()) checkProject(report, project);

		std::printf("\n");
		if (report.problems == 0) {
			std::printf("rda: nothing wrong found\n");
			return 0;
		}
		std::printf("rda: %s found%s\n", count(static_cast<size_t>(report.problems), "problem").c_str(),
		            report.fatal ? ", and at least one is something that is needed" : "");
		return report.fatal ? 1 : 0;
	}

	namespace {
		const Command kDoctor{ __FILE__, "doctor", Section::Learn, 0,
			"  rda doctor [<project>]             what rda found -- esbuild, the GPU loader, python,\n"
			"                                     node, dotnet -- and, for a project, what it lacks\n",
			cmdDoctor };
	}
}
