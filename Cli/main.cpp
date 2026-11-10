// rda -- the layout toolchain. README.md says how this directory is laid out, and how to add a
// command; `rda` with nothing after it lists them.
#include "Command.h"

int main(int argc, char** argv) {
	return Cli::runLine(Cli::Args(argv + 1, argv + argc));
}
