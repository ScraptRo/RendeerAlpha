# After cloning, on Windows

## The short way

```bat
git clone https://github.com/ScraptRo/RendeerAlpha.git
cd RendeerAlpha
scripts\windows-bringup.bat
```

The bring-up script is meant for a machine that has none of this yet. It checks for
Visual Studio 2022 with C++, CMake and the Vulkan SDK, and when one is missing prints the
`winget` line or the download page and stops — install it, run the script again. Then it
fetches esbuild, builds the engine, runs its tests, lays the result out in `bin\`, and
registers the Python and Node packages with whatever interpreters it found. Everything it
prints is meant to be readable on its own, so the output can be pasted somewhere without
the machine being present.

`--jobs N` limits the parallel compile; `--release` stages a Release engine instead of
Debug. A Debug engine is the one to develop against: it recompiles a layout you edit
while the application runs, and it turns the Vulkan validation layer on.

It installs nothing itself. The one thing it writes outside the checkout is a `.pth` file
in your Python user site-packages, which is how `import rda` finds the checkout.

## What it needs, if you would rather install by hand

- **Visual Studio 2022** with the *Desktop development with C++* workload — any edition,
  Build Tools included. `winget install Microsoft.VisualStudio.2022.BuildTools --override
  "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"`.
- **CMake 3.21+** — `winget install Kitware.CMake`, or the copy bundled with Visual
  Studio, which the script finds by itself.
- **The Vulkan SDK** from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home), or
  `winget install KhronosGroup.VulkanSDK`. The installer sets `VULKAN_SDK` for *new*
  shells; a terminal opened before the install does not have it, which is the usual
  reason the SDK looks missing when it is not.
- **Python**, **Node**, **.NET** — each only for a backend in that language.

## The manual way

```bat
scripts\build.bat
scripts\test.bat
cmake --build build\default --config Debug --target stage
```

The first configure builds GLFW from source, which needs the network once; after that it
is cached in the build tree. Configuring prints where each dependency came from:

```
-- Vulkan: C:/VulkanSDK/1.3.275.0/Lib/vulkan-1.lib
-- shaderc: C:/VulkanSDK/1.3.275.0/Lib/shaderc_combined.lib
-- glm: Vulkan SDK
-- GLFW: building from source (3.4)
-- esbuild: G:/.../RendeerAlpha/bin/esbuild.exe (0.28.2)
```

`test.bat` runs two kinds of thing. The engine's own cases need no device and take a
fraction of a second. The **binding tests** start a real engine — so a window appears for
a few seconds and closes itself. That is not a stray application; it is
`rda_python_binding`, `rda_node_binding` and `rda_csharp_binding` proving that the C ABI
works from outside C++. Each skips itself, with a reason, when Python, node, koffi or
dotnet is not installed — and all three are skipped when esbuild is not there.

```
1/4 Test #1: rendeer_tests ....................   Passed
2/4 Test #2: rda_python_binding ...............   Passed
3/4 Test #3: rda_node_binding .................   Passed
4/4 Test #4: rda_csharp_binding ...............   Passed
```

The `stage` target is what fills `bin\`; the bring-up runs it for you. Registering the
packages by hand is `python bindings\python\register.py` and, for Node, `npm install`
inside `bindings\node`.

## Where things are

```
bin\                       written by `stage`; not tracked
├── rendeer_c.dll          the engine as a shared library -- what every non-C++ backend opens
├── rendeer_c.lib, .pdb    the import library and the symbols
├── rda.exe                the toolchain: `rda build <project>` compiles an interface
├── esbuild.exe            what the toolchain transforms TypeScript with
├── include\RendeerC.h     the C ABI, for anything with an FFI
├── res\                   the engine's font and syntax definitions, found from here
└── BUILD.txt              which configuration this is

build\default\             the build tree: rendeer_tests.exe, and the same things before staging
```

A C++ application never looks in `bin\`: it adds the checkout as a subdirectory and
links the static engine. Every other language does, and `bin\README.md` says how.

There is no application in here to start. This repository is the engine alone —
[your first interface](../first-interface/README.md) is where one comes from.

---

Next: [adding the engine to a project](../adding-to-a-project/README.md).

Back to [setup](../README.md) · [all documentation](../../README.md)
