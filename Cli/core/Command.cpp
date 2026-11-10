#include "Command.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// Every file in commands/, as a comma separated list. CMake writes it from the same glob that
// compiles them, so it is what --check-commands holds the registered commands against.
#ifndef RDA_CLI_COMMAND_FILES
	#define RDA_CLI_COMMAND_FILES ""
#endif

namespace Cli {

	namespace {

		// Reached through a function, because a Command in another file may be constructed
		// before anything at file scope here is.
		std::vector<const Command*>& registered() {
			static std::vector<const Command*> all;
			return all;
		}

		Args gLine;

		struct SectionTitle {
			Section     section;
			const char* title;
		};
		constexpr SectionTitle kSections[] = {
			{ Section::Start, "Start" },
			{ Section::Build, "Build" },
			{ Section::Run,   "Run" },
			{ Section::Learn, "Find out" },
		};

		// A file's name, from a __FILE__ that may be a full path in either direction.
		std::string baseName(const char* path) {
			const std::string text = path;
			const size_t slash = text.find_last_of("/\\");
			return slash == std::string::npos ? text : text.substr(slash + 1);
		}

		const Command* find(const std::string& name) {
			for (const Command* one : registered()) {
				if (name == one->name) return one;
			}
			return nullptr;
		}

		void usage() {
			std::printf("rda - the Rendeer layout toolchain\n");
			for (const SectionTitle& heading : kSections) {
				// By name, because the order commands register in is the order their files
				// happened to be linked.
				std::vector<const Command*> rows;
				for (const Command* one : registered()) {
					if (one->section == heading.section && one->usage) rows.push_back(one);
				}
				std::sort(rows.begin(), rows.end(), [](const Command* a, const Command* b) {
					return std::strcmp(a->name, b->name) < 0;
				});
				std::printf("\n%s\n", heading.title);
				for (const Command* one : rows) std::printf("%s", one->usage);
			}
			std::printf(
				"\n"
				"A layout is TypeScript with JSX in it. Compiling turns it into a flat file of\n"
				"nodes and strings that an application reads without a parser of any kind.\n\n"
				"`build` expects the layout every project has: res/state.ts, res/themes/*.ts and\n"
				"res/layouts/*.tsx. It writes each theme's .rdth and each layout's .rdab beside\n"
				"its source, res/layouts/rda.d.ts from the first theme and the state, and the\n"
				"state module -- state.py, state.mjs or state.cs -- into the project directory,\n"
				"for each backend the folder has, or the one a flag names. A C++ project gets its\n"
				"state header from CMake, so there is none to write for it.\n");
		}

		// Whether the line names enough for the command to start: each positional argument it
		// needs is there and not empty.
		bool hasRequired(const Command& command, const Args& args) {
			for (size_t i = 1; i <= command.required; ++i) {
				if (argument(args, i).empty()) return false;
			}
			return true;
		}

		// Whether every command reached the core. A file that registers nothing still compiles
		// and links, and its command is simply not there, so this asks the question that only
		// shows up otherwise when somebody types the name.
		int checkCommands() {
			int problems = 0;
			const auto problem = [&problems](const std::string& what) {
				std::printf("rda: %s\n", what.c_str());
				++problems;
			};

			std::set<std::string> names;
			std::set<std::string> files;
			for (const Command* one : registered()) {
				if (!names.insert(one->name).second) {
					problem(std::string("two commands are called `") + one->name + "`; one is in " + baseName(one->file));
				}
				if (!one->run) problem(std::string("`") + one->name + "` has nothing to run");
				// An alias has no usage of its own, and every other command says how it is typed.
				if (one->usage) {
					const std::string opening = std::string("  rda ") + one->name;
					if (std::strncmp(one->usage, opening.c_str(), opening.size()) != 0) {
						problem(std::string("the usage of `") + one->name + "` does not start with `rda " + one->name + "`");
					}
					const size_t length = std::strlen(one->usage);
					if (length == 0 || one->usage[length - 1] != '\n') {
						problem(std::string("the usage of `") + one->name + "` does not end its last line");
					}
				}
				files.insert(baseName(one->file));
			}

			std::string expected = RDA_CLI_COMMAND_FILES;
			size_t fileCount = 0;
			for (size_t at = 0; at < expected.size();) {
				size_t comma = expected.find(',', at);
				if (comma == std::string::npos) comma = expected.size();
				const std::string name = expected.substr(at, comma - at);
				at = comma + 1;
				++fileCount;
				if (files.count(name) == 0) problem("commands/" + name + " registers no command");
			}

			if (problems > 0) return 1;
			std::printf("rda: %zu commands from %zu files, all connected\n", registered().size(), fileCount);
			return 0;
		}
	}

	Command::Command(const char* file_, const char* name_, Section section_, size_t required_,
	                 const char* usage_, Run run_)
		: file(file_), name(name_), section(section_), required(required_), usage(usage_), run(run_) {
		registered().push_back(this);
	}

	int runCommand(const Args& args) {
		const Command* command = args.empty() ? nullptr : find(args[0]);
		if (!command) {
			std::printf("rda: no command called `%s`\n", args.empty() ? "" : args[0].c_str());
			return 1;
		}
		return command->run(args);
	}

	const Args& commandLine() { return gLine; }

	int runLine(const Args& line) {
		gLine = line;
		Args args = line;
		takeGlobalOptions(args);

		if (args.empty()) { usage(); return 1; }
		if (args[0] == "--check-commands") return checkCommands();

		const Command* command = find(args[0]);
		if (!command || !hasRequired(*command, args)) { usage(); return 1; }
		return command->run(args);
	}
}
