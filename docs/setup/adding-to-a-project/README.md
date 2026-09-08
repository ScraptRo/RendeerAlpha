# Adding the engine to a project

An application lives *outside* the engine's checkout and points at it. Cloning the engine
gets you a library, not a library plus somebody's demos, and your project is yours.

## Where things go on disk

The arrangement the examples assume is two folders side by side, with the engine built
once by its bring-up script:

```
somewhere/
├── RendeerAlpha/       the engine checkout
│   └── bin/            what the bring-up leaves: the shared library, the tool, the font
└── MyApp/              your application, pointing at it
```

Nothing enforces that. A C++ project sets `RDA_ENGINE_DIR` once when configuring; a Node
project names the path in its `package.json`; a Python project finds whichever checkout
`import rda` was registered to; and every binding honours `RDA_ENGINE=<some bin/>` as an
override.

## What every project has

Whatever the language of its backend, an application carries the same `res/`:

```
MyApp/
└── res/
    ├── state.ts             what both sides agree on: signals, commands, tables, screens
    ├── themes/app.ts        the look
    └── layouts/
        ├── rda.d.ts         generated -- what a layout is checked against
        ├── tsconfig.json    editor type-checking only
        └── home.tsx         the interface
```

[Your first interface](../first-interface/README.md) shows each of those files in full.
This folder is about what sits beside them, which depends on the language — and there
are two shapes, not four.

## C++: build rules

A C++ project links the engine statically, so it has a CMake build, and
`add_subdirectory` on the engine gives that build four rules:

| | |
| --- | --- |
| `rda_add_state(target decl.ts)` | generates `RdaState.h` and puts it on the include path |
| `rda_add_theme(target theme.ts)` | compiles the theme to a `.rdth` beside its source |
| `rda_add_types(target out.d.ts theme.ts decl.ts)` | generates the TypeScript a layout is checked against |
| `rda_add_layouts(target a.tsx b.tsx …)` | compiles each to a `.rdab` beside its source |

Two of those write into your **source tree** rather than the build directory, on purpose.
A `.rdab` and a `.rdth` are committed artefacts, the way a generated parser is: a build
with `RDA_COMPILE_LAYOUTS=OFF`, or on a machine with no esbuild, has no compiler and
needs the blueprint to already be there. `rda_add_state` generates into the build tree
and has no such fallback.

`rda_add_types` regenerates `rda.d.ts` from three things — the widget schema compiled into
the toolchain, the theme that defines the variants, and the state declaration — so it
follows all three. A committed `.d.ts` that has drifted from the engine is worse than
none: it reports errors that are not real and misses ones that are.

## Everything else: one command

A Python, Node or C# project has no C++ to compile and no reason to own a CMake build.
It points at the engine's `bin/`, and compiles its interface with one command that does
what the four rules above do, by convention:

```
rda build <project> --python      # or --node, --csharp
```

Given the `res/` above, that writes each theme's `.rdth` and each layout's `.rdab` beside
its source, `res/layouts/rda.d.ts` from the first theme and the declaration, and the
state module — `state.py`, `state.mjs` or `state.cs` — into the project directory,
beside the program that imports it. Anything already newer than its inputs is left alone,
so it is cheap to run before every start. Python spells it `python -m rda build .`,
Node `npx rda build .`; C# calls the tool from its `.csproj`.

The engine's own assets — the font, the syntax definitions — are not copied anywhere:
the engine finds them beside its shared library, which is `bin/res/`. Your project's
`res/` resolves relative to the working directory, so run the program from the project
directory.

## One page per language

| | |
| --- | --- |
| [C++](cpp.md) | the native case: link `rendeer`, compile the interface, copy the assets |
| [Python](python.md) | `import rda`, `python -m rda build .`, `python app.py` |
| [Node](node.md) | `npm install <checkout>/bindings/node`, `npx rda build .`, `node app.mjs` |
| [C#](csharp.md) | two source files compiled in, the tool run from the `.csproj`, `dotnet run` |
| [An application that already exists](existing-application.md) | four lines, one include, two calls |

[Build options](../build-options.md) lists the switches a C++ project sets before
`add_subdirectory`.

---

Back to [setup](../README.md) · [all documentation](../../README.md)
