# The repository

This repository is the engine and its toolchain. Applications live outside it and point
at it, so that cloning this gets a library rather than a library plus somebody's demos.
Writing one is the subject of [Setup](../setup/README.md).

| Path | What it is |
| --- | --- |
| `RendeerAlpha/` | The engine, built as a static library, plus `rendeer_c` beside it |
| `Cli/` | The `rda` tool: `build` for a whole project, and `layout`, `theme`, `state`, `types`, `python`, `node`, `csharp`, `dump` one file at a time |
| `bindings/` | The backend packages: `python/` (ctypes), `node/` (koffi), `csharp/` (P/Invoke), and `tests/` |
| `cmake/` | Dependency resolution, and the codegen rules a C++ project's build calls |
| `RuntimeTests/` | The test runner — 105 cases, run by `ctest` |
| `scripts/` | The bring-up for each platform, and `build` / `test` one step at a time |
| `skills/` | The reference written for a language model, and an MCP server that lets one compile a layout and read the error |
| `bin/` | Not tracked. What the build's `stage` target leaves for every backend that is not C++: the shared library, the tool, esbuild, the C header, the engine's `res/` |
| `*/vendor/` | What is genuinely vendored: QuickJS, stb, tinyxml2, VMA |

Vulkan, shaderc, glm and GLFW are **not** vendored. They come from an installed SDK or a
package manager, and GLFW is built from source when neither has it — see
`cmake/RdaDependencies.cmake`, which explains the ordering and why a 1.2 GB `Dep/`
directory used to be tracked and is not any more.

A C++ application consumes it with one line — `add_subdirectory` on this checkout —
which gives it the `rendeer` library, the `rda` tool, and the `rda_add_*` build rules.
The library exports its own language requirement, so a consumer does not have to know it
is C++20. Every other language consumes `bin/`: the shared library opened through an FFI,
and `rda build` run over the project's `res/`. The engine finds its own assets beside
its shared library, so nothing is copied into a project.

Inside the engine, `src/` groups by what the files do: `GraphicalSrc/` is Vulkan,
`GraphicalObjects/` is windows and widgets, `Layout/` is the blueprint pipeline, `Core/`
is signals and loop plumbing.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
