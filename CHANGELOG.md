# Changelog

Newest first.

Two numbers move independently and both are named in every entry. The **version** is the
engine's. The **C ABI** is what a backend in Python, Node, C# or anything else with an FFI
is built against: its major says whether a package built against an older engine still
runs, and a minor means functions were added while nothing already there changed meaning.
See [the C ABI](docs/backend/c-abi.md).

## 1.3.0 — 2026-11-10

C ABI **1.8**: three calls added for what happens to a viewport (1.7), and three for what a
command carries, running one for its result, and loading state.ts's code (1.8); nothing already
there changed meaning, so a package built against 1.2.0 runs against this untouched. A backend gets
state.ts's code by regenerating its state module, and a layout gets the rest by being rebuilt.

This one is about writing an interface rather than drawing one. Logic can live in `res/state.ts`
and in a layout's own functions, a screen says what happens when it opens and closes, a value that
reads state needs no arrow, and a layout can be an object. The command line starts a project, works
out its backend, catches a misspelt name, explains every widget, and edits a layout with the mouse in
`rda prototype`. Underneath, the engine is twelve layers that the build enforces, and a test or a
build server can run an application with nothing on the screen.

### Before upgrading

A backend changes nothing, and nothing here crosses the C ABI. These can change what an existing
project builds or what it draws:

- **A value binding that writes state no longer builds.** It always undid itself at run time; the
  write belongs in a handler, and the message says where. See *A value binding could write*.
- **`width` and `height` apply to a widget nothing arranges** -- a child of a `<panel>` or a
  `<container>`, or a layout's root. A size that used to be ignored takes effect, so
  `height="content"` on a root no longer stretches to the window.
- **A theme's `fontSize` is drawn at that size** rather than at the nearest one baked at startup,
  so text at 16 is a little larger than the 15 it was.
- **`rda build .` writes the state module of the backend it finds** -- an `app.py` means a
  `state.py` -- where it used to write none without a flag. A flag is still the whole answer.
- **A misspelt element or property is a warning at build time**, and an error with `--strict`.
- **A C++ application that reached past the interface into the engine** may have an include or a
  name to change; *The engine in layers* lists them. Include paths and the `rendeer` library are
  the same.

### Logic in the state, and commands that carry their data

`res/state.ts` may hold code now, and none of it is a new kind of thing. A field with
`compute` is a signal state.ts works out; a command with `run` is one state.ts answers; and
every command carries its arguments.

```ts
export const state = defineState({
  price: 0,
  total: { compute: () => state.price * state.qty, doc: "what the cart costs" },
})
export const commands = defineCommands({
  rename: { doc: "rename a line", params: { id: 0, name: "" } },
  add:    { run: (n: number) => { state.qty += n; return state.qty } },
})
```

**A command carries what the event meant.** `commands.rename(item.id, state.draft)` from a
handler, in the grammar or in JavaScript; the backend's handler receives `(id, name)` typed
in its own language, from the declaration's `params`. Underneath it is one JSON array,
which the C ABI hands out with `rda_command_args()`. It replaces writing a signal first and
asking second -- two steps for one intent, a signal declared only to carry a value, and a
race when two rows were clicked before the backend looked.

**A computed signal** is read like any other, by a binding or a backend, and written by
nothing but the engine: a handler, run-time code and `rda_signal_set_*` are refused, and no
setter is generated. It is recomputed inside the write that changed what it read, before
that write returns, so a backend that sets `price` and reads `total` on the next line reads
the new total. What it reads is recorded as it runs; a table it read is checked once a
frame. Its type is what it returned when the build ran it once on the starting values.

**An action** is a command state.ts answers. A layout calls it as it calls any other and
cannot tell; `rda prototype` runs it with no backend at all; a backend that binds it hears
of it afterwards; and `rda_call(name, args_json)` runs any command and returns what it
returned -- which is how a rule written once in state.ts is the backend's rule too.

The code is compiled by `rda build` the way a layout's is and embedded in every generated
module -- `RdaState.h`, `state.py`, `state.mjs`, `state.cs` -- whose `define()` loads it
(`rda_load_state_code`), so it runs before any interface is loaded. `defineState`,
`defineTables` and `defineCommands` change nothing that is built; they type `state` in the
file as it is when the code runs. `rda prototype` reloads state.ts's code on save and keeps
every value. `rda build` checks it whenever it writes the types. See
[functions](docs/frontend/functions.md#in-statets-computed-values-and-actions) and
[the declaration](docs/backend/declaration.md#code-in-the-declaration).

### Functions in a layout

A binding is no longer limited to the expression grammar. It may call a function the file
or one of its imports defines, and anything past the grammar -- `.toFixed()`, `.split()`,
an array, a loop, `tables.files.reduce(...)` -- is compiled to JavaScript instead of
refused. So a formatter, a validator or a sum over a table is written once, in the layout,
and works the same under a backend in any language, rather than being written in each
backend as a signal it keeps up to date.

Nothing is parsed at run time. Code reached from a binding or a handler is compiled to
QuickJS bytecode when the layout is built and carried in the blueprint; everything else --
the file's top level, every component -- runs at build time as before. A top-level value
whose initializer calls nothing ships as code; one a call made ships as the value it made,
which must be plain data. A binding the grammar covers still runs as a few stack operations
with no engine behind it, and a layout with no functions starts no runtime at all.

What it reads is recorded as it runs, so a binding that calls a function re-runs when
exactly what it read changes, tables included. A value binding cannot write state or call
a command; a handler can. An error is logged once with the `.tsx` line and the binding keeps
its last value; a call that runs past 50 ms is stopped. Hot reload swaps the code and keeps
the state, because the state was never in it.

Measured on a Release build: a grammar binding 0.07 µs, one calling a function 0.13 µs, a
whole binding in JavaScript 0.4 µs; the runtime 0.1 ms and 91 KB, once.

`rda build --no-runtime-code`, or `-DRDA_RUNTIME_CODE=OFF`, refuses all of it. `tables` is
typed in `rda.d.ts`. See [functions](docs/frontend/functions.md) and
[the design](docs/architecture/runtime-code.md).

A blueprint with functions is version 3; one without is version 2, byte for byte what it
was. A blueprint with functions in it is code, and is to be trusted only as far as the
executable that loads it is.

### Watching a value

A field of state can say what to do when its value changes, beside what `compute` says about
how it is worked out. It is the home for what a write in a value binding was reaching for.

```ts
export const state = defineState({
  country: { value: "", watch: () => { state.city = "" } },
  city:    "",
  volume:  { value: 50, watch: (now: number) => { if (now > 100) state.volume = 100 } },
  theme:   { value: "dark", watch: (now: string) => commands.saveSetting("theme", now) },
  total:   {
    compute: () => state.price * state.qty,
    watch:   (now: number, was: number) => { if (now > was) commands.noteIncrease(now) },
  },
})
```

A watch runs as a handler does: it may write state, call commands and start timers. It is told
`(now, was)`, once for a batch of writes, and not at all for a starting value or for a value
that moved and came back. It runs at the end of the frame's script step, after timers and
computed values have settled and before bindings are applied, so what it writes is on the
screen in the same frame, and a table can be watched through a computed value that reads it.
A watch may write what it watches, which is how a clamp settles. Watches that never settle are
stopped after 64 passes and logged by name, and what they reached stands, so a value cannot
climb for ever the way the binding could.

Nothing crosses the C ABI: state.ts's code already travels in every generated module, so a
backend gets this by regenerating, and `rda prototype` runs it with no backend. Its parameters
are named by hand, as an action's are, because typing them from the field made every `compute`
that reads `state` circular. See [functions](docs/frontend/functions.md#reacting-to-a-change-watch)
and [the design](docs/architecture/watch-and-on-open.md), which also holds what comes next for
a screen and for a backend.

### Opening and closing a screen

`onOpen` and `onClose` on a layout's root element say what happens when it opens and closes.
They are ordinary handlers, so a plain one runs in the grammar and needs no QuickJS, and
anywhere but the root they are refused with the reason. Every `onOpen` gets exactly one
`onClose`, unless the process dies: a route change, opening another layout in its place, and
the application exiting all close it, the last of those before the backend's shutdown. A saved
edit runs neither, so the count survives; a reload does stop the layout's timers, and the log
now says so, since nothing will start them again.

`Router::show` builds the arriving screen first, so that one that will not load cannot take the
running one down. So the host separates building from announcing, and the order is build,
close the old, swap, put `route` right, open the new. A cross-fade keeps that order, and
`onClose` runs as the screen starts to leave.

A command called from `onOpen` runs while the layout loads, so its handler has to be bound by
then, and the Node and C# samples now bind first. In Node, `run()` also joins the engine before
its last poll, because a closing layout's commands arrive after `running` has turned false. On
a root `<popup>`, `onClose` stays the popup's own event. See
[screens](docs/frontend/screens.md#opening-and-closing).

### A layout as an object

A layout may return plain objects instead of JSX, with `type` naming the element and `children`
what is inside it. `{ type: "label", text: "Hello" }` and `<label text="Hello" />` are the same
thing and compile to the same bytes, which a test holds them to across every kind of node. It
is not a second pipeline: an object is turned into what `h()` makes at the one place every node
passes through, so nothing after that knows which was written. `Layout` is generated into
`rda.d.ts` from the same table as the JSX elements, so a misspelt element or property is
underlined in a nested child as well, and a layout's default export may be the object itself.

Writing one found two things. A handler written as a method, `onClick() { ... }`, did not
compile, because a binding reached the module without going through the step that makes a
method an expression; an action's `run` always had. And text between tags now says so, naming
the text and the element, where it said "a fragment, perhaps": an `<option>English</option>`
or a comment beside a tag gets a reason that points at what was written. See
[a layout as an object](docs/frontend/objects.md).

### A value that reads state needs no arrow

`width={state.gap}` is a binding, the same as `width={() => state.gap}`, and compiles to the same
bytes. So are `visible={state.count > 0}` and a template string that reads state, and so is
`text={item.name}` in a row. It used to be `ReferenceError: state is not defined`, so nothing that
built before means anything different now. The arrow still works. A handler keeps its own, because
there it says something else: that the code runs when the event does. Writing one without gets a
message that says so.

It is done on esbuild's output, with the acorn the analysis already has: a value of an element
that reads state is wrapped in the arrow before anything is evaluated, so imports and layouts written
as objects are covered and nothing after it has a second case. A name the code around it declared is
left alone, so a component with a prop called `state` means its prop. It costs a compile about 14 ms.

### Formatting and timers

What an interface needs that QuickJS lacks. **`format`** -- `number`, `percent`, `bytes`,
`duration`, `date` by pattern, `relative` -- because there is no `Intl` and
`toLocaleString` gives none of grouped digits, sizes, dates or "5 minutes ago". It is
JavaScript compiled into a module only when the module names it, like the colour helpers,
and defined while a file is built as well, so a top-level `format.bytes(2048)` gives at build
what it gives later.

**`setTimeout` and `setInterval`**, on the frame clock: a callback goes off on the loop
thread at the first frame at or after its time and runs as a handler does, writing state
and calling commands. An idle window waits no longer than the next timer, so one goes off on
time -- measured at 400 to 412 ms for 400 ms. A timer belongs to the layout or state.ts that
started it and goes when that is unloaded or reloaded. A value binding, a computed value and
a file's top level cannot start one, each refused with why. Not `std::async`: QuickJS and the
state it touches live on one thread, so a callback anywhere else could only queue itself
back to the loop.

Two things found on the way. The helpers a file is built with were evaluated in front of its
source, so an error inside one named a line of the file -- `main.tsx:239` of a file three
lines long; they are a script of their own now. And the frame's clock for run-time code ran
inside its GUI part, so an application with state and no window had no timers and no table
checks; it runs first now. See [functions](docs/frontend/functions.md#formatting).

### An error while a file is built names the line it was written on

A layout that threw while it was built was reported at a line of esbuild's bundle, not of
the file: the types are gone from that, the imports are pasted in above, and the compiler
wraps the lot in a function. A component throwing on its third line said `main.tsx:5`. A
helper imported by `throws.tsx` that threw on its own line 4 said `throws.tsx:4` -- a
comment, in the wrong file -- and its caller on line 13 said `throws.tsx:10`. A theme, a
mesh and a state.ts that threw said no line at all: the message alone, the stack left off.
Every frame of the bundle is moved back through esbuild's source map now, to the file, line
and column it came from -- `throws-helper.ts:4:10`, then `throws.tsx:13:24` -- and what no
file accounts for, the wrapper's own lines, says "the engine's own code", as a run-time
module's frames do.

The rest of a kind. A mesh the geometry helpers refuse -- an edge written as a word, a
state with another's triangle count -- shows their frames and then the mesh's line that
asked. A computed value that failed when it was run once at build, which the engine catches
to say which value it was, says where too: the compute's own frame, and none of the
evaluator's around it. A row template that threw used to say only that it had; it says what
it threw. And a theme's colour helpers are evaluated as a script of their own, as a
layout's have been since the timers: written in front of the theme, they had put every line
of it off by their length, which nobody saw only because no line was printed.

### `rda init`

`rda init [<dir>] [--python | --node | --csharp | --cpp] [--force] [--no-build]` writes the
files a project starts from, where a newcomer used to type five or six of them out of the
tutorials before anything could be seen. With no language it makes an interface alone --
`res/state.ts`, `res/themes/app.ts`, `res/layouts/main.tsx`, the editor's `tsconfig.json` and a
`.gitignore` covering what the build generates -- which `rda prototype .` opens; a language
flag adds that language's backend (`app.py`, `app.mjs` and `package.json`, `Program.cs` and a
`.csproj`, or `main.cpp` and `CMakeLists.txt`). The content is the counter from the first
tutorial, with a command the backend answers. The directory defaults to the current one, is
made if it is missing, and names the project.

It refuses to overwrite: if any file it would write exists it changes nothing, lists them and
says `--force`. Unless `--no-build` is given it ends by running what `rda build` runs, so
`res/layouts/rda.d.ts` exists and the editor works at once; without esbuild the project is
still made, and it says so. Then it prints the next steps for the language. In Node and C# the
handler is bound before the interface is loaded.

It is in `rda`, not `rda-preview`, and adds no dependency: the templates are real files under
`Cli/templates/`, embedded with `rda_embed_js` like the JavaScript preludes. Tests make each
of the five kinds and build them with a separate `rda build`, and check the refusal.

### The command line, for the person who is not yet sure what is wrong

Seven things that three people stumbled on in a usability test, and two that were in the way of
the next three. Nothing here crosses the C ABI.

`rda build .` works out the language. It wrote no state module unless it was told `--python`,
`--node` or `--csharp`, and printed `3 written` as though that were all of it; the tutorial said
the opposite. It now reads the project root -- `app.py` or any `*.py` is Python, `package.json` or
any `*.mjs` is Node, a `*.csproj` or an existing `state.cs` is C#, a `CMakeLists.txt` is C++ and
writes nothing because CMake generates that one -- and says what it found on its first line.
Several matches write each of them, not a guess between them. No match writes none, in one line
that names the flags. A flag is still the whole answer: nothing is looked for when one is given.
`rda prototype` and `rda shot` build with no detection, so a picture never leaves a `state.py` in
a project it was only asked to draw.

A misspelt element or property is said at build time. `<buton widht={1}>` and
`<checkbox text="x">` compiled, exited 0, and were skipped by the loader with a line in a log.
They are now warnings with what was meant, from edit distance over that element's own properties
and the common ones (`widht` is `width`, a swap counting as one edit): `<checkbox> 'root/c' has no
property 'text'; did you mean 'label'?`. A short table covers the guesses a distance cannot reach --
`text` on a checkbox, `gap` on a stack, `className` -- and a name with no close match lists a few
that are valid. The structural keys the compiler reads itself (`id`, `children`, `row` on a list and
a tree) and the signals it writes are never checked, a component is expanded first so its own props
are not, and an element written as an object is checked the same as one written as JSX, because it
goes through the same function. `rda build --strict` and `rda layout --strict` make each an error,
and `--strict` compiles every layout again: a warning exists only while a layout is being compiled,
so skipping the up-to-date ones would pass a typo that had already been reported. Checked against
the layouts under `RuntimeTests/script`, the templates, and the tsx in `docs/` and `skills/` that
compiles on its own: no valid name is flagged.

`rda doctor [<project>]` prints what was found, a line each, `ok` or `PROBLEM` with what to do:
rda and its engine version, esbuild and its version, `rda-preview` beside it, whether a Vulkan
loader opens -- by `LoadLibrary` or `dlopen` at run time, so `rda` still links no GPU code -- and
python, node and dotnet, with where `import rda` lands for that python. Given a project it adds
`res/`, the declaration, themes, layouts, the backend it detected, and whether anything is
missing or older than its source or than `rda` itself. It exits 1 when something the project or the
build cannot do without is missing, and not for a missing GPU on a machine that only builds.

`rda dev [<project>] [--layout <name>] [--dry-run]` is the edit loop as one command: build, start
the backend that was detected (`python app.py`, `node app.mjs`, `dotnet run`), and watch
`res/state.ts` and what it imports -- the one thing a Debug engine does not reload by itself. A
change builds again and restarts the backend; a build that fails leaves the running one alone. The
backend's exit code is `dev`'s, and Ctrl+C ends it. A C++ project is its own program, so it says
to use CMake; with no backend it falls back to the prototype. `--dry-run` prints what it would do.
The launching is `Forward.cpp`'s, extended to a child that can be waited for and started again, and
inside the same job object, so a backend is never left open by a tool nobody is waiting for.

`rda add screen <name> [<project>]` writes `res/layouts/<name>.tsx` and the route to it. With
`export const routes` in state.ts, one entry is inserted in the layout the file has, aligned the way
its author aligned it. Without, a declaration is added at the end with an entry for each screen
already in `res/layouts/` -- imported components are not screens -- `main` first, because the first
route is where the app opens, and the command says what the backend changes in its language,
`open_routes(ROUTES, ...)` where it had `load_interface`. The text is edited, not rewritten, so
comments and formatting stay, and what was written is read back the way the build reads it and put
back if it would not load. A file in a shape that cannot be edited safely is left alone and the
lines to paste are printed. The layout or the route already existing is a refusal.

`rda widgets [<name>] [--json]` is the widget schema readable: every widget with its line, or one
widget's own properties with their types, whether they can be bound, their docs, and its events.
People were searching the generated `rda.d.ts` for what `hAlign` takes. `rda elements` is the same
command, under the name it had while this release was being written.

`rda shot --click <id>` takes a widget's id or the end of its path (`add`, `root/buttons/add`) as
well as `X,Y`, and so do `--hover` and `--press`: the pointer goes to the centre of that widget
once the layout is built. An id that matches nothing, or the end of several paths, is refused
before a window opens -- the ids are in the blueprint -- naming the choices. And when a project has
commands, `shot` and `prototype` now say once that no backend is attached, so `rda: commands.x()`
does not read as the command having run.

### Editing in the window

A container marked `editable` can be built with the mouse in `rda prototype`. It gets an outline,
and a panel opens in a window of its own that stays on top and can go anywhere: drag a widget in,
or click one and then click where it goes, click one to see its properties, type over them, and F2
hides the lot so that what was built can be tried. Everything is written into the
`.tsx`, and hot reload is what puts it on the screen, so what the editor shows is the file and
there is nothing to save. See [editing in the window](docs/setup/prototyping.md#editing-in-the-window).

An edit is the smallest change to the text that says what was asked, because the same file is
edited by hand: `Layout/JsxSource.h` finds where each element and attribute is and replaces only
that, in the file's own indentation and line endings, so comments and blank lines come through. It
does not understand a layout, only the shape of JSX, and the compiler still has the last word: a
bad edit is a compile error with the running interface left alone. An element is found by the id
path in the blueprint, in the layout's file or what it imports, including what a component in
another file returns. A value computed from state is shown and not offered for typing over, and a
layout written as objects says that it has nothing to change.

It needed nothing from the engine. Drawing from `onUpdate` already lands on top of the interface,
`claimPointer` already keeps the pointer from what is underneath, and `wantAnchor` already says
where a widget ended up. `rda shot --edit` draws it too, and lets a script drive it.

### `rda widgets`

`rda widgets stack` prints what the documentation says about a stack as well as its properties: the pages under
`docs/frontend/elements/` are embedded and a widget's own section is found by its heading, so there
is one description to keep true. With an example from the same snippets the editor drops in, and
the common properties by name, with `rda widgets common` for the rest of what they say.

### The command line, a file per set of commands

`Cli/` is `core/`, `commands/` and `preview/`. A command registers itself from its own file and CMake
compiles whatever is in `commands/`, so adding one touches nothing shared: no table in `main.cpp`,
no header, no source list. One command reaches another with `runCommand`, by name.
`rda --check-commands` fails on a file that registered nothing, two commands with one name, or a
usage that does not say how it is typed, and it is a test. `Cli/README.md` is how to add one.
`Commands.h` is gone, and `rda-preview` now holds every command.

### Running with nothing on the screen

`RDA_OFFSCREEN=1` runs an application with its window never shown, in any language and with no code
in the application. The binding tests set it, so they no longer open a window on the desktop each
time, and with nothing competing for the screen they can run at the same time. A new binding gets
it for free, and `rda_binding_test` in `bindings/tests/CMakeLists.txt` is now one line per language.

`config.offscreen` already hid the window, but only after opening it, so it flashed. A window can
be created hidden now, with `WindowStyle::hidden`, which `rda shot` gains too. Two other things put a
hidden window back on the screen, both found by reading what they call: a window with a position
was shown by the code that placed it, and maximising calls `ShowWindow` in this GLFW. So a hidden
window remembers being asked to maximise, minimise or go fullscreen, and answers with that.

`GLFW_VISIBLE` is stated for every window now. A placed main window used to leave it off for the
next window opened, which would never have appeared.

### Shortcuts

A layout can declare its own key chords, which until now every application had to read from
its backend. `shortcut="Ctrl+S"` on a `<button>` presses it -- the same `onClick` a click
runs, so a menu entry and its shortcut are one widget and cannot drift apart -- and on a
`<checkbox>` toggles it. `<shortcut keys="F5" onPress={...}/>` is a chord with no button,
drawing nothing and taking no room.

A shortcut is live while its widget could be reached: drawn, scrolled out of view, or
inside a `<popup>` that is shut, because a menu entry's shortcut has to work with the menu
closed. It is not live when hidden, on a tab that is not showing, in a `<list>` row, in a
screen on its way out, or behind an open blocking popup -- the interface behind a dialog is
behind it for the keyboard too. A focused text field keeps plain keys and its own Ctrl
editing chords; Ctrl+S, F5 and the rest go past it. Two live widgets with one chord: the one
drawn last wins, and the log names both, once.

The generated `rda.d.ts` types a chord, so `"Ctrl+Shfit+S"` is an error in the editor that
suggests `"Ctrl+Shift+S"`. A chord may be a binding, for a keymap kept in state; one that
does not parse is reported with the widget it was on.

`rda shot --key <chord>` presses a chord before the picture, repeatable, and
`rendeerSyntheticChord` is the same thing for C++. `Probe::press` does it without a window.

### Screen readers

An interface drawn by this engine is pixels to the operating system, so a screen reader
found one blank window. It now finds a tree. Every widget describes itself -- a button by
its text, a checkbox by its label and whether it is ticked, a slider by its value and range,
a tree row by its text and whether it is open -- and on Windows a UI Automation provider
speaks that tree to Narrator, NVDA, JAWS and the inspection tools. A screen reader's user
can press, toggle, set, open, shut and choose, and each of those goes through the path a
hand would take: pressing Save from a screen reader runs its `onClick`.

On Linux the same tree is spoken to AT-SPI -- what Orca reads -- over D-Bus, by the engine
itself rather than through ATK and GLib. libdbus is loaded when a window opens, not linked,
so nothing new is needed to build and a machine without it runs as before. It registers
with the desktop's registry only while assistive technology is on, and builds nothing
until a client other than the registry asks. Checked against a real registry and bus:
the tree, every action, and the events. A tree row reports its depth as a level on both
systems. macOS is not done: the engine does not run there.

`live="polite"` or `"assertive"` on any element has a screen reader read it out when what it
says changes, or when it appears saying something, without the focus going there: a status
line, a count, an error under a form. On a container it is everything inside it as one
sentence. UI Automation's LiveSetting and LiveRegionChanged on Windows, AT-SPI's
Announcement on Linux.

A list's rows are all in the tree, not only the ones it has widgets for: a row scrolled out
of view is described from the table -- named by the first text its template gives it, the
same name it has on screen -- marked off-screen, and scrolled into view by a screen reader's
ordinary command. Up to two thousand rows of one list, around the view, so a screen reader
can count it and read it from the top.

An open dropdown's options are items under it: the chosen one selected, the highlighted
one where the reader is, so each arrow is read out as it moves, and any of them can be
chosen from the screen reader.

Text fields are read caret by caret. A field tells the screen reader where its caret and
selection are, where its lines start and where each character on screen is, and the screen
reader walks it by character, word and line, reads what is typed as it is typed, and can
move the caret and select. UI Automation's text pattern on Windows, AT-SPI's Text on Linux,
with what a word is decided once, in shared code both use, and tested on its own.

`alt` names what has no words of its own -- a slider, an icon, a button holding only a
picture -- and makes a container a named group. Nothing else needs writing.

It costs nothing until something asks: the tree is built only once a client has asked the
window for it, and is handed over as a snapshot at the end of a frame, so a screen reader
reads on its own threads and the frame never waits. Actions come back as a queue applied
during the next walk, where the widget is known to exist. Focus, toggles, values, opening
and shutting, selection and structure are all raised as events. `rda shot --a11y tree.txt`
writes the tree as text, which is also how to read it on Linux and macOS, where nothing
speaks it yet.

### A tree

`<tree>` is a `<list>` that shows some of its rows. The table holds every node in reading
order -- each one, then everything under it -- with a `depth` column, so nothing is nested
and fifty thousand nodes are fifty thousand rows and about thirty widgets. It takes the same
`row={(node) => ...}` template and is built on the same pool, scrolling and bar; what it
adds is which rows are shown, an indent and a disclosure arrow, a chosen node, and the
keyboard.

What is open is the tree's, remembered by each node's key so it survives the table being
refilled; `openDepth` says how far it starts open. `onSelect`, `onActivate` (double-click or
Enter), `onExpand` and `onCollapse` hand over the key. A row with `hasChildren: true` and
nothing under it has an arrow and starts shut, and opening it is the backend's cue to insert
its children -- which is lazy loading with no new mechanism. Up, Down, Home, End; Right
opens and then steps in, Left shuts and then steps out; Space toggles.

`<list>` gained the three questions a tree needed answered differently -- how many rows are
shown, which table row a shown row is, and where a row's widgets go -- with defaults that
are exactly what it did before.

### A data grid

`<grid of="files">` with `<column field="size" title="Size" width={90} hAlign="end"/>`
children shows a table as a table: columns under titles, a click on a title sorts by it and
a second click turns it round, the line between two titles drags to resize, and a column
written `width="fill"` takes what the others leave. It builds no widget per cell -- a cell
is the table's value drawn as text, a number without trailing zeros or to `decimals`
places, a bool as a tick -- so a hundred thousand rows by ten columns is one widget, and a
row that needs a button in it is still a `<list>`. `selected`, `onSelect`, `onActivate`
(double-click or Enter) and `onSort` (`"size"`, or `"-size"` descending); Up, Down, Page Up,
Page Down, Home and End move the choice. Wider than the grid, the columns scroll sideways,
with a second bar and Shift on the wheel. A screen reader reads a row as one sentence with
every column titled. That makes 27 elements.

Sorting and filtering are a view rather than a copy, and `<list>` has them too: `sortBy`,
`sortDescending`, `filter` and `filterColumns`. The backend keeps one table in its own order;
what is shown is a list of row numbers, worked out again when the table or one of those
changes. Text sorts naturally -- `file2` before `file10` -- and ignoring case, numbers by
value, bools false first, and ties keep the table's order. The filter ignores case and
looks in every text column, or the ones named; a number column named is searched as it is
shown.

A focused `<grid>` or `<tree>` keeps Home, End, Page Up and Page Down from the shortcuts, the
way a focused button keeps Space.

### Choosing several

`multiSelect` on a `<grid>` or a `<tree>` lets the reader choose more than one row. Ctrl+click
toggles one, Shift+click reaches from the last one clicked, Ctrl+Shift+click adds the reach;
Shift with the arrows, the page keys, Home or End reaches from the keyboard, Ctrl with them
moves without choosing, Ctrl+Space toggles and Ctrl+A chooses every row shown. The chosen
keys are `selectedKeys`, one per line, reported by `onSelectionChange` -- a signal holds one
value, and a line is what a key never contains. `selected` stays one row, the one the
keyboard is on, outlined so it shows whether or not it is chosen. Rows are chosen by key, so
a sort or a node opening keeps them, and a reach is worked out from where the rows are now.

A screen reader is told the list is multi-selectable and can add a row, take one out, choose
all or none: UI Automation's Selection pattern -- which a list, a tree and a tab strip now
have, multi-selectable or not -- and its added and removed events on Windows, and AT-SPI's
Selection with the MULTISELECTABLE state on Linux.

`rda shot --click Ctrl+X,Y` clicks with Ctrl or Shift held through the click, which a chord
cannot do; `rendeerSetSyntheticModifiers` is the same for C++.

### Dragging and dropping

Any element can be picked up and any element can be a place to put it. `drag="task-17"`
says what it carries -- a string, because what lands on the other end is a handler and a
handler is handed a value -- and in a `<list>` row it is a binding, so each row carries its
own. `onDrop={(id) => ...}` on a target receives it. `dragKind` and `accepts="task|note"`
keep a task off a tag. `onDragOver(boolean)` reports the arrival and leaving the way
`onHover` does, and `state.rda.dragging` and `state.rda.dragKind` say what is travelling,
so every place it could go can light up before the pointer gets there.

Nothing travels until the pointer has moved a few units with the button held, so a click
on a draggable card is still a click. Once it moves, a figure the size of what was picked
up follows the pointer with its text on it, the target under it is outlined in the focus
ring's colour, the click is cancelled, and Escape puts it back. A press something takes as
a drag of its own -- a slider, a text field -- stays that thing's. Nothing behind an open
blocking popup or in a screen on its way out is a target.

Files from outside: `onFiles={(paths) => ...}` on any element, paths one per line, to the
deepest element under the drop that asked. A backend's own drop queue still sees every
drop.

`rda shot --drag X1,Y1,X2,Y2` carries something across, `--hold` pictures it mid-air, and
`--files "a;b"` drops files where the pointer is. `Probe::drop(from, onto)` does it with no
window.

### A dropdown finds what you type

A focused `<select>` takes the first letters of a choice: `bud` goes to Budapest, and the
same letter again steps through everything beginning with it. Shut, it opens on the match
rather than choosing it, so Enter still decides and Escape still changes nothing. A pause of
about a second starts again. While it has the keyboard, plain keys are typing as far as
shortcuts are concerned, the same as in a field.

`rda shot --type <text>` types, in order with `--key`; and `--key` now presses a key the
way a hand would -- `--key Tab` moves the focus -- rather than only reaching shortcuts.

### Floating panels

A press on a floating panel brings it to the front, and a saved layout keeps the stacking.
Two smaller things were wrong in the same place: every floating `<dock>` a layout declared
started at the same rectangle, so two of them were one panel as far as anyone could see --
they are stepped down and across now, and `x`, `y`, `w` and `h` on a `<dock>` say where one
starts. And a press on one panel's body went on looking down the stack, so it could pick up
the title bar of a panel *behind* it and drag that instead.

### A list builds the rows it shows

A `<list>` or `<tree>` builds as many row widgets as its view shows, when it is first drawn,
and more on the frame its view grows -- where it used to build forty when the layout loaded,
which was too many for a short list and too few for a tall window, which then said so in
the log. Built mid-frame and drawn the same frame, so a row never arrives late. `poolSize`
still builds a number ahead of time for a list that would rather. A binding in a row that
reads only state, not the row, is applied the moment its row is built.

### Chinese, Japanese and Korean

An interface can show and take CJK text without naming a font or doing anything else. A
character none of the named faces has is looked for in the system's own -- Microsoft YaHei,
Yu Gothic, Malgun Gothic and JhengHei on Windows, Noto CJK or WenQuanYi on Linux -- loaded
one at a time, the first time a character needs one, in the order the system's language
says, because the same Han character is drawn differently in each. A named face is always
asked first; `RDA_SYSTEM_FONTS=0` leaves the system's alone.

On Windows the input method composes at the caret: the word in progress is drawn there,
underlined, with the method's cursor in it, and its candidates open under it. The method is
on only while a text field has the keyboard, so over anything else a letter is a shortcut
again, and while it composes, the keys it is using never reach the interface. On Linux,
X11's input methods hand over finished text through their own window; Wayland's do not
reach GLFW yet.

`rda.exe` now runs with UTF-8 as its code page, so a non-Latin argument -- `--set
note=北京`, a path with Japanese in it -- arrives intact instead of as question marks. `rda
shot --compose <text>` pictures a field mid-word.

### What happens to a viewport: resized, or the pointer

A drawing could be shown and neither touched nor kept the right size. A graph could not be
panned, a plot not zoomed, a model not orbited, because nothing about the pointer reached a
backend; and when a splitter moved or a panel beside it closed, the drawing stayed made for
the old size -- stretched, or cropped -- because nothing said the size had changed, and the
only way to find out was asking the loop every frame.

Now a viewport reports what happens to it. **Every** viewport reports its size, from the
first frame that places it, which is also when a backend can make its first drawing at the
right size. And a viewport a backend listens to -- `rda_viewport_listen(name, 1)`,
`rda.listen_viewport(name)` -- reports the pointer: a press over it is its own until the
button comes up, wherever the pointer goes meanwhile, and the wheel stops there instead of
scrolling what is around it. One nobody listens to lets the pointer through, as before.

What happened is *summed* rather than queued -- movement and the wheel add up, a button
that went down or up and a size that changed stay recorded, the position and the size are
the latest -- because a pan wants how far the pointer travelled since the drawing was last
made, a redraw wants the size now, and a backend that was busy drawing must not come back
to a backlog. `rda_viewport_take` takes it; `rda_viewport_wait` sleeps until there is
something to take. Neither crosses to the loop thread, so a thread can wait for the life of
the window and an idle window stays idle. Two busy viewports are answered in turn.

Only the visible part takes the pointer, so a viewport half scrolled out of view does not
steal presses from what is drawn over the rest. And a viewport drawn with an empty list is
transparent, so one laid over a `<stream>` is the way to make a picture the GPU drew
draggable. Python has it (`listen_viewport`, `viewport_event`, `wait_viewport`,
`ViewportEvent`); Node and C# do not yet. C++ uses `RDA::viewports().events()`. Ten new test
cases cover the summing, the waiting, the shutdown that lets a waiter go, and a resize that
is neither lost nor allowed to move the pointer.

### A frameless window's frame

A window with no OS frame could be moved by its bar and nothing else: its edges did not
resize it, a double click on the bar did nothing, and a maximised one could not be dragged
at all. All three are the engine's now, so a layout that draws its own title bar gets the
frame's behaviour without writing any of it.

The edges are a band five units deep inside the window, longer at the corners. Over it the
pointer shows which way it would resize -- GLFW 3.3 has no diagonal resize cursor, so those
two are drawn -- and a press there resizes until the button comes up, from where the button
went *down* rather than where the pointer had got to by the frame that heard of it, so a
quick drag does not leave the edge trailing the hand. The interface hears nothing of the
pointer while it is on the band, so the close button in a corner does not light up under a
resize; a drag that began in the interface keeps going if it wanders over the band. The far
edge holds, minimums and maximums hold, and a window with no minimum still stops at 160 by
100. A double click on a `dragWindow` region maximises or puts back, through the same path
the OS button takes, so `state.rda.maximized` hears of it. Dragging the bar of a maximised
window restores it under the pointer -- at the same fraction of its width, the same depth
into the bar -- and carries on dragging.

On Windows the platform does all of that itself, and snaps. A frameless, resizable window
is given a frame's styles (sizing border, caption, maximise box) with WM_NCCALCSIZE taking
the whole window as client area, so nothing of a frame is drawn; and WM_NCHITTEST answers
what a frame would -- the resize edges, HTCAPTION wherever the pointer is on a
`dragWindow` region with nothing drawn over it, HTMAXBUTTON over the button a layout marks
`maximizeButton`. A press on the bar is then Windows' own caption press from the first
moment: dragging to a screen edge snaps to half the screen, to the top maximises, a snapped
or maximised window dragged away restores under the pointer, a double click maximises, a
right click opens the system menu, and on Windows 11 resting on the maximise button offers
the snap layouts. The pointer over the bar and the button is handed back to the interface
as the ordinary pointer it is, so the button still lights up and clicks and the interface
keeps knowing whether the pointer is on bare bar. Maximised, the client area is inset by
the frame Windows hangs off the screen. Elsewhere the GLFW paths above are what there is.
Ten test cases cover which edge a point is on, what a drag makes of the rectangle, and the
drawn cursors; the Windows half was checked against the window's own rectangle while
dragging it into each edge.

A stream that changed size, or was closed, destroyed its surface on the spot -- while the
frame in flight could still be drawing it and the GUI still held a descriptor set for its
sampler (VUID-vkDestroySampler-sampler-01082, every time a window was resized with a
stream in it). Such a surface is retired now, as a replaced `<image>` already was, and
destroyed once the GPU is done with it.

### The engine in layers

A layout, a backend and the C ABI do not change at all, and a C++ application changes only if
it reached past the interface into the engine; those calls are listed below. The headers keep
their names and `rendeer` is the same library. What changed is where the files are and what may
include what. The engine is now twelve layers, bottom to top -- `foundation`, `sync`, `state`,
`formats`, `vulkan` and `platform` side by side, `engine`, `renderer`, `ui`, `blueprint`,
`runtime`, `toolchain` -- each a directory under `RendeerAlpha/` and each an object library
compiled against its own headers and those of the layers below, so an include that points
upward does not compile. See [the layers](docs/architecture/layers.md). It started as a review
of the whole engine, and [restructure.md](docs/architecture/restructure.md) is what it found.

Getting there meant cutting every edge that pointed the wrong way, and each cut is a seam a
lower layer reaches through rather than an include. Anything asks for a frame through
`Core/Wake.h` instead of the public facade. The style registry reads a theme through a slot
the blueprint layer fills. Hot reload reaches the compilers through `Layout/Rebuilder.h`, so
the running engine includes no compiler header. The renderer draws into a `RenderSurface`,
an interface it declares and a window implements, instead of reading a `Window`. A list asks
the runtime to bring a row up to date instead of evaluating its bindings itself. And the
layout loader is two things now: the `Instantiator`, which builds widgets from a blueprint,
and the runtime that binds them.

The large files are split by what each part is for. `RendeerAlpha.cpp` is six files under
`runtime/src/Runtime/`. `Widget.cpp` is one file per widget, and `Gui.cpp` one per concern.
`Docking.cpp`, `ScriptHost.cpp`, `FontAtlas.cpp`, `StateSchema.cpp`, `Cli/rda.cpp` and
`Gui::textField`'s 880 lines are each split along their own seams. `Window` gave up its
surface, swapchain and FrameBuffer to a `PresentationSurface` in the engine layer, and the OS
window itself to an `OsWindow` in the platform layer. GLFW is private to the platform and
Vulkan layers now, so nothing above them can reach for it. Every split was checked by rendering
a set of interfaces before and after and comparing the pictures and accessibility trees byte
for byte.

`Gui` is a facade now rather than one class spread across files. Seven classes own what it was
made of, each with its own state: `Canvas` for drawing and text, `Interaction` for ids and what
the pointer is on, `Focus`, `Shortcuts`, `DragDrop`, `A11yCollector` and `WindowChrome`. What one
needs from another is passed in, and `Gui`'s public interface did not change.

`FontAtlas` no longer touches Vulkan: it bakes into a bitmap of its own, so text can be measured
with no device, and `GlyphAtlasTexture` in the renderer owns the image. A texture waiting to be
freed is in a `RetireQueue` and a window's frames in flight are a `FrameSync`, so each of those
rules is written once. What changed between two accessibility trees is `AccessibleDiff`, worked
out once for UI Automation and AT-SPI, which keep only what to say about it. Every event a
screen reader was sent, recorded on both platforms before and after, came out the same.

`rda` no longer needs a GPU. `prototype` and `shot` are a program of their own, `rda-preview`,
beside `rda`, which forwards the two commands to it. So the tool that builds a project imports
nothing but `KERNEL32` and the C runtime on Windows.

A C++ application that called any of these has something to change:

- `Window::getGLFW()` is gone, and GLFW cannot be included above the platform layer. Use the
  window's own methods, which now include the clipboard.
- `InitGlfw()` and `endGlfw()` are `initWindowSystem()` and `endWindowSystem()` in
  `Platform/WindowSystem.h`.
- `Swapchain::imageInFlight()` is replaced by `claimImage()`.
- `FontAtlas::texture()` and `FontAtlas::uploadPending()` are on `GlyphAtlasTexture`, which is
  given the atlas.

The toolchain evaluates JavaScript one way. Seven copies of setting QuickJS up and three ways
of turning an exception into text are one `JsContext`, keeping the two differences that were
real: the deeper stack a layout is compiled with, and the wording of two messages. The
preludes, the analysis and the module a layout's code ships in are `.js` files now rather
than C++ raw strings -- one of them split in three to fit under MSVC's limit -- embedded at
configure time by `rda_embed_js`.

**A widget kind is one record.** How each element is built, which of its properties a binding
can drive, and which events it has used to be three chains of `dynamic_cast` in three files,
and they had drifted. `Layout/WidgetRegistry.h` has one record per element instead. A binding
finds its setter once, when the layout loads, rather than comparing names whenever a value
moves. A test now fails the build if the schema and the registry disagree. What the drift had
been hiding:

- About twenty properties type-checked as bindable and were ignored at run time, with a
  warning: `hAlign` on a label or a button, everything on `<scroll>` and `<splitter>`,
  `fit` on an image, `barHeight` on tabs, a list's `rowHeight`, a grid's `headerHeight`, a
  column's `field` and `hAlign`, a dock's `title`, `closable` and `variant`, `anchor`, and
  more. They follow state now.
- What is only read once -- the table a list shows, a text field's mode, where a dock panel
  starts -- is marked constant in the schema. `rda.d.ts` offers no binding form for it, and
  the compiler refuses one with the reason, rather than compiling a binding that does nothing.
- The loader and the binding runtime parsed the same words separately, and disagreed:
  `"auto"` was an alignment to one and not the other, and a bound `arrange` the stack did not
  know turned it vertical. One parser now.
- A typo inside a list's row template was never reported, because nothing checked a template's
  nodes. It is reported once, when the layout loads.

Fixed on the way:

- **A second window could not copy or paste and wore the built-in look.** Only the first got the
  clipboard and the theme.
- **`rda_set_title` went around the window,** so a screen reader kept the old name.
- **A `Router` going away left back and forward bound to it.**
- **Two panels closed in one frame lost one.**
- **A synthetic press reached the first window only.**
- **A stream's lock was held through a whole GPU upload,** so a producer on another thread asking
  `wanted()` waited as long.
- **The logger had no lock,** with the loop on its own thread.
- **A property error on a `<list>` with a row template aborted the compiler** with a QuickJS
  assertion rather than reporting it.
- **A theme, a mesh or a `state.ts` that recursed without end took the compiler down.** They are
  compiled on the same large stack a layout is now, so it is an error with a line.
- **On Linux, a program started by a relative path could lose its own assets.** For the main
  program `dladdr` reports `argv[0]`, which stops meaning anything once the program has changed
  directory, as `prototype` and `shot` do.
- **The Python, Node and C# state modules wrote a text default containing a newline as broken
  code.** One writer escapes for every language now.

Smaller: three unused vendored libraries are gone (shaderc's sources, nlohmann/json and an OBJ
loader, about 220 files); the command queue and the dropped-files queue in the C ABI are one
`Core/Mailbox.h`; four copies of "did this file change" are `Core/FileStamp.h`; and
`RDA_LOG_SUCCES` is spelled `RDA_LOG_SUCCESS`, with the old name kept as an alias.

### More than apt

`scripts/linux-bringup.sh` knew Debian and stopped everywhere else with Debian's package names.
It now finds the package manager by the command it provides -- apt, pacman, dnf, zypper, apk or
xbps -- and describes each dependency by what it is, with a table of what each manager calls
it, its own check for what is installed and its own install. On anything else it says what is
needed by purpose. `--check` prints what it found and what it would run, and does neither.

pacman is never asked for a sync on its own, because a partial upgrade is unsupported on Arch: a
stale database is reported, with `sudo pacman -Syu` as the advice. And shaderc is searched for as
`shaderc_shared` first on Linux, because Arch ships that beside a static `libshaderc.a` that the
plain name would have stopped at. Debian and Ubuntu have no such file and find what they found.

### A value binding could write

`text={() => state.count = 1}` built and ran. The rule that a value binding may not write was
kept past the grammar and at run time, but the grammar itself accepted the plainest spelling of
it, so the same write was refused or not depending on how it was written. It did harm when it
got through. A value binding runs again when what it reads changes, and the signal it stores
into counts as read, so that line undid every handler's write to `count`, leaving a button that
looked dead, and `state.count = state.count + 1` climbed every frame without end. Every spelling
is refused now, with the words the other path already used. A layout that relied on this stops
building and says where: the write belongs in a handler.

### A size on a child of a panel

`<panel><button height={30} /></panel>` drew a button as big as the panel. `width` and `height`
were read by a stack and by nothing else, so a widget placed by its own `x` and `y` used `w` and
`h` or filled its parent, and the size that was written was dropped with no message. They work on
a child of any parent now, with their bounds, and bound values too: a number, `"content"`,
`"fill"`, and `"fill-40"`, where a fill is all of the room because there is nobody to share it
with. A `w` or an `h` still wins, and a pair of anchored edges still decides its own axis.

This can move something that was already drawn. A size written on such a widget and quietly
ignored until now takes effect, including on a layout's root, which `height="content"` no longer
stretches to the window.

### Any font size

`fontSize: 16` in a theme drew at 15, and a warning named `GuiConfig::fontSizes`, which only C++
can reach. A size the atlas does not hold is added the first time it is drawn now, and its glyphs
are baked as they are met, so what a theme writes is what it gets, in every language. `fontSizes`
stays as a head start. The limits are 32 added sizes and 192 pixels tall; past either, text is
drawn at the nearest size and the log says so.

### Text that is not English, in a binding

`text={() => "Bună ziua"}` drew `Bunu0103 ziua`. esbuild writes every non-ASCII character as an
escape, and the binding parser took the letter after the backslash for the character, so only
text in a binding was affected and a constant beside it looked fine. The parser reads `\uXXXX`,
`\u{...}`, `\xXX` and a surrogate pair as the character they stand for now, in strings and in
template literals, and refuses an escape that is not one. esbuild is also told to leave the text
alone, so an error or an `rda dump` shows what was written.

### `Commands` is always in a state module

`from state import State, Commands` failed with an `ImportError` on a project that had declared
no command yet, and an `import` in Node stopped the same way. The Python, Node and C# modules
always have `Commands` and `call` now, empty when there is nothing to put in them.

### A file saved straight after a build

`rda build` could keep the old layout and say `0 written`. A source saved in the same clock tick as
its output was written has the same stamp, and only a newer stamp counted, so on Linux a script that
ran `rda init` and wrote a layout straight away got the template's blueprint three times in five. An
equal stamp counts as out of date now, which costs nothing: an output is always written later than
the source it was made from, by at least as long as a compile.

### A theme rebuilt every time on Linux

`rda build` recompiled every theme on every run there, whatever had changed, and said so as
`1 written`. A file time counts from its clock's own epoch, and with GCC's that lies in the
future, so every real stamp is negative; the newest mesh started at zero, outranked them all,
and made each theme look older than something. The tests for `rda init` found it, by asking a
second build to have nothing to do.

### A failed submit hung the engine

A frame whose `vkQueueSubmit` failed left a fence that had been reset and that nothing would
signal, so the next draw waited on it for good: a window that froze, with nothing said. Such a
frame cannot simply be dropped, because its semaphore and its acquired image are left waiting
too, and retrying would mean rebuilding all three on a device that has just refused work. So
the engine says which `VkResult` it got and stops the ordinary way, with `onShutdown` still to
come. That includes running out of memory, which now ends the application. A lost device
reported by acquire or present, rather than by the submit, still only logs, every frame.

### A reload could follow freed widgets

The loop walks each window, calls `onUpdate`, then ends the frame -- and `onUpdate` is where
a layout reloads and a route swaps. What the walk noted by pointer for the end of the frame,
the tree a screen reader is told about among it, could point at widgets the reload had just
destroyed. It took a screen-reader client and a press to show. The GUI counts widgets
destroyed since its walk began, and follows none of those pointers on a frame where any
were.

### What is under a floating panel is under it

A press on a floating panel was also a press on whatever docked widget lay beneath it.
Docked panes are painted first and floating panels over them, and a widget decides about a
press while it paints -- so a button, a list row or a listening viewport under a floating
panel took the click as well. While the pointer is over a floating panel the docked panes
now paint with the pointer elsewhere (`Gui::pushPointerAway`), the keyboard untouched, so a
field with the caret goes on taking keys. Not while something is already held: a drag that
began in a pane keeps going when it passes under a floating panel. `inert()` counts only
`pushInert`, so those panes are still described to a screen reader.

### A click on a floating panel's bar is a click

Two ways a floating panel docked itself without being dragged. A floating panel wearing a
`<header>` gave a press on its bar to the drag before the header's own buttons could take
it -- tiles already deferred to the header and floating panels did not -- so a close button
on the header carried the panel instead of closing it. And a drag that had not moved still
dropped on release, into whatever pane was under the pointer, so clicking a floating
panel's bar to bring it forward could dock it. A floating drag now has the 6-unit slop a
tab already had before anything travels.

### Documentation

`limitations.md` still said there was one font face and that text outside Latin draws as a
box, and the element index said nineteen. Fallback faces and glyphs baked on first use made
the first two untrue some time ago; the third was four elements behind.

The tutorials had fallen behind the same way. They still measured a layout in pixels, the
declaration in State and backends did not compile, and the backends beside it read a signal
it never declared. A top-level constant reaches a binding now while a component's local is
refused, which the text had as one rule, and it still said the program contains no
JavaScript engine. Every sample in the series has been compiled again, and the backends
checked against what the build generates.

## 1.2.0 — 2026-10-02

C ABI **1.6, unchanged**. Three things: a display scale, so an interface is the size it was
written to be on a monitor configured for legibility; hot reload for the theme and the meshes
it names; and the mesh compiler's first tests, which found two bugs on their way in.

### Before upgrading

**An interface is bigger on a scaled display than it was in 1.1** — which is the fix, and it
is visible. A layout unit is now as many pixels as the display says it is: at 125% a
`height={30}` button is 37 pixels tall and *looks* 30, where before it was 30 pixels and
looked three quarters the size it was written to be. Nothing in a layout changes.

Two consequences worth knowing before you upgrade:

- `state.rda.width` and `state.rda.height` are in layout units now, not pixels. A binding
  comparing them against a number the layout wrote itself was comparing against something a
  quarter larger on a 125% display; now the two are the same thing.
- `config.gui.uiScale = 1.0f` restores 1.1's behaviour exactly, for an application that
  wants physical pixels. `0` -- the default -- means ask the display.

### A picture of it, with no window

```bash
rda shot .                       # a .png of the interface
rda shot . --hover 208,27        # the same, with the pointer on something
rda shot . --frames 8 --every 35 --settle 0   # a transition, frame by frame
```

`rda prototype` opens a window and waits for somebody. This is the same command -- same
compile, same placeholder state, same route -- which builds every frame and never puts one
on screen. `config.offscreen` is the flag, `rendeerCaptureFrame` hands the pixels back, and
`Core/Png.h` writes them out.

That is for the two cases where watching is the wrong shape: a machine with nobody at it,
and anything that has to be *compared* -- a hover against a rest, a theme against the one
before it. `--size` and `--scale` together make one machine produce what another would,
which is also the quickest way to see what a display at 150% does to a layout.

- **The pointer can be put somewhere.** Hover and press are worked out from where it is, so
  `--hover X,Y` and `--press X,Y` capture those states; `rendeerSetSyntheticPointer` is the
  same thing from an application.
- **A still waits for the interface to arrive.** A window nobody presents runs the loop as
  fast as the machine can, so a naive capture catches a 160ms transition a millisecond in
  and the picture shows a colour one step from where it started. A sequence does not wait,
  since settling is usually the thing being looked at.
- `Core/Png.h` is a deliberately plain encoder -- no filtering, no compression -- so that
  writing a picture needs no dependency. Its test writes one and reads it back with the
  decoder vendored for reading other people's.

[Seeing it without watching it](docs/setup/seeing-it.md).

### Five things a picture found

Every one of these was found by looking at `rda shot` output, and every one is measured
rather than eyeballed -- the numbers are from the pictures.

- **A closed `<popup>` took room in its parent's flow.** It is an overlay: it draws over
  the interface from its own anchor, and the container holding it is only where it lives in
  the tree. A stack gave it a share of the space anyway, so a menu of three 24-unit buttons
  made the `height="fill"` sibling above it 76 units short -- open or shut, at every window
  size. Widgets answer `inFlow()` now, a popup answers no, and a stack and a scroll view
  both skip what is not in their flow while still painting it. Measured: a 100-unit popup
  took exactly 100 units from a filling panel, and now takes none.
- **A scroll view that scrolls both ways could not reach the end of its content.** The bar
  along the bottom takes a strip, and the vertical range was computed as though it did not
  -- so the bar sat over the last few units and nothing could scroll past it. The
  `horizontalBar` that `Widget.cpp` had worked out and never used is what it is for.
  Measured: scrolled to the end, the last row is whole, where it used to be cut.
- **A focus ring around a mesh-drawn widget was a rectangle**, which announced a shape that
  was not there. The mesh is now drawn a second time underneath, grown by the ring's width
  and in the ring's colour, so what shows is an outline that follows the silhouette exactly
  -- because it is the silhouette. Everything else keeps its rectangle.
- **A failure during start-up hung instead of stopping.** `RDA_RUNTIME_ERROR` throws, the
  entry point is `rendeerRun`, and nothing inside the engine catches -- so the C runtime
  did, which on Windows means a modal box nobody can see and a process that never exits.
  Found by asking for a display scale the font atlas could not hold: no window, no message,
  killed by hand. It reports and returns now.
- **And that atlas error blamed the wrong thing.** At 3x it said "ask for fewer sizes, or
  smaller ones" about sizes nobody had written; it names the scale now.

### A capture is the whole frame

`rda shot` captured from `onUpdate`, which runs before `Gui::end()` -- and `end()` is where
the focus ring is added. So every shot was the frame *minus* whatever the GUI adds after the
walk, which is a difference nobody would think to look for and which cost an hour of looking
for a ring that was being drawn correctly all along.

`AppConfig::onFrameEnd` runs once per frame, after every window has finished and before any
is drawn. That is the only moment at which a finished frame exists and has not been shown,
which is exactly what a capture wants.

Two smaller things in the tool, both for the same reason -- a picture has to be the same
picture twice: a shot with no `--hover` parks the pointer outside the window, instead of
inheriting wherever the real mouse happened to be, and `--scroll` puts the pointer in the
middle of the window when nothing else placed it, since a wheel only reaches what the
pointer is over.

### Fixed on the way there

- **An offscreen target had no format unless the viewport mode was `Widget`.** The surface's
  format is what every offscreen target is made of, and it was being settled inside the
  branch that builds the scene target -- so in `Fullscreen` mode it stayed
  `VK_FORMAT_UNDEFINED`, and `cacheGuiLayer` asked for an image of no format at all. Which
  is a validation error and a target that never gets made.
- **An offscreen target could not be read back**, since its image was created without
  `TRANSFER_SRC` usage. A copy out of one was a validation error that read nothing.
- **A window's size ignored the scale the application asked for.** `GLFW_SCALE_TO_MONITOR`
  scaled it by the *display*, so `uiScale = 1` -- physical pixels, on purpose -- still opened
  a window a quarter larger on a 125% display. The size is multiplied by the scale the engine
  settled on, and GLFW is no longer asked to multiply it again.

### A unit is a unit

Everything an application says is in layout units; pixels exist only inside the renderer. A
window's size, a padding, a font size, a mesh's corner radius, the rectangle a `<viewport>`
takes. Measured on a 125% display: a window asked for at 1280x800 opens as 1600x1000 pixels
and the interface inside it is handed 1280x800 units.

- **Text is baked, not stretched.** The atlas bakes at the scaled size and reports its
  metrics back in units, so 15-unit text on that display is a 19-pixel bitmap in a 15.2-unit
  quad. A glyph is snapped to a whole *physical* pixel rather than a whole unit, since
  rounding to the unit grid would put the bitmap on a quarter-pixel boundary and undo the
  reason for baking it larger.
- **An `.svg` is rasterised at the scaled size** and drawn into the unscaled box, so an icon
  has the pixels the display can show. So is a `<viewport>`'s offscreen target, or the 3D
  view would be the one soft thing on the screen.
- `GLFW_SCALE_TO_MONITOR`, so a window asked for in units opens at the right physical size.
  Window size bounds are in units too.
- One number does all of it: the GUI's projection carries the scale, and the clip rectangles
  are multiplied where the scissor is set. Nothing per frame costs anything it did not before.

[Units and pixels](docs/architecture/units-and-pixels.md) is the whole of it, including the
one thing it does not do: a window dragged onto a monitor at a different scale keeps the
scale it opened with, because the atlas is baked at it. The log says so once.

### The look reloads too

A layout has reloaded on save since 0.1. What it *looks like* -- `res/themes/*.ts` and
`res/mesh/*.ts` -- needed the application restarted, which is worst exactly where it matters
most: authoring a shape means moving a corner two units and looking at it, twenty times.

Both reload now. No application asks for it: a layout is something the application names and
polls, but a theme and its meshes are the engine's own assets, so the poll is in the frame.

- **Meshes first**, in the build's own order, because what a mesh declares is part of what a
  theme may carry. The declarations are read back out of the compiled `.rdam` rather than by
  evaluating the sources again -- which also means a mesh shipped without its source still
  contributes its parameters. That ordering is what lets somebody add a parameter and set it
  in the theme in the same minute.
- **A theme reload replaces rather than merges.** Deleting a line should put that colour back
  to the default, and this loop is mostly deleting a line and looking again. The cost: a C++
  application's code-defined variants are forgotten until it restarts.
- **The theme forgets what it resolved.** A style works out which colour belongs to which of
  a mesh's parameters once and remembers the mesh by address; clearing the library destroys
  that mesh and the replacement can land at the same address, which would read as "already
  resolved" and keep the colours worked out for a shape that is gone.
- Checked a few times a second rather than every frame. The question is whether somebody
  pressed Ctrl+S.
- A build with no compiler in it still watches the artifacts, so something else writing a
  `.rdth` retints a running program.

### The mesh compiler, tested

**165 cases, up from 138.** The reader, the refusals, the anchor arithmetic, the state blend,
the colour fallbacks, and the library cache that hot reload stands on.

- **`rda mesh` parses back what it has just written**, with the reader the engine will use.
  The format has two halves in two files, and the failure they can have together is a `.rdam`
  that compiles cleanly and then will not load -- surfacing much later as a widget quietly
  drawn without its mesh. Checked by making the writer disagree with the reader on purpose.
- A `rda_mesh` test runs the whole toolchain over a fixture written to use every primitive a
  real mesh uses: esbuild, the geometry prelude, QuickJS, the writer and the reader.

### Fixed

- **An esbuild *warning* corrupted every transform** — layouts, themes and meshes alike.
  stderr was folded into stdout, so a duplicate key in an object literal put
  `[WARNING] Duplicate key ...` into the middle of the JavaScript and the author was told
  `SyntaxError: unexpected character`, from QuickJS, with no line number and no mention of a
  duplicate anything. The streams are separate now: the same file reports the real warning,
  with both line numbers, and compiles.
- **`rrect` with every corner square fell through to `rect`** — four triangles against
  twenty, so a square-cornered state could not share a topology with a rounded one and the
  mesh was refused. Which is the one case that has to work: a radius of 0 in one state exists
  *because* another state is rounded. The documentation already promised this worked.
- `TextFieldStyle::forMode` moved from the widget drawing code to where every other style is
  built, so a theme can be read without linking the immediate-mode widgets.

### Also

- The documentation said "twenty-one elements" in three places. `<number>` and `<header>`
  arrived in 1.1 and nothing counted again; it is twenty-three.
- A new loose end written down rather than fixed: a scroll view that scrolls both ways cannot
  reach the last few units of its content, because the horizontal bar's strip is never
  subtracted from the vertical range.
- [Why state is declared where it is](docs/backend/declaration.md#so-i-write-my-state-twice),
  for the question everybody asks once: what is and is not written twice, and why the seam is
  at the state rather than at your types.

## 1.1.0 — 2026-10-01

C ABI **1.6, unchanged**. Nothing in this version crosses it: this is the layout language,
the theme, the toolchain and what gets drawn, all of which a backend reaches through calls
it already makes. A backend built against 1.0.0 runs against this untouched.

### Before upgrading

Two things that were already being drawn now look different.

- **A click no longer leaves a focus ring.** It is drawn only where the keyboard put the
  focus — Tab draws one, a click does not, and neither does `rda_set_focus`. A press is its
  own feedback, and on a widget that is *also* the chosen one a ring reads as a second kind
  of chosen-ness, which is wrong in the one place it matters most. `show: "always"` on the
  focus theme puts it back everywhere; `width: 0` still removes it entirely.
- **The engine prints warnings and errors only.** It used to print everything it had, which
  on start-up is dozens of lines of somebody else's business in your terminal.
  `config.logLevel = RDA::LogLevel::Info` asks for all of it back and `RDA_LOG=info` does
  it without a rebuild. A Debug build still writes every line into `RDA_DEBUG.txt` whatever
  the console is set to, so nothing is lost — only moved out of the console.

### Meshes: a widget's shape as an asset

The shape of every widget was a literal in a `.cpp` — a button is a rounded rect because
`Gui::button` draws one. Every colour of that was themeable and none of its *shape* was, so
a look not made of rectangles needed an engine change.

A mesh is that shape as an asset. You write it in TypeScript, `rda` compiles it, and a
theme variant names it — so a layout that already says `<button variant="tab">` gets the
new shape without a line changing:

```ts
tab: { mesh: "res/mesh/tab.rdam", text: "#E8EAF0", transitionMs: 160,
       "tab.seam": "#C8503C" },          // the mesh declared this; the theme sets it
```

- **Anchors are the one idea.** The graph runs when you build, and a mesh can see nothing
  at run time except the rectangle the layout gave the widget — so a vertex is not a
  position, it is a position expressed in terms of that rectangle:
  `pos.x = rect.x + ax * rect.w + ox`. Two multiply-adds when it is drawn, and nine-slice
  scaling generalised from nine rectangles to any mesh. Whether a feature stretches with the
  widget or keeps its size is the anchor, which is what makes the anchor the design decision.
- **States blend.** A mesh cannot see whether it is hovered, so hover is not a parameter —
  it is another state, and states are a ladder used in the order they are written: resting,
  under the pointer, held. The engine eases between them over the variant's `transitionMs`,
  so a shape change is an animation and you wrote no interpolation. Every state shares a
  topology, which is exactly what makes them blendable, and why a mesh cannot grow geometry.
- **Regions, rather than input.** `hit`, `text` and `clip` say *where* a press, a string and
  a container's contents go. The widget runs the same hit test it always did, and text stays
  with the text system, which already wraps, falls back and speaks UTF-8.
- **Parameters are declared, and the theme schema grows to fit.** A mesh says what it wants
  from the theme instead of the engine deciding in advance, and those become real fields on
  that element, namespaced by the mesh's file name. They are checked like every other
  field — `"tab.seem"` is refused with a suggestion, and so is a mesh parameter on a
  `<label>`, which no mesh can draw — and they reach the generated `rda.d.ts`, so an editor
  catches it first.
- `rda mesh <mesh.ts>` compiles one, and `rda build` finds `res/mesh/*.ts` by itself. Meshes
  compile **before** themes, because what a mesh declares is part of what a theme may carry.
  A C++ project states the list — `rda_add_meshes(myapp res/mesh/tab.ts)`, declared before
  `rda_add_theme` and `rda_add_types` — and that rule hands `--mesh` to both for you.

[Meshes](docs/frontend/meshes.md) is the whole of it, including what one deliberately
cannot do.

### Chosen, as against pointed at

- **`selected` on `<button>`.** Hover and press are things happening *to* a widget and the
  GUI works them out for itself. Being the chosen one — a tab, a segmented control, the tool
  that is currently the tool — is something only the application knows, so it is a property
  and a binding the button never writes. It draws in the variant's `pressed` colour and
  keeps it while the pointer is elsewhere, ahead of hover on purpose: a chosen tab that went
  back to looking unchosen because the pointer arrived on it would be saying the wrong thing
  in the one place it matters most. Drawn with a mesh, it is the top rung of that ladder.
- **`variant` on `<tabs>`.** Every title wears it and the widget marks the open one
  `selected`, so one variant describes a whole bar: its `normal` is a closed title, its
  `pressed` the open one. The bar is drawn **after** its pages now, which lets a title reach
  a pixel or two past its own rectangle onto the page below — how a mesh-drawn title merges
  into its page and the seam between them disappears. One consequence: a click lands after
  the pages have painted, so the page it chose begins arriving on the next frame.

### New elements

- **`<number>`** — a number you drag sideways to change and click to type into. `label`,
  `value`, `step`, `min`, `max`, `decimals`, and `onChange` while it is dragged as well as
  when a typed value is committed. Shift is the fine adjustment. Clicking turns it into a
  real text field, with selection, the caret and undo, because those already exist and a
  second half of them would be worse at all of it. Neither widget that was already here
  fits: a `<slider>` needs a range and spends its width on a track, and a `<textfield>`
  makes you select, type and commit to nudge a value by one.
- **`<header>`, inside a `<dock>`** — a panel's title bar as a widget tree instead of a
  filled strip with an `x` on the end. Dragging the band still picks the panel up, and a
  button on the bar still wins its own press: the band reads which widget the pointer went
  down on before its contents paint and again after, so a close button closes rather than
  drags. It is drawn wherever the panel is the only one in its pane — floating and tiles
  included — and not when it is one tab among several, because a strip belongs to a pane and
  a header belongs to a panel, and two panels in one group cannot both dress it.

### Right-click menus

`onContext` is on every element and fires when the right button goes down on it; the deepest
widget that asked for it gets it. It carries nothing, because the thing that wants the
position is the menu, and `<popup anchor="pointer">` already knows where the pointer was
when it opened. The middle button is readable too (`GuiInput::mDown` and friends), with no
event of its own yet.

Mouse state is per button now — `lPressed`, `rPressed`, `mPressed` and the rest — rather
than one set of flags for whichever button it happened to be.

### Docking

- **A drag shows you where it would land.** The four window edges, banded, with the one
  under the pointer lit, so docking across a whole side is discoverable at all. The pane
  under the pointer, tinted. And the panel itself as a figure: the rectangle it would take,
  outlined, wearing a bar with its name on it. Over a pane's edge that figure is the half
  the panel would take; in a pane's middle, where the drop joins that pane's tab group, it
  is the **tab it would become**, drawn at the end of that pane's strip. It fades in with
  the drag rather than blinking on the frame a panel is picked up.
- **The corner of a pane splits and joins it.** Dragging the grip in a pane's bottom-right
  inward cuts the pane in two and moves the panel showing into the new half; dragging it out
  over a neighbour makes the two one. A split *moves* the panel rather than copying it,
  which is where this parts company with the gesture it is borrowed from — so a pane holding
  one panel has nothing to split off, and the grip is not drawn when there is neither
  anything to split nor anyone to join.
- **`visible` on a `<dock>` closes the space up.** The pane's sibling takes the whole split,
  the splitter between them goes, and showing the panel again puts it back where it was at
  the size it was. That is what a close button you drew yourself should write; the engine's
  `x` is different, and removes the panel from the tree for good.
- Animation keys are per dock space, so two spaces in one window no longer drive each
  other's guides.

### Seeing a layout with no backend behind it

```bash
rda prototype .
```

A window, with your interface in it, and nothing behind it. It compiles the project,
defines every signal at the value it was declared with, every table with its columns, and
binds every command to a line on the terminal saying which one the interface asked for.
Editing a `.tsx` rebuilds it in place, the same hot reload an application gets.

`--set count=99` reads a value as that signal's declared type, `--rows products=50`
generates deliberately obvious placeholder rows, `--data mock.json` does a screenful at
once, `--layout` says which route — or which file — to open on, and `--no-build` shows what
is already compiled. A key naming neither a signal nor a table is refused rather than
skipped: a misspelling in a mock file is otherwise a screen that stays empty for a reason
nobody can see. Every refusal exits non-zero.

[Prototyping](docs/setup/prototyping.md).

### The log

`LogLevel` is `Off`, `Error`, `Warning` or `Info`. `config.logLevel` sets it and defaults to
`Warning`, and `RDA_LOG=off|error|warning|info` overrides whatever an application asked for,
because an environment variable is somebody standing at the machine and they outrank a value
compiled in months ago.

A **Release** build could previously neither log nor stop. Every macro compiled away, which
did not make a release build quiet so much as deaf: it could not report a theme that would
not load or a swapchain that would not be created, and `RDA_RUNTIME_ERROR` expanded to
nothing, so a fatal failure carried straight on into the next line. Both are fixed — errors
are reported, and fatal ones still throw.

### Fixed

- **Animation snapped at the end, for two separate reasons.** A journey is read from one
  clock and that clock was x's, so a widget moving only downward — every child of a vertical
  stack, which is most widgets that move at all — had an x that had never been anywhere and
  therefore a clock saying nothing had happened. It stood still for the whole duration and
  arrived in a single frame. Either axis having somewhere new to go now restarts both. And
  the step that lands a value *on* its target reported nothing as moving, so an on-demand
  window skipped the one frame that would have shown it there: the end of every journey
  never reached the screen, and sat a frame short until something unrelated forced a walk.
  Three cases cover both, linear on purpose, because with an easing that decelerates the
  leftover is a fraction of a pixel and the same bug is invisible.
- **Toggling window decorations could take the process down.** The GLFW position, size and
  refresh callbacks fire synchronously out of `SetWindowPos`, and `SetWindowPos` is what
  `state.rda.maximized = true` eventually calls — so the frame the engine hangs off those
  callbacks (to keep drawing while a window is dragged, when `glfwPollEvents` will not
  return) started in the middle of the walk that ran the handler, reset the frame arena and
  truncated the draw list underneath it. `runFrame` now says while it is building, and that
  second door only asks for another frame instead. Dropping the event is safe: the new size
  and position are read again at the top of the next frame.
- **`anchor="pointer"` was a type error** in a project's generated `rda.d.ts`. An element
  that shadows a common property now emits `Omit<RdaCommonProps, "anchor">`.
- A scroll thumb follows the left button specifically, so a right-click on its way to a menu
  no longer grabs it.

### Also

- 138 test cases, up from 135.
- Linux: Debug **and** Release both build on GCC 13.3 and pass, from a clone. The log macros
  expanding in Release is what found two includes MSVC had been supplying by accident —
  `<cstring>` for `strrchr` and `<strings.h>` for `strcasecmp`.

## 1.0.0 — 2026-09-22

C ABI **1.6**. 55 functions to 88, with nothing removed and nothing changed in meaning.

One thing was not compatible, and it is why this was 1.0 rather than 0.10: **a colour is
the colour you wrote.** The GUI shader took sRGB bytes into a linear pipeline, so every
value in every theme drew lighter than it was written, and applications had been
compensating for it in their own palettes.

Drawings, from an `.svg` rasterised at the size the layout gives it — and animated, since
`<animate>`, `<animateTransform>` and `<set>` are read, with `stroke-dasharray`, which is
what makes an icon draw itself. Motion with a shape: `route` is an SVG path a widget travels
and `pace` is how fast it travels it. `<stream>`, a picture that keeps arriving, and compute
effects over one to four pictures with no Vulkan in the application. A window of your own —
icon, `decorated`, size bounds, and four signals of `state.rda` that read and write, so a
close button is an assignment. `arrange="tiles"` as a second docking arrangement. `<popup>`,
undo per text field, a label in more than one colour, rows of different heights, `onHover`,
pictures from memory, `tint`, `submitKey`, `language`. And a collapsing container takes its
contents with it rather than slicing them.

## 0.9.0 — 2026-09-08

C ABI **1.1**, and the reason there is one: a backend reaches the interface layer through a
C ABI, so it does not have to be C++. Python, Node and C# each got a package, a generator
that emits the state declaration in that language, a CMake rule and a test that starts a
real engine.

Docking with a customisable chrome, animation as an inherited property, alignment at three
levels, and text that is UTF-8 from the `.tsx` to the screen — the atlas had covered ASCII
32..126 and replaced everything else with a space, which is correctly laid out, silently
wrong, and invisible to a test written in English.

The Vulkan SDK stopped being vendored: `Dep/` was 1.2 GB of a 1.22 GB repository. The first
Linux build, which found 31 headers using a fixed-width integer without including
`<cstdint>` themselves. And the documentation became a folder per subject rather than four
files.

## 0.1.0 — 2026-08-27

The turn that made this an interface engine rather than a renderer. A layout is written in
TypeScript and compiled ahead of time into a flat binary blueprint, so the application that
loads it contains no parser and no JavaScript engine. Then signals and bindings over a state
declaration both sides are generated from, compiled to stack bytecode so that writing a
signal marks exactly the bindings that read it; hot reload that survives the swap; and
themes in TypeScript, compiled the same way.

Added here and removed again before the end: a run-time JavaScript host, a global runtime
process with its own wire protocol and compositor, and an in-repo sandbox. They were routes
to the same goal, and the compiled blueprint replaced all three.

## Before that — unversioned

A Vulkan renderer: instance and device selection against a real surface, swapchain and
render pass, framebuffers, shaders compiled at run time and cached, a memory allocator over
VMA, window and input handling on GLFW. Then an immediate-mode GUI over it, with a baked
font atlas carrying a reserved white block, so that solid rectangles and text of every size
sample one texture and a whole frame batches into a single draw.

---

Entries before 1.1.0 were written after the fact. The commit messages are the longer record,
and the one written at the time — `git log` has them in full.
