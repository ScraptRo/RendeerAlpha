// `rda prototype` and `rda shot`, which are run by another program.
//
// They open a window, so they need Vulkan and a display, and the rest of `rda` needs neither:
// compiling a layout in a build step on a machine with no GPU is the ordinary case. They live
// in `rda-preview`, and `rda` hands them over so that nobody has to type the other name.
#pragma once

#include "Common.h"

namespace Cli {

	// Runs `rda-preview` from the directory `rda` itself is in, with `line` as its command
	// line -- the whole of it, the options that apply to every command included, exactly as
	// they were written. Its standard streams are this process's, and its exit code is
	// returned. When it is not there, says so and returns 1.
	//
	// On Windows the command line is taken from the process rather than rebuilt from `line`,
	// so quoting survives as the shell had it; `line` is what Linux passes on.
	int forwardToPreview(const Args& line);
}
