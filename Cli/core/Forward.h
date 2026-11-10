// `rda prototype` and `rda shot`, which are run by another program.
//
// They open a window, so they need Vulkan and a display, and the rest of `rda` needs neither:
// compiling a layout in a build step on a machine with no GPU is the ordinary case. They live
// in `rda-preview`, and `rda` hands them over so that nobody has to type the other name.
#pragma once

#include "Common.h"

#include <string>
#include <vector>

namespace Cli {

	// Runs `rda-preview` from the directory `rda` itself is in, with `line` as its command
	// line -- the whole of it, the options that apply to every command included, exactly as
	// they were written. Its standard streams are this process's, and its exit code is
	// returned. When it is not there, says so and returns 1.
	//
	// On Windows the command line is taken from the process rather than rebuilt from `line`,
	// so quoting survives as the shell had it; `line` is what Linux passes on.
	int forwardToPreview(const Args& line);

	// A program started to be waited for, and started again: what `rda dev` keeps running
	// while it watches the files. The launching is the same as forwardToPreview's, with one
	// difference that is the whole reason this is not that: this process stays, so it has to
	// know when the child is done, be able to end it, and start another in its place.
	class Child {
	public:
		Child() = default;
		Child(const Child&) = delete;
		Child& operator=(const Child&) = delete;
		// A child left running is ended with this: a window nobody is waiting for is the worse
		// way for a tool to finish.
		~Child() { stop(); }

		// `command` is the program and its arguments, run in `directory` with this process's
		// streams. False, with the reason printed, when it will not start.
		bool start(const std::vector<std::string>& command, const fs::path& directory);

		// Whether it has not ended yet. Never waits; the first call after it ends takes its
		// exit code.
		bool running();
		int  exitCode() const { return mExitCode; }

		// Asks it to end, gives it a moment, and ends it if it has not. Safe to call twice.
		void stop();

	private:
		struct Handles;
		Handles* mHandles = nullptr;
		int      mExitCode = 0;
	};

	// Ctrl+C, as a flag to be looked at. Installed by the one command that waits for a child:
	// the child gets the same Ctrl+C on the same console and ends itself, and this process
	// outlives it by long enough to say so and to return the child's code.
	void watchForInterrupt();
	bool interrupted();
}
