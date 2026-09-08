# `res/state.ts` — what both sides agree on

One file. The build generates every other side from it: the C++ header, the Python
module, the JavaScript module, the C# class, and the TypeScript the layouts are checked
against. Add a name here first, then use it.

It is TypeScript, but only four exports are read: `state`, `commands`, `tables`,
`routes`. Anything else in the file is ignored.

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

A name and a sentence saying what it does. **Commands take no arguments**: write what one
needs into a signal first, then ask.

```tsx
onClick={() => { state.pending = item.id; commands.open() }}
```

The backend binds a handler per name. A command nothing is bound to is refused rather
than ignored, and says so in the log.

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

## After changing this file

Build again. The state module is regenerated, and a backend that was running has to be
restarted — hot reload covers layouts, not the declaration.

```bash
python -m rda build .        # or: npx rda build .   /   dotnet build   /   cmake --build
```
