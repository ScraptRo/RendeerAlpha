# Setup

How to get from a fresh clone to a running interface of your own, in any of the four
languages the engine supports.

The engine is a checkout you build **once**. A bring-up script does that and leaves the
result in `bin/` at the checkout's root: the engine as a shared library, the `rda` tool
that compiles an interface, and the engine's own assets. After that, a C++ project links
the engine through CMake, and a Python, Node or C# project points at `bin/` and builds
nothing but its own interface — one command, no build system.

## In order

1. [What you need](what-you-need.md) — the compiler, CMake, the Vulkan SDK, and which of
   Python, Node and .NET you actually need.
2. After cloning: [Windows](after-cloning/windows.md) or [Linux](after-cloning/linux.md) —
   run the bring-up script. It checks what is installed and says what is missing, builds
   the engine, runs its tests, fills `bin/`, and registers the Python and Node packages.
3. [Adding the engine to a project](adding-to-a-project/README.md) — what a
   [C++](adding-to-a-project/cpp.md), [Python](adding-to-a-project/python.md),
   [Node](adding-to-a-project/node.md) or [C#](adding-to-a-project/csharp.md) project
   looks like, or an [application that already exists](adding-to-a-project/existing-application.md).
4. [Your first interface](first-interface/README.md) — the three files every interface
   starts from, then the backend that drives them in
   [C++](first-interface/cpp.md), [Python](first-interface/python.md),
   [Node](first-interface/node.md) or [C#](first-interface/csharp.md).

## When you need them

| | |
| --- | --- |
| [Editor support](editor-support.md) | red squiggles in a layout, before the build sees it |
| [Build options](build-options.md) | the switches, and what each is for |
| [Testing what you build](testing.md) | driving a compiled interface without opening a window |
| [When it does not work](troubleshooting.md) | the messages you are most likely to meet, and what each means |

---

Back to [all documentation](../README.md)
