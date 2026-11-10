// rda-preview — the interface, run with nothing behind it.
//
//   rda-preview prototype <project> ...   open it with placeholder state
//   rda-preview shot      <project> ...   the same with no window: writes a .png
//
// These are `rda prototype` and `rda shot`, and are what `rda` runs when it is asked for
// either: it hands over its whole command line and returns what this returns. They are a
// program of their own because they are the part of the toolchain that needs Vulkan and a
// window. Everything `rda` does besides is the compilers, which need neither, and a build
// step on a machine with no GPU is not a place to find that out.
//
// The arguments are the ones they have always taken (Preview.h, PreviewOptions.cpp), the
// command word first, and the options that apply to every command can still be written
// anywhere on the line. The project is compiled before it is shown, by the same code as
// `rda build`.
#include "Preview.h"
#include "PreviewUsage.h"

#include <cstdio>
#include <string>

namespace {

	void usage() {
		std::printf("rda-preview - the interface, run with nothing behind it\n"
		            "`rda prototype` and `rda shot` run this; the commands are the same\n\n");
		std::printf("%s%s", Cli::kPrototypeUsage, Cli::kShotUsage);
	}
}

int main(int argc, char** argv) {
	Cli::Args args(argv + 1, argv + argc);
	Cli::takeGlobalOptions(args);

	if (args.empty()) { usage(); return 1; }

	if (args[0] == "prototype") return Cli::cmdPrototype(args);
	if (args[0] == "shot")      return Cli::cmdShot(args);

	usage();
	return 1;
}
