# Build options

For a C++ project, set these before `add_subdirectory`, from your own CMakeLists. A
Python, Node or C# project has no CMake and sets none of them; it uses whatever the
engine's own build staged into `bin/`.

| | default | |
| --- | --- | --- |
| `RDA_BUILD_TESTS` | ON | the engine's own test runner, and the binding tests. Off for an application |
| `RDA_BUILD_TOOLS` | ON | the `rda` toolchain. Off means committed blueprints only |
| `RDA_BUILD_C_API` | ON | the `rendeer_c` shared library. Off for a C++-only application |
| `RDA_COMPILE_LAYOUTS` | ON | recompile `.tsx` during the build. Off uses what is committed |
| `RDA_ENABLE_SCRIPTING` | OFF | an embedded JavaScript host at run time. See below |

`RDA_ENABLE_SCRIPTING` stays off because *a shipped binary evaluates no JavaScript* is a
promise this engine makes, and this switch is what qualifies it.

Three more that are not switches:

| | |
| --- | --- |
| `RDA_ENGINE_DIR` | where the engine checkout is. Every C++ example defaults it to `../RendeerAlpha` |
| `RDA_ESBUILD` | a specific esbuild to use, instead of the one found in `bin/`, `node_modules` or on `PATH` |
| `RDA_STAGE_DIR` | where the `stage` target lays out the engine for other languages. Defaults to `bin/` in the checkout |

## The `stage` target

Only the engine's own build has it. `cmake --build build/default --config Debug --target
stage` copies the shared library, the tool, the C header and the engine's `res/` into
`RDA_STAGE_DIR`, and writes a `BUILD.txt` saying which configuration it was. The bring-up
scripts run it; run it yourself after rebuilding the engine, or to switch the staged
configuration to Release.

---

Back to [setup](README.md) · [all documentation](../README.md)
