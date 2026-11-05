## Or have it written for you

What the rest of this page describes is also what `rda init` writes. It makes the folder, puts in `res/` and a backend in the language you name, and builds it once:

```bash
rda init myapp --python      # or --node, --csharp, --cpp
cd myapp
rda prototype .              # the interface, before any backend exists
python app.py
```

With no language flag you get the interface alone, which `rda prototype .` opens as it stands. The project is a small counter — a heading, a button that counts, and a command the backend answers — so there is something on screen to change. Its layout is `res/layouts/main.tsx`, where the tutorials call it `home.tsx`; nothing depends on the name.

`init` refuses to touch a folder that already holds a file it would write, names the files, and changes nothing until you add `--force`. It ends by running the same build as `rda build`, so `res/layouts/rda.d.ts` is there and your editor works at once; if esbuild cannot be found the project is still made, and it says so. `--no-build` skips that step.

The pages after this one are the same files, written out and explained. Read them for the reasons; run `rda init` to skip the typing.

## C++ — one line of CMake

```cmake title="CMakeLists.txt"
add_subdirectory("path/to/RendeerAlpha" rendeer)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE rendeer)

rda_add_state(myapp   res/state.ts)
rda_add_theme(myapp   res/themes/mytheme.ts)
rda_add_types(myapp   res/layouts/rda.d.ts res/themes/mytheme.ts res/state.ts)
rda_add_layouts(myapp res/layouts/main.tsx)
```

That gives the application the library, the `rda` tool, and the build rules that compile its interface. Each `rda_add_*` rule is a build step: `rda_add_state` generates `RdaState.h` from the declaration, `rda_add_theme` compiles the theme to `.rdth`, `rda_add_types` generates the `rda.d.ts` your editor checks the layout against, and `rda_add_layouts` compiles each `.tsx` into a `.rdab`. A project with widget shapes of its own adds `rda_add_meshes(myapp res/mesh/tab.ts)` ahead of them, because a theme may set what a mesh declares.

## Python, Node and C# — no build system at all

A project in another language has no CMake. It points at the checkout’s `bin/`, and one command does for its `res/` what those four rules do:

```bash
python -m rda build .      # Python
npx rda build .            # Node
rda build .                # anything, using bin/rda directly
```

That compiles any meshes, the theme and every layout beside their sources, and writes the generated state module for the backend the folder has — `state.py`, `state.mjs` or `state.cs`. It works out which from the project root and says what it found on the first line, `rda: detected Python (app.py)`:

| Found in the project root | Written |
| --- | --- |
| `app.py`, or any `*.py` | `state.py` |
| `package.json`, or any `*.mjs` | `state.mjs` |
| a `*.csproj`, or an existing `state.cs` | `state.cs` |
| a `CMakeLists.txt` | nothing: CMake generates the C++ header |

A folder that matches more than one gets each of them rather than a guess. A folder that matches none gets no state module, and the build says so in one line that names the flags, because `3 written` would otherwise read as complete. `--python`, `--node` or `--csharp` always wins and nothing is looked for. The engine itself is a shared library the backend opens at run time, not something you compile against.

| Language | What sits beside res/ |
| --- | --- |
| **C++** | a `CMakeLists.txt` with the four `rda_add_*` rules |
| **Python** | nothing — `import rda` finds the checkout through the `.pth` the bring-up wrote |
| **Node** | a `package.json`; one `npm install` links the package from the checkout and brings koffi with it |
| **C#** | a `.csproj`; the binding is two source files you compile in |

## What a project looks like

```text
myapp/
├── res/
│   ├── state.ts                what both sides agree on
│   ├── themes/app.ts           the look
│   └── layouts/
│       ├── home.tsx            the interface
│       ├── rda.d.ts            generated — what the editor checks against
│       └── tsconfig.json       for the editor only; nothing emits from it
└── app.py | main.cpp | app.mjs | Program.cs
```

> An unknown element or an unknown property **compiles, with a warning**. The build checks names against the widget schema and says what it did not recognise and what you probably meant: `<button> 'root/again' has no property 'widht'; did you mean 'width'?`. The loader still skips the element and ignores the property, so `rda build --strict` (or `rda layout --strict`) turns each warning into an error, which is what a build that must not ship a typo wants. The editor catches the same typo sooner, which is what `rda.d.ts` and that `tsconfig.json` are for; `rda elements checkbox` lists what an element takes.

## An application that already exists

`rendeerRun` owns the window and the loop, which suits a program written around it. A program that already has a main loop runs the engine **Owned** instead: it keeps its own loop and pumps the engine from inside it. Same layouts, same theme, same state — the difference is only who calls whom.
