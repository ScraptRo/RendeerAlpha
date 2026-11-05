# `res/state.ts` — what both sides agree on

One file. The build generates every other side from it: the C++ header, the Python
module, the JavaScript module, the C# class, and the TypeScript the layouts are checked
against. Add a name here first, then use it.

It is TypeScript, but only four exports are read: `state`, `commands`, `tables`,
`routes`. Anything else in the file is ignored, unless a `compute`, a `watch` or a `run` uses it --
then it ships with them.

```ts
export const state = {
  count:  { value: 0,     doc: "how many times the button was pressed" },
  notes:  { value: "",    doc: "what is typed in the field" },
  busy:   { value: false, doc: "whether work is in flight" },
  ratio:  0.5,                          // short form: no documentation
}

export const commands = {
  save:    "write the notes somewhere",
  refresh: "read it all again",
}

export const tables = {
  products: {
    title:   { value: "", doc: "what it is called" },
    price:   { value: 0,  doc: "in whole currency units" },
    inStock: false,
  },
}

export const routes = {
  home:      { layout: "home",      doc: "where it opens" },
  catalogue: { layout: "catalogue", doc: "the list", params: ["productId"] },
}
```

Two names exist without being declared here: `state.rda.width` and `state.rda.height`,
the window's size in pixels. They are the engine's, read-only, and a layout may use them
to change shape. See `references/bindings.md`.

## Signals

A field's **type is whatever its initial value is** — number, string or boolean. There
are no others: no arrays, no objects, no null. A list of things is a table.

`{ value, doc }` is the long form; a bare value is the short one. The documentation
reaches every generated side and the tooltip an editor shows, so it is worth writing.

Naming: use the same name everywhere. `state.count` in a layout, `State.count` in
Python, Node and C#, `State::count()` in C++ — one signal, one spelling.

## Commands

A name and a sentence saying what it does -- or the long form, with what it is called with,
declared by example and in order:

```ts
export const commands = {
  save: "write the notes somewhere",
  open: { doc: "show one product", params: { id: 0 } },
}
```

```tsx
onClick={() => commands.open(item.id)}
```

**Pass what the command needs as arguments**, not through a signal written first. The
backend's handler receives them typed: `onOpen([](double id) {...})`, `def on_open(id)`,
`(id) => ...`, `Action<double>`. A command nothing answers is refused rather than ignored,
and says so in the log.

## Code in state.ts: computed values and actions

Logic more than one layout or the backend needs belongs here, written once:

```ts
export const state = defineState({
  price: 0,
  qty:   1,
  total: { compute: () => state.price * state.qty, doc: "what it costs" },   // computed
  theme: { value: "dark", watch: (now: string) => commands.save(now) },      // watched
})

export const commands = defineCommands({
  add: { doc: "one more", run: (n: number = 1) => { state.qty += n } },      // an action
})
```

- **A computed field** is a real signal: bindings and backends read it, nothing writes it
  (no setter is generated, and a write is refused). It is current the moment what it read
  changes. Its type is what it returns when the build runs it once on the starting values,
  tables empty -- give `value` too if it cannot run without data. It may read `state` and
  `tables`; it may not write state or call commands.
- **A watched field** runs its `watch` when the value changes, whoever wrote it. It runs as a
  handler does, so this is the place for a reaction: never a write in a value binding. It is
  told `(now, was)` -- name their types -- once for a batch of writes, not on load, and
  not for a value that came back. For several signals or a table, put it on a `compute`.
- **An action** is a command state.ts answers: called from a layout like any command,
  run by `rda prototype`, callable from a backend with `call("add", 2)` (returns its result),
  and bindable by a backend to hear of it after. It runs as a handler does.
- **Wrap with `defineState`, `defineTables`, `defineCommands`** whenever the file has code:
  they change nothing built, but without them the editor types `state.qty` as
  `{ value, doc }`.
- The code ships inside the generated backend module and loads in `define()`; regenerate
  with `rda build` after changing it.

## Tables

The rows a `<list>` shows. Each key is a table; inside it, each key is a column with an
initial value that fixes its type.

A table rather than one widget per row because a `<list>` keeps about as many widgets as
fit on screen and re-binds them while scrolling: ten thousand rows cost ten thousand rows
and roughly thirty widgets.

The backend fills a table **one column at a time** — every call crosses to the engine's
loop thread, so ten thousand rows of three columns is three round trips rather than
thirty thousand. Each binding's `fill()` does that for you; see
`references/backends.md`.

## Routes — more than one screen

Declaring `routes` adds three things by itself:

| | |
| --- | --- |
| a `route` signal | holds the name of the screen showing. Assign to it to navigate |
| `commands.back()` | the screen visited before this one |
| `commands.forward()` | the screen gone back from |

`layout` is the file's stem: `layout: "home"` loads `res/layouts/home.rdab`. The first
entry is where the application opens unless `route` already says otherwise.

Navigating is an ordinary write, from the interface or from the backend:

```tsx
<button id="about" text="About" height="content"
        onClick={() => state.route = "about"} />
```

`params` names signals whose values are remembered with the history entry and restored
when you go back to that screen — a selected row's id, a scroll position.

There are no URLs and no deep links, and history starts empty every run.

## Opening and closing a screen

`onOpen` and `onClose` go on a layout's root element and nowhere else. They are ordinary
handlers. Every `onOpen` gets exactly one `onClose`, the application exiting included, and a
saved edit runs neither. Bind a command's handler before loading a layout whose `onOpen`
calls it. On a root `<popup>`, `onClose` is the popup's own event.

## After changing this file

Build again. The state module is regenerated, and a backend that was running has to be
restarted — hot reload covers layouts, not the declaration.

```bash
python -m rda build .        # or: npx rda build .   /   dotnet build   /   cmake --build
```
