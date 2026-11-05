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
}
