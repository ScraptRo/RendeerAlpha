// `rda dev`: the edit loop as one command.
//
// Building, starting the backend, and starting it again after state.ts changed were three
// terminals and a habit. This is the habit: build, run what the project has for a backend,
// and watch the one file a running engine cannot pick up by itself.
//
// A Debug engine reloads layouts and themes the moment they are saved, so there is nothing
// to do about those. state.ts is different: a new field is a new generated module, and the
// backend imports that module, so the backend has to be started again to see it. That is the
// only thing watched, and the only thing that restarts anything.
//
// Without a backend there is still something to look at, so it falls back to the prototype,
// which is the interface with placeholder state and reloads state.ts itself.
//
// Lives in `rda`, not in the library the other commands share: it needs the child-process
// code that is beside `main`, and `rda-preview` has no use for either.
#include "Commands.h"
#include "Forward.h"

#include <Core/FileStamp.h>
#include <Layout/ModuleTransform.h>

#include <chrono>
#include <cstdio>
#include <thread>

namespace Cli {

	namespace {

		struct DevOptions {
			fs::path    project = ".";
			std::string layout;           // --layout, for the prototype
			bool        dryRun = false;
			// Which backend to run when the project has more than one: a flag says so, as it
			// does for `build`.
			bool        python = false, node = false, csharp = false;
		};

		bool parseDevOptions(const Args& args, DevOptions& options) {
			for (size_t i = 1; i < args.size(); ++i) {
				const std::string& arg = args[i];
				if      (arg == "--dry-run") options.dryRun = true;
				else if (arg == "--python")  options.python = true;
				else if (arg == "--node")    options.node = true;
				else if (arg == "--csharp")  options.csharp = true;
				else if (arg == "--layout" && i + 1 < args.size()) options.layout = args[++i];
				else if (!arg.empty() && arg[0] == '-') {
					std::printf("rda: unknown option %s\n", arg.c_str());
					return false;
				}
				else options.project = arg;
			}
			return true;
		}

		std::string commandText(const std::vector<std::string>& command) {
			std::string out;
			for (const std::string& word : command) {
				const bool quote = word.find(' ') != std::string::npos;
				out += (out.empty() ? "" : " ") + (quote ? "\"" + word + "\"" : word);
			}
			return out;
		}

		// What starts this project's backend, or empty with the reason printed when it cannot
		// be started from here.
		std::vector<std::string> backendCommand(const DevOptions& options, const Languages& found) {
			const bool chosen = options.python || options.node || options.csharp;
			const bool wantPython = chosen ? options.python : !found.python.empty();
			const bool wantNode   = chosen ? options.node   : !found.node.empty();
			const bool wantCSharp = chosen ? options.csharp : !found.csharp.empty();
			const int  count = int(wantPython) + int(wantNode) + int(wantCSharp);

			std::error_code ec;
			if (count > 1) {
				std::printf("rda: %s has more than one backend (%s) -- say which to run with "
				            "--python, --node or --csharp\n", shown(options.project).c_str(),
				            found.described().c_str());
				return {};
			}
			if (wantPython) {
				if (!fs::is_regular_file(options.project / "app.py", ec)) {
					std::printf("rda: a Python backend is started as `python app.py`, and there is no "
					            "app.py in %s\n", shown(options.project).c_str());
					return {};
				}
#if defined(_WIN32)
				return { "python", "app.py" };
#else
				return { "python3", "app.py" };
#endif
			}
			if (wantNode) {
				if (!fs::is_regular_file(options.project / "app.mjs", ec)) {
					std::printf("rda: a Node backend is started as `node app.mjs`, and there is no "
					            "app.mjs in %s\n", shown(options.project).c_str());
					return {};
				}
				return { "node", "app.mjs" };
			}
			if (wantCSharp) return { "dotnet", "run" };

			if (!found.cpp.empty()) {
				std::printf("rda: a C++ project is its own program, and `dev` cannot start it: build it with "
				            "CMake (cmake -S . -B build && cmake --build build) and run what that makes. "
				            "`rda prototype %s` shows the interface meanwhile.\n",
				            shown(options.project).c_str());
			}
			return {};
		}

		int prototype(const DevOptions& options, bool dryRun) {
			Args line = { "prototype", options.project.string() };
			if (!options.layout.empty()) { line.push_back("--layout"); line.push_back(options.layout); }
			if (dryRun) {
				std::printf("rda: would run: rda %s\n", commandText(line).c_str());
				return 0;
			}
			std::printf("rda: no backend to run, so the prototype: the interface with placeholder state, "
			            "reloading as you save\n");
			std::fflush(stdout);
			return forwardToPreview(line);
		}

		// state.ts and what it imports, the files a backend started earlier is out of date with.
		std::vector<std::string> watchedFiles(const fs::path& project) {
			return RDA::Layout::importedFiles((project / "res" / "state.ts").string());
		}

		int runDev(const DevOptions& options) {
			std::error_code ec;
			if (!fs::is_directory(options.project / "res", ec)) {
				std::printf("rda: no res/ directory in %s -- `rda init` starts a project\n",
				            shown(options.project).c_str());
				return 1;
			}

			const Languages found = detectLanguages(options.project);
			const bool backendFlag = options.python || options.node || options.csharp;
			const std::vector<std::string> command = backendCommand(options, found);

			if (options.dryRun) {
				std::printf("rda: would build %s\n", shown(options.project).c_str());
				if (command.empty()) {
					if (!found.any() && !backendFlag) return prototype(options, true);
					return 1;
				}
				std::printf("rda: would run: %s   (in %s)\n", commandText(command).c_str(),
				            shown(options.project).c_str());
				std::printf("rda: would watch res/state.ts and what it imports, and build and run it "
				            "again when one changes. Layouts and themes reload by themselves in a "
				            "Debug engine.\n");
				return 0;
			}

			// A line is built first. The backend's first act is to read the state module, which
			// is what this writes.
			Args buildLine = { "build", options.project.string() };
			if (options.python) buildLine.push_back("--python");
			if (options.node)   buildLine.push_back("--node");
			if (options.csharp) buildLine.push_back("--csharp");
			if (cmdBuild(buildLine) != 0) return 1;

			if (command.empty()) {
				// A project that has a backend we cannot start has been told why; one that has
				// none gets the prototype.
				if (found.any() || backendFlag) return 1;
				return prototype(options, false);
			}
			if (!options.layout.empty()) {
				std::printf("rda: --layout is for the prototype; the backend decides which screen it opens\n");
			}

			watchForInterrupt();
			RDA::FileWatch watch;
			watch.watch(watchedFiles(options.project));

			Child backend;
			std::printf("\nrda: running %s -- Ctrl+C stops it\n", commandText(command).c_str());
			std::fflush(stdout);
			if (!backend.start(command, options.project)) return 1;

			using Clock = std::chrono::steady_clock;
			Clock::time_point lastCheck = Clock::now();
			for (;;) {
				if (interrupted()) {
					std::printf("\nrda: stopping the backend\n");
					backend.stop();
					return 130;
				}
				if (!backend.running()) {
					// Its own code, so a script around `rda dev` sees what the backend said.
					const int code = backend.exitCode();
					std::printf("rda: the backend ended with code %d\n", code);
					return code;
				}

				if (Clock::now() - lastCheck > std::chrono::milliseconds(250)) {
					lastCheck = Clock::now();
					if (watch.changed()) {
						std::printf("\nrda: state.ts changed -- building again\n");
						std::fflush(stdout);
						// Watched afresh: an import added since is one to watch from now on.
						watch.watch(watchedFiles(options.project));
						if (cmdBuild(buildLine) == 0) {
							backend.stop();
							std::printf("rda: running %s again\n", commandText(command).c_str());
							std::fflush(stdout);
							if (!backend.start(command, options.project)) return 1;
						} else {
							// The old one keeps running: a typo in state.ts is not a reason to
							// lose the window it was edited beside.
							std::printf("rda: the build failed, so the backend is left as it was; save "
							            "again when it is fixed\n");
							// Now, and not when something next flushes: through a pipe this line and
							// the error above it would wait for the next save, which they are the
							// reason for.
							std::fflush(stdout);
						}
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}
	}

	int cmdDev(const Args& args) {
		DevOptions options;
		if (!parseDevOptions(args, options)) return 1;
		return runDev(options);
	}
}
