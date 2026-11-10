# The rda command line

`rda` is the commands that compile, which need no device and no window. `rda-preview` is the two
that open one, `prototype` and `shot`; `rda` hands those over, so nobody has to type the other name.

```
Cli/
  main.cpp      rda's main, one line
  core/         what every command stands on: the registry, arguments, files, child processes
  commands/     one file per set of commands
  preview/      rda-preview: the window, the editor, a scripted pointer
  templates/    what `rda init` writes
```

## Adding a command

One new file in `commands/`, and nothing else. CMake compiles every `.cpp` in that directory, and
the file registers itself. A new file is seen the next time CMake configures, so run
`cmake -S . -B build/default` once after adding it. Visual Studio's generator otherwise only
notices on the build after next.

```cpp
// `rda hello <name>`: says hello.
#include "Command.h"

#include <cstdio>

namespace Cli {
	namespace {
		int cmdHello(const Args& args) {
			std::printf("hello, %s\n", argument(args, 1).c_str());
			return 0;
		}

		const Command kHello{ __FILE__, "hello", Section::Learn, 1,
			"  rda hello  <name>                  says hello\n",
			cmdHello };
	}
}
```

- `Section` is where it is listed in the usage text: `Start`, `Build`, `Run` or `Learn`.
- The number is how many positional arguments it cannot do without. A line with fewer gets the
  usage text, so the command never has to check for them.
- The usage is printed exactly as written: it starts with `  rda <name>`, and it ends with a newline.
- A second name for a command is another `Command` with `nullptr` for the usage. It works and is
  not listed.
- A file may register several commands, when they belong together: `Compile.cpp` has five.

Keep everything file-local, in an anonymous namespace. A command needs no header, because nothing
calls it by its C++ name. To use another command, go through the core:

```cpp
if (runCommand({ "build", project.string() }) != 0) return 1;
```

Shared helpers belong in `core/Common.h`, and that is the one place two people adding commands can
meet. Add to it only what a second command really needs, and keep the rest in your own file.

## Checking it is connected

```bash
rda --check-commands
```

It fails if a file in `commands/` registered nothing, two commands share a name, or a usage text
does not follow the rules above, and it runs as the `rda_commands_connected` test. It is the last
step when several commands were written at once: merge them, build, and run this.

Tests for a command go in `RuntimeTests/CMakeLists.txt`, and a person reads about it in
`docs/setup/commands.md`.

## Why `prototype` and `shot` are elsewhere

They link the renderer and need a display. A build step on a machine with no GPU is the ordinary
case for everything in `commands/`, so that code stays free of it. `commands/Forwarded.cpp` lists
the two commands and hands the line over. Both programs contain every command, so the preview can
run `build` before it shows anything.
