# Errors

Two kinds, and the difference matters. **Build errors** come from `rda build` and name the
file, the element and the property. **Run-time warnings** go to `RDA_DEBUG.txt` in the
working directory, and are how a layout that shows nothing explains itself.

Read the log after every run. A program that exited cleanly can still have warned that
half its bindings read a signal nobody declared.

## Build errors — a layout

| Message | Cause | Fix |
| --- | --- | --- |
| `'Math' is not in scope` | a call in a binding | compute in the backend, put the answer in a signal |
| `'step' is not in scope` | a binding read a variable from around it | inline the literal, or make it a signal |
| `'item' is not in scope` | `item.x` outside a `<list>` row template | only the `row={(item) => ...}` function has it |
| `'props' is not in scope` | see the props row below | |
| `only a single level of state is readable: state.<name>` | `state.a.b`, or a method call like `state.kb.toFixed(1)` | state is flat; format in the backend |
| `a value binding cannot call a command` | `commands.x()` in a value binding | call it from an event handler |
| `arrays and objects are not compiled yet` | `[1,2,3]` or `{a:1}` in a thunk | use a table for rows, signals for values |
| `props are resolved when the layout is compiled, so they cannot be read from a binding` | `text={() => props.label}` in a component | `text={props.label}`, or have the caller pass a thunk |
| `the layout has no default export, or it is not a function` | `export function Home()` | `export default function Home()` |

An error from esbuild — a stray bracket, a bad import — is passed through as it is, with
the line and column in the `.tsx`.

## Build errors — a theme

| Message | Fix |
| --- | --- |
| ``nothing is styled by `buton`. Did you mean `button`?`` | only `button checkbox slider panel label textfield dock` are read |
| ``` `button.primary` has no field called `borderWith`. Did you mean `borderWidth`? ``` | see `references/theme.md` for the fields of each |
| `the theme exports nothing` | a theme is a set of **exported** objects |

## Build errors — the toolchain

| Message | Fix |
| --- | --- |
| `esbuild not found` | the engine's `bin/` has none. Run the bring-up script in the engine checkout, or set `RDA_ESBUILD` |
| `cannot find the rda tool` | the same: `bin/` is empty. `python -m rda where` / `npx rda where` says what it looked for |
| `no res/ directory in <path>` | `rda build` was given something that is not a project |

## Run-time warnings — a layout that is not doing what it should

These all **build cleanly**. The log is the only place they appear.

| Warning | What you get | Fix |
| --- | --- | --- |
| `rda_init: on_start finished without loading an interface` | an empty window | call `load_interface` or `open_routes` inside `on_start`. Declaring state is not enough |
| `layout: no widget type named 'div' (id 'x'), skipping it and its children` | that whole subtree is missing | one of the 20 element names |
| `layout: <label> has no property 'colour' (id 'x') - ignored` | the property does nothing | check `references/elements.md` |
| `layout: … reads state.typo, which nothing declared - assuming a number` | the binding reads 0 for ever | declare it in `res/state.ts`, or fix the spelling |
| `layout: … calls commands.typo(), which the application never declared` | the button does nothing | declare it, and bind a handler in the backend |
| `layout: … reads kb, which is not a column of the list's table` | that cell is empty | the column names come from the table in `res/state.ts` |
| `list 'rows': there is no table named 'entries'` | an empty list | declare the table, and call `define()` before the layout opens |
| `layout: list 'rows' shows table 'x', which nothing declared` | the same | |
| `layout: nothing answers 'onClick' on 'x'` | the handler never runs | that element has no such event — a `<label>` is not clickable |
| `binding: nothing named 'text' on this widget to drive` | the binding is dropped | that element has no such property |
| `Router: nothing routes to 'x'; opening 'home'` | the wrong screen | `state.route` was set to a name not in `routes` |
| `list 'rows': 40 rows fit but only 32 were built; raise poolSize` | the bottom of the list is blank | raise `poolSize` on the `<list>` |
| `Validation layer … is not installed; running without it` | nothing; it still runs | Linux: `sudo apt install vulkan-validationlayers` |

## Starting up

| Symptom | Cause | Fix |
| --- | --- | --- |
| `cannot find the engine (rendeer_c.dll)` | the engine was never built or staged | run the bring-up script in the engine checkout |
| `No module named rda` (Python) | this interpreter is not registered | `python <checkout>/bindings/python/register.py` |
| `Cannot find module 'rda'` (Node) | no `npm install` in the project | `npm install <checkout>/bindings/node` |
| the font cannot be loaded, and it stops | started from the wrong directory | run from the project directory; `res/` resolves relative to it |
| a layout loads but every binding reads 0 | `define()` ran after the layout opened | declare the names first |
| the window opens and stays empty | nothing loaded an interface | the warning above says so; `load_interface(blueprint, source)` in `on_start` |
| `BadAccess (attempt to access private resource denied)`, then an abort | started with `sudo` under a desktop session | never `sudo`; `sudo chown -R $USER:$USER` anything an earlier run left behind |
| `no DISPLAY or WAYLAND_DISPLAY` | no screen | run from a desktop session, not over SSH |

## When a change does nothing at all

In order:

1. Did `rda build` actually run, and did it print a line for the file you edited? It
   skips anything already newer than its source.
2. Is the program running from the project directory?
3. Is the warning in `RDA_DEBUG.txt`?
4. Is the name spelled the same in `res/state.ts` and in the layout?
