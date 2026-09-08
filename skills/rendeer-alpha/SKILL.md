---
name: rendeer-alpha
description: Write a RendeerAlpha interface — a .tsx layout, a res/state.ts declaration, a theme — and the backend behind it in C++, Python, Node or C#. Use when the task mentions RendeerAlpha, rda, a .tsx layout, a .rdab blueprint, res/state.ts, or an rda_add_* / `rda build` command.
---

# RendeerAlpha

A desktop interface is written in TypeScript and **compiled ahead of time** into a flat
binary blueprint (`.rdab`). The running program contains no parser and no JavaScript
engine: it reads the blueprint and builds widgets. The backend is C++, Python, Node or C#
and owns the data.

They meet at three things, declared once in `res/state.ts`:

| | |
| --- | --- |
| **signals** | named values. The layout reads them; the backend writes them |
| **commands** | named work. The layout asks; the backend does it |
| **tables** | rows. The backend fills; a `<list>` shows as many widgets as fit |

The layout never calls the backend directly and the backend never touches a widget.

## Every project has this shape

```
MyApp/
├── app.py | app.mjs | main.cpp | Program.cs     the backend
├── state.py | state.mjs | state.cs              GENERATED - never edit
└── res/
    ├── state.ts              signals, commands, tables, routes
    ├── themes/app.ts         colours and sizes
    └── layouts/
        ├── home.tsx          the interface
        ├── rda.d.ts          GENERATED - never edit
        └── tsconfig.json
```

## The loop

Edit files under `res/`, then compile, then run:

```bash
python -m rda build .   &&  python app.py      # Python
npx rda build .         &&  node app.mjs       # Node
dotnet run                                     # C# (the .csproj runs the tool)
cmake --build build --config Debug             # C++ (the CMake rules run the tool)
```

`rda build` compiles each theme to `.rdth`, each layout to `.rdab`, regenerates
`rda.d.ts`, and writes the state module. It prints one line per file and skips anything
already up to date. **A build error names the file and the line — read it, do not guess.**

While a Debug build is running, saving a `.tsx` rebuilds that screen in place and the
state survives.

## A complete example

`res/state.ts`

```ts
export const state = {
  count: { value: 0,  doc: "how many times the button was pressed" },
  notes: { value: "", doc: "what is typed in the field" },
}

export const commands = {
  save: "write the notes somewhere",
}
```

`res/layouts/home.tsx`

```tsx
export default function Home() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12}>
      <label id="title" variant="heading" height="content"
             text={() => `Clicked ${state.count} times`} />

      <stack id="row" arrange="horizontal" height="content" spacing={6}>
        <button id="add" text="Add one" variant="primary" width={110} height="content"
                onClick={() => state.count++} />
        <button id="reset" text="Reset" width={110} height="content"
                onClick={() => state.count = 0} />
      </stack>

      <textfield id="notes" mode="document" height="fill"
                 text={() => state.notes}
                 onChange={(typed) => state.notes = typed} />

      <button id="save" text="Save" width={110} height="content"
              onClick={() => commands.save()} />
    </stack>
  )
}
```

`app.py`

```python
import rda
from state import State, Commands, define

config = rda.StartupConfig()
config.theme = "res/themes/app.rdth"

@Commands.save
def on_save():
    print("saving:", State.notes)

@config.on_start
def opened():
    define()
    rda.load_interface("res/layouts/home.rdab", "res/layouts/home.tsx")

rda.init(config)
rda.wait()
```

## Rules that are not style

The first four are build errors. The rest are worse: they build, and go wrong later.

1. **A binding may read `state.<name>`, `state.rda.<name>`, `item.<column>` and
   literals. Nothing else.**
   No function calls, no `Math`, no `.toFixed()`, no `.length`, no arrays, no objects,
   and no variable from the code around it — not even a `const` at the top of the same
   file. Compute in the backend and put the answer in a signal.
2. **`commands.x()` only inside an event handler**, never in a value binding.
3. **`state.a.b` does not exist.** State is one level deep. The single exception is
   `state.rda.width` and `state.rda.height`, the window's size, which the engine
   maintains and a layout may only read.
4. **A layout file has a `default` export that is a function** returning one element.

5. **Only these 19 elements exist.** `container panel stack scroll splitter viewport
   label button checkbox slider textfield list image tabs tab select option
   dockspace dock`. There is no `div`, `text`, `input`, `row`, `column` or `grid`.
   A made-up element **compiles**, and at run time it and all its children are skipped
   with a line in the log. A made-up property is ignored the same way.
6. **A name the application never declared also compiles.** `state.typo` reads 0,
   `commands.typo()` does nothing, `<list of="typo">` shows nothing — each with a warning
   in `RDA_DEBUG.txt`. The build cannot catch these; **read the log**.
7. **A value is a constant or a thunk.** `text="Save"` is a constant. `text={() => ...}`
   is a binding, re-evaluated only when a signal it reads changes. `text={state.notes}`
   without the arrow reads the value once, while the layout is compiled, and then never
   changes again.
8. **Sizing.** In a **vertical** stack give every child a `height`
   (`"content"`, `"fill"` or a number); its width fills the stack. In a **horizontal**
   stack give every child a `width`; its height fills the row. `"fill"` shares what is
   left over.
9. **Give every element an `id`**, unique among its siblings. It is optional, and it is
   what hover, focus, hot reload and every log message are keyed on.
10. **Never edit a generated file**: `state.py`, `state.mjs`, `state.cs`, `RdaState.h`,
    `res/layouts/rda.d.ts`. Change `res/state.ts` and build again.

After running an interface, check `RDA_DEBUG.txt` in the working directory. Anything
worse than `INFO` is a real mistake, including in a program that looked fine.

## Read one of these before writing that part

| Doing | Read |
| --- | --- |
| choosing an element, or its properties | `references/elements.md` |
| anything inside `{() => ...}` | `references/bindings.md` |
| adding a signal, command, table or screen | `references/state.md` |
| colours, sizes, variants | `references/theme.md` |
| the backend in C++ / Python / Node / C# | `references/backends.md` |
| a list, a form, screens, tabs, docking | `references/recipes.md` |
| a build or run-time error message | `references/errors.md` |

Answer with complete files. A layout is short; write the whole `.tsx` rather than a
fragment to paste in.
