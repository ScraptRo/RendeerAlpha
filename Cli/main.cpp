// rda — the layout toolchain.
//
//   rda init   [<dir>] [--python|--node|--csharp|--cpp] [--force] [--no-build]
//                                      a new project: an interface, and a backend if asked
//   rda build  <project> [--python|--node|--csharp|--cpp <dir>] [--force]
//                                      everything a project's res/ needs, by convention
//   rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint
//   rda types  <out.d.ts> [theme.ts] [state.ts]  emit the TypeScript definitions
//   rda theme  <theme.ts> [out.rdth]   compile a theme
//   rda mesh   <mesh.ts>  [out.rdam]   compile a widget mesh
//   rda state  <state.ts>  <out.h>     generate the C++ for a state declaration
//   rda python|node|csharp <state.ts> <out>   the same, for another language
//   rda dump   <file.rdab>             print what a compiled layout contains
//   rda add screen <name> [<project>]  a layout and its route
//   rda elements [<name>] [--json]     the widget schema, readable
//   rda dev    [<project>]             build, run the backend, run it again on a state.ts change
//   rda doctor [<project>]             what was found, and what is missing
//   rda prototype <project>            open the interface with placeholder state
//   rda shot   <project>               the same with no window: writes a .png
//
// Options that apply to every command can be written anywhere on the line:
// --no-runtime-code, --strict, and --mesh <mesh.ts> (repeatable) on `theme` and `types`.
//
// Compilers, every one of them -- bar `prototype` and `shot`, which run what the compilers
// made so that it can be looked at, and which are not here: they need Vulkan and a window,
// and this program needs neither, so they are `rda-preview` (PreviewMain.cpp) and this
// hands them over (Forward.cpp). Building and running the engine itself is a
// script's job and lives in scripts/: shell work wearing a C++ coat. These are not, because they need the widget
// schema and a JavaScript engine to evaluate a layout once.
//
// `build` is the one a person runs. A C++ project has CMake to call the others one file
// at a time; a Python, Node or C# project has no build system that would, and should
// not need one to compile an interface. So this walks a project's res/ and does what the
// CMake rules would have: themes, the types, the layouts, and the state in whichever
// language was asked for -- skipping whatever is already newer than its inputs.
//
// `init` writes the files a project starts from and then runs `build` over them, so it needs
// what `build` needs and nothing more -- in particular no GPU, which is why it is here and
// not behind `rda-preview` with the two commands that open a window.
//
// One file per command -- CmdCompile.cpp, CmdState.cpp, CmdBuild.cpp -- and this one only
// chooses between them. What they share is in Common.h. `dev` is the one that starts
// programs and waits for them, which is Forward.cpp's business, so it is built into `rda`
// itself and not into the library the preview shares.
#include "Commands.h"
#include "Forward.h"
#include "PreviewUsage.h"

#include <cstdio>
#include <string>

namespace {

	struct Command {
		const char* name;
		// Null for a command that is run by `rda-preview`: the line is handed over whole.
		int       (*run)(const Cli::Args& args);
		// Positional arguments it cannot do without; a line with fewer is the usage text.
		size_t      required;
		// Its lines in the usage text, exactly as they are printed.
		const char* usage;
	};

	const Command kCommands[] = {
		// Nothing is required: no directory means this one.
		{ "init", Cli::cmdInit, 0,
			"  rda init   [<dir>] [--python] [--node] [--csharp] [--cpp] [--force] [--no-build]\n"
			"                                     start a project: res/ and, with a language, its\n"
			"                                     backend. Refuses to overwrite without --force\n" },
		{ "build", Cli::cmdBuild, 0,
			"  rda build  <project> [--python] [--node] [--csharp] [--cpp <dir>] [--force]\n"
			"             [--no-runtime-code] [--strict]\n"
			"                                     compile everything under <project>/res/, and write\n"
			"                                     the state module of whatever backend the folder\n"
			"                                     has (app.py, package.json, *.csproj); a flag\n"
			"                                     overrides that. --strict makes a misspelt element\n"
			"                                     or property an error instead of a warning\n" },
		{ "layout", Cli::cmdLayout, 1,
			"  rda layout <file.tsx> [out.rdab] [--strict]\n"
			"                                     compile a layout into a blueprint\n" },
		{ "add", Cli::cmdAdd, 3,
			"  rda add screen <name> [<project>] [--no-build]\n"
			"                                     a new layout, and the route to it in state.ts\n" },
		{ "elements", Cli::cmdElements, 0,
			"  rda elements [<name>] [--json]     the elements a layout can use; with a name, its\n"
			"                                     properties, their types, and its events\n" },
		{ "dev", Cli::cmdDev, 0,
			"  rda dev    [<project>] [--layout <name>] [--dry-run] [--python] [--node] [--csharp]\n"
			"                                     build, run the project's backend, and build and\n"
			"                                     run it again when res/state.ts changes; with no\n"
			"                                     backend, the prototype\n" },
		{ "doctor", Cli::cmdDoctor, 0,
			"  rda doctor [<project>]             what rda found -- esbuild, the GPU loader, python,\n"
			"                                     node, dotnet -- and, for a project, what it lacks\n" },
		{ "types", Cli::cmdTypes, 1,
			"  rda types  <out.d.ts> [theme] [state]  emit the TypeScript definitions\n" },
		{ "theme", Cli::cmdTheme, 1,
			"  rda theme  <theme.ts> [out.rdth]   compile a theme\n" },
		// --mesh sits with the command that makes what it names: it is the one option here
		// that is about a mesh, though it is read by `theme` and `types`.
		{ "mesh", Cli::cmdMesh, 1,
			"  rda mesh   <mesh.ts>  [out.rdam]   compile a widget mesh\n"
			"  --mesh <mesh.ts>                   on `theme` and `types`, repeatable: a mesh\n"
			"                                     whose parameters the schema includes\n" },
		{ "state", Cli::cmdState, 2,
			"  rda state  <state.ts>  <out.h>     generate the C++ for a state declaration\n" },
		{ "python", Cli::cmdPython, 2,
			"  rda python <state.ts>  <out.py>    generate the Python for a state declaration\n" },
		{ "node", Cli::cmdNode, 2,
			"  rda node   <state.ts>  <out.mjs>   generate the JavaScript for a state declaration\n" },
		{ "csharp", Cli::cmdCSharp, 2,
			"  rda csharp <state.ts>  <out.cs>    generate the C# for a state declaration\n" },
		{ "dump", Cli::cmdDump, 1,
			"  rda dump   <file.rdab>             print what a compiled layout contains\n" },
		{ "prototype", nullptr, 0, Cli::kPrototypeUsage },
		{ "shot", nullptr, 0, Cli::kShotUsage },
	};

	void usage() {
		std::printf("rda - the Rendeer layout toolchain\n\n");
		for (const Command& one : kCommands) std::printf("%s", one.usage);
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
	bool hasRequired(const Command& command, const Cli::Args& args) {
		for (size_t i = 1; i <= command.required; ++i) {
			if (Cli::argument(args, i).empty()) return false;
		}
		return true;
	}
}

int main(int argc, char** argv) {
	const Cli::Args line(argv + 1, argv + argc);
	Cli::Args args = line;
	Cli::takeGlobalOptions(args);

	if (args.empty()) { usage(); return 1; }

	for (const Command& command : kCommands) {
		if (args[0] != command.name || !hasRequired(command, args)) continue;
		// What is forwarded is the line as it was typed, the options that apply to every
		// command included: `rda-preview` takes them out itself, and a build it does first
		// needs --no-runtime-code as much as `rda build` does.
		return command.run ? command.run(args) : Cli::forwardToPreview(line);
	}

	usage();
	return 1;
}
