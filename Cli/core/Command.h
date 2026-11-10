// What a command is, and how one reaches the core.
//
// A command is a file in commands/ and nothing else. It declares a Command at file scope, which
// is its name, its place in the usage text and what to run, and CMake compiles every file in that
// directory without being told about it. So adding one touches no shared file, which is the
// reason this exists: several people can each add a command at once and never meet.
//
// `rda --check-commands` is what says each one arrived. README.md has the rest.
#pragma once

#include "Common.h"

namespace Cli {

	// Where a command is listed in the usage text.
	enum class Section { Start, Build, Run, Learn };

	class Command {
	public:
		using Run = int (*)(const Args& args);

		// `file` is __FILE__, which is how --check-commands knows that every file in commands/
		// registered something. `required` is how many positional arguments it cannot do
		// without; a line with fewer gets the usage text. `usage` is its lines in that text,
		// exactly as printed, or nullptr for an alias, which works and is not listed. `run`
		// takes the line with the command itself at [0] and returns the exit code.
		Command(const char* file, const char* name, Section section, size_t required,
		        const char* usage, Run run);

		const char* file;
		const char* name;
		Section     section;
		size_t      required;
		const char* usage;
		Run         run;
	};

	// Another command, as if it had been typed: `args[0]` is its name. This is how one command
	// uses another, so that neither has to know what file the other is in.
	int runCommand(const Args& args);

	// The line as it was typed, the options that apply to every command included, for a command
	// that hands it on whole to another program.
	const Args& commandLine();

	// The whole of `rda`, from the line to the exit code.
	int runLine(const Args& line);
}
