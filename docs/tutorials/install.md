## What you need

|  |  |
| --- | --- |
| **A C++20 compiler** | MSVC 2022, or GCC 11 / Clang 14 |
| **CMake 3.21+** | the multi-config presets need it |
| **The Vulkan SDK** | on Windows this is the only install: it carries Vulkan, shaderc and glm as a matched set |
| **Python 3.8+** | only for a Python backend. Nothing to compile — the binding is ctypes |
| **Node 18+** | only for a Node backend |
| **.NET 8+** | only for a C# backend. Nothing beyond the SDK |

The first three build the engine, once. On Linux the four dependencies are ordinary packages — `libvulkan-dev`, `libshaderc-dev`, `libglm-dev`, `libglfw3-dev`. On Windows, GLFW is the one thing the Vulkan SDK does not ship, and it is built from source the first time a build tree is configured.

## The bring-up script

```bash
git clone https://github.com/ScraptRo/RendeerAlpha.git
cd RendeerAlpha

scripts\windows-bringup.bat        # or ./scripts/linux-bringup.sh
```

The script is written for a machine that has none of this yet. It checks what is installed, and when something is missing it prints the `winget` line or the `apt` package and stops — install it, run the script again. Then it fetches esbuild, builds the engine, runs its tests, lays the result out in `bin/`, and registers the Python and Node packages with whatever interpreters it found.

- `--jobs N` limits the parallel compile.
- `--release` stages a Release engine instead of Debug.
- A **Debug** engine is the one to develop against: it recompiles a layout you edit while the application is running, and it turns the Vulkan validation layer on.

> It installs nothing itself. The one thing it writes outside the checkout is a `.pth` file in your Python user site-packages, which is how `import rda` finds the checkout.

## esbuild, and why node is not on the list twice

A state declaration, a theme and a layout are all TypeScript. The `rda` tool turns each into what the engine loads, and the first step of that is esbuild transforming the TypeScript into something the tool can evaluate. esbuild is a single static binary, not a node program — the bring-up puts one in `bin/` beside the tool, copied from `node_modules` if you have node and downloaded from the npm registry if you do not.

So a Python project with no `node_modules` anywhere still compiles its layouts, and node appears on the requirements list only for a **Node backend**, where it is the interpreter your application runs in. What ships contains no parser and no node; a layout's own functions run as precompiled bytecode on an embedded QuickJS.

## The manual way

```bash
scripts\build.bat
scripts\test.bat
cmake --build build\default --config Debug --target stage
```

The same steps, one at a time. Configuring prints where each dependency came from, because *which one did it find* is the first question when a build works on one machine and not another:

```text
-- Vulkan: C:/VulkanSDK/1.3.275.0/Lib/vulkan-1.lib
-- shaderc: C:/VulkanSDK/1.3.275.0/Lib/shaderc_combined.lib
-- glm: Vulkan SDK
-- GLFW: building from source (3.4)
-- esbuild: .../RendeerAlpha/bin/esbuild.exe (0.28.2)
```

The tests are two kinds of thing. The engine’s own cases need no device and take a fraction of a second. The three **binding tests** start a real engine, so a window appears for a few seconds and closes itself — that is `rda_python_binding`, `rda_node_binding` and `rda_csharp_binding` proving the C ABI works from outside C++. Each skips itself, with a reason, when Python, node, koffi or dotnet is not installed.

## What lands in bin/

```text
bin/                       written by the `stage` target; not tracked
├── rendeer_c.dll          the engine as a shared library — what every non-C++ backend opens
├── rda.exe                the toolchain: `rda build <project>` compiles an interface
├── esbuild.exe            what the toolchain transforms TypeScript with
├── include/RendeerC.h     the C ABI, for anything with an FFI
├── res/                   the engine's font and syntax definitions
└── BUILD.txt              which configuration this is
```

A C++ application never looks in `bin/`: it adds the checkout as a subdirectory and links the engine through CMake. Every other language points at `bin/` and builds nothing but its own interface.
