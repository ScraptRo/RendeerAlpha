# The rda commands

`rda` is one program with a command for each thing you do to a project. Run it with no
arguments for the list; this page is what each one is for.

```bash
rda init myapp --python     # a project: res/ and a backend to start from
rda build .                 # compile everything under res/
rda dev .                   # build, run the backend, run it again when state.ts changes
rda add screen settings     # a second screen, and the route to it
rda elements checkbox       # what an element takes
rda doctor .                # what rda found, and what is missing
rda prototype .             # the interface in a window, with placeholder state
rda shot .                  # the same with no window: a .png
```

## build

`rda build <project>` compiles the meshes, the themes, the layouts and the types under
`res/`, and writes the generated state module of the backend the folder has. It works that
out from the project root, and prints what it found:

| Found in the project root | Written |
| --- | --- |
| `app.py`, or any `*.py` | `state.py` |
| `package.json`, or any `*.mjs` | `state.mjs` |
| a `*.csproj`, or an existing `state.cs` | `state.cs` |
| a `CMakeLists.txt` | nothing: CMake generates the C++ header |

More than one match writes each. No match writes none, and says so with the flags to use.
`--python`, `--node` and `--csharp` override the detection, and nothing is looked for when
one is given.

Every element and property in a layout is checked against the widget schema. A name it does
not know is a warning that says what was probably meant, and the layout still builds:

```text
rda: warning: res/layouts/main.tsx: <checkbox> 'root/remember' has no property 'text'; did you mean 'label'?
```

`--strict`, on `build` or `layout`, makes each of those an error, and compiles every layout
again rather than skipping the ones whose blueprint is current: a warning only exists while
a layout is being compiled, so a build that skipped it would pass on a typo it had already
been told about. A component's own props are never checked -- it is expanded first, and what
it returns is what is.

## dev

`rda dev [<project>]` builds, then starts what the project has for a backend: `python
app.py`, `node app.mjs` or `dotnet run`. A C++ project is its own program, so `dev` says to
build it with CMake and stops. With no backend at all it falls back to `rda prototype`.

A Debug engine reloads layouts and themes the moment they are saved, so the only file
`dev` watches is `res/state.ts` and what it imports. When it changes the project is built
again and the backend is stopped and started in its place. A build that fails leaves the
running backend alone. The backend's exit code is `dev`'s, and Ctrl+C stops it.

`--dry-run` prints what would run and starts nothing. `--python`, `--node` or `--csharp`
chooses when the folder has more than one backend.

## add screen

`rda add screen <name> [<project>]` writes `res/layouts/<name>.tsx` from a template and adds
the route to `res/state.ts`. If `routes` is there an entry is added in the layout the file
already has. If it is not, the declaration is added at the end of the file with an entry for
each layout already in `res/layouts/`, so the screen you have is not lost, and `main` comes
first because the first route is where the application opens.

A project with no routes had a backend that loaded one layout. With routes the backend
opens all of them instead, and the command says what to change for the language the folder
has: `rda.open_routes(ROUTES, "res/layouts", "res/layouts")` in Python,
`rda.openRoutes(ROUTES, ...)` in Node, `Rda.OpenRoutes(Routes.All, ...)` in C#, a
`Router` in C++. See [screens](../frontend/screens.md).

The command refuses when the layout or the route already exists. A `state.ts` it cannot edit
safely -- the routes written on one line, a `routes` that is something else -- is left
alone, and the lines to paste are printed.

## elements

`rda elements` lists every element with a line on what it is. `rda elements <name>` lists
its properties and events and the common ones with them, with each one's type, whether it
can be bound to state, and what it does. `--json` prints the same as data.

## doctor

`rda doctor [<project>]` says what it found, one line each, `ok` or `PROBLEM` with what to
do: rda and its version, esbuild, `rda-preview` beside it, whether a Vulkan loader can be
opened, python, node and dotnet if they are there, and where `import rda` lands for that
python. Given a project it adds `res/`, the declaration, the themes and layouts, which
backend it found, and whether the build is current. It exits 1 when something the project
or the build cannot do without is missing.

## prototype and shot

See [seeing it before there is a backend](prototyping.md) and [seeing it without
watching it](seeing-it.md). `rda shot` can aim at a widget by name as well as by point:
`--click add` clicks the centre of the widget whose id is `add`, or whose path ends in
`/add`; `root/buttons/add` says more of the path. A name that matches nothing, or more than
one, is refused with the ids to choose from. `--hover` and `--press` take a name the same way.

---

Back to [setup](README.md) · [all documentation](../README.md)
