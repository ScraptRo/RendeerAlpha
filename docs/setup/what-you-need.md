# What you need

| | |
| --- | --- |
| **A C++20 compiler** | MSVC 2022, or GCC 11 / Clang 14 |
| **CMake 3.21+** | the multi-config presets need it |
| **The Vulkan SDK** | on Windows this is the only install: it carries Vulkan, shaderc and glm |
| **Python 3.8+** | only for a Python backend. Nothing to compile |
| **Node 18+** | only for a Node backend |
| **.NET 8+** | only for a C# backend. Nothing to install beyond the SDK |

The first three build the engine, once. The bring-up script for your platform checks for
each and prints the `winget` line or the `apt` package for whatever is missing —
[Windows](after-cloning/windows.md), [Linux](after-cloning/linux.md).

Nothing is vendored that you have to fetch by hand. On Windows the Vulkan SDK supplies
Vulkan, shaderc and glm as a matched set — one installer, so the headers and the libraries
cannot drift apart — and GLFW, the one thing it does not ship, is built from source the
first time a build tree is configured. On Linux the four are ordinary packages
(`libvulkan-dev`, `libshaderc-dev`, `libglm-dev`, `libglfw3-dev`), and the bring-up
installs them. Configuring prints which copy of each it picked, because "which one did it
find" is the first question when a build works on one machine and not another.

## esbuild, and why node is not on the list twice

A state declaration, a theme and a layout are TypeScript. The `rda` tool turns each into
what the engine loads, and the first step of that is esbuild transforming the TypeScript
into something the tool can evaluate. So esbuild is needed to compile any interface —
and esbuild is a single static binary, not a node program.

The bring-up puts one in `bin/` beside the tool: copied from `node_modules` if you have
node, downloaded from the npm registry if you do not. Either way a Python project with no
`node_modules` anywhere compiles its layouts, and node is only on the list for a **Node
backend**, where it is the interpreter your application runs in.

It is a **build-time** dependency and only that. What ships contains no parser, no
JavaScript engine and no node — a Node backend opens the engine as a shared library, not
the other way round.

The engine itself builds without esbuild; the binding tests, whose fixture is compiled
from `.tsx`, skip with a reason when it is absent.

---

Next: after cloning, on [Windows](after-cloning/windows.md) or [Linux](after-cloning/linux.md).

Back to [setup](README.md) · [all documentation](../README.md)
