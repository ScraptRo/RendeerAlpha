// The entry points of the commands that compile, which is all main.cpp and the preview
// need to know of them. Each takes the command line with the command itself at [0] and
// returns the process's exit code. `prototype` and `shot` are declared in Preview.h.
#pragma once

#include "Common.h"

namespace Cli {

	// CmdCompile.cpp: one source file in, one compiled file out.
	int cmdLayout(const Args& args);
	int cmdTheme(const Args& args);
	int cmdMesh(const Args& args);
	int cmdTypes(const Args& args);
	int cmdDump(const Args& args);

	// CmdState.cpp: a state declaration, as the code a backend is written against.
	int cmdState(const Args& args);
	int cmdPython(const Args& args);
	int cmdNode(const Args& args);
	int cmdCSharp(const Args& args);

	// CmdBuild.cpp: a whole project by convention. The preview calls it too, so a picture is
	// never of a layout that was edited since it was last compiled.
	int cmdBuild(const Args& args);

	// CmdInit.cpp: the files a project starts from, then the build of them.
	int cmdInit(const Args& args);

	// CmdAdd.cpp: a screen, as a layout and the route that reaches it.
	int cmdAdd(const Args& args);

	// CmdElements.cpp: what the widget schema says, for a person to read.
	int cmdElements(const Args& args);

	// CmdDoctor.cpp: what was found, and what to do about what was not.
	int cmdDoctor(const Args& args);

	// CmdDev.cpp: build, run the backend, and run it again when state.ts changes. Only `rda`
	// has it: it starts programs, which is Forward.cpp's, and `rda-preview` has none to start.
	int cmdDev(const Args& args);
}
