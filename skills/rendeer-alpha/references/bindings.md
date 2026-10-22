# Bindings and handlers

`{() => ...}` is compiled at build time, two ways. What the grammar below covers becomes a
few stack operations with no engine behind it -- the fast path, used whenever it can be.
Anything more is JavaScript, compiled to QuickJS bytecode and run by the engine: a function
the file defines, a method, an array, a loop. **What a thunk can name is `state`,
`commands`, `tables`, `item` in a row, and the top level of its file and its imports** --
never the code around it, because it is compiled from its own text.

| | |
| --- | --- |
| **binding** | `text={() => ...}` — a value. Re-evaluated when a signal it reads changes |
| **handler** | `onClick={() => ...}` — an action. Runs on the event, may write state |
| **value handler** | `onChange={(v) => ...}` — an action given the new value |

## Allowed in both

| Form | Example |
| --- | --- |
| signal | `state.count` |
| literal | `42`, `"text"`, `true` |
| template literal | `` `${state.n} items` `` |
| arithmetic | `state.a + state.b * 2`, `-state.x` |
| comparison | `=== !== == != < <= > >=` |
| logic | `state.a && !state.b`, `state.a \|\| state.b` |
| ternary | `state.n === 1 ? "item" : "items"` |
| row cell, inside a `<list row=...>` | `item.price` |
| the window's size | `state.rda.width`, `state.rda.height` |
| the window's state | `state.rda.maximized`, `.minimized`, `.fullscreen`, `.open`, `.focused` |
| a size with an offset | `` width={() => `content-${state.gap}`} `` |
| an alignment with an offset | `` hAlignSelf={() => `start+${state.indent}`} `` |

### The window

`state.rda` holds the only two-level names there are, and the only ones the engine
writes. Use the size for a layout that changes shape:

```tsx
<stack id="root" arrange={() => state.rda.width < 700 ? "vertical" : "horizontal"}
       spacing={8} padding={12}>
```

`width`, `height` and `focused` are the engine reporting, and writing one is a build
error. They are in layout units, the same ones every width and height is in, and they follow
a resize by themselves.

A unit is a pixel on an ordinary display and 1.25 pixels on one at 125%: the engine scales
the whole interface and bakes text at the display's size, so a layout never multiplies
anything by anything. Say "units" rather than "pixels" when explaining a number to somebody.

`maximized`, `minimized`, `fullscreen` and `open` read **and** write. That is how a
window opened with `decorated = false` gets buttons that do something -- the layout draws
its own title bar, `dragWindow` makes it a handle, and the buttons are assignments:

```tsx
<panel id="bar" height={34} dragWindow={true}>
  <button id="close" text="x" width={30} height={22}
          onClick={() => state.rda.open = false} />
</panel>
```

See `docs/frontend/window.md` for the config that opens a window without a frame.

## Allowed only in a handler

| Form | Example |
| --- | --- |
| write a signal | `state.open = true` |
| compound write | `state.count += 1`, also `-= *= /=` |
| increment | `state.count++`, `state.count--` |
| ask for work | `commands.save()`, `commands.open(item.id, state.draft)` -- arguments travel with it |
| the event's value | `(typed) => state.notes = typed` |
| several statements | `() => { state.open = false; commands.save() }` |

## Functions: code that runs

Write the logic once, in the layout, instead of in every backend:

```tsx
// util.ts -- or the top of the .tsx
const UNITS = ["B", "KB", "MB", "GB"]
export function formatSize(bytes: number) {
  let i = 0
  while (bytes >= 1024 && i < UNITS.length - 1) { bytes /= 1024; i++ }
  return `${bytes.toFixed(i ? 1 : 0)} ${UNITS[i]}`
}
export const total = () => tables.files.reduce((sum, f) => sum + f.size, 0)
```

```tsx
<label id="s" text={() => formatSize(state.size)} />
<label id="t" text={() => state.tags.split(",").length + " tags"} />
<label id="u" text={() => `${formatSize(total())} in ${tables.files.length} files`} />
<button id="b" text="Add" onClick={() => { const n = state.count + 1; state.count = n }} />
```

- **What runs when.** Code a binding or handler reaches runs at run time. The file's top
  level and every component run at build time, as always. A top-level value with no call
  in its initializer (`[...]`, a literal) ships as code; one made by a call ships as the
  value the call returned -- it must be plain data (numbers, text, arrays, objects, Map,
  Set, Date, RegExp), not a class instance or a closure.
- **Run-time code may** read and write `state` (writes only in a handler), read `tables`
  (`.length`, `.at(i)`, `for...of`, `.map`, `.filter`, `.find`, `.reduce`), call
  `commands` (handler only), use `Math`, `JSON`, `Date`, `RegExp`, `Map`, `Set`, the colour
  helpers, `format`, `console.log`, and -- in a handler or an action -- `setTimeout` and
  `setInterval`. Not files, network, `eval`, JSX or `signal()`.
- **Dependencies** are recorded while it runs: a binding re-runs when exactly what it read
  changes, tables included.
- **Errors** are logged once with the `.tsx` line; the binding keeps its last value. A call
  that runs past 50 ms is stopped.
- **No `Intl`; use `format`.** `format.number(x, { decimals: 2 })` -> `"1,234.50"`,
  `format.percent(0.25)`, `format.bytes(n)` -> `"1.5 KB"`, `format.duration(ms)` ->
  `"1:02:03"`, `format.date(t, "EEE d MMM, HH:mm")`, `format.relative(t)` -> `"5 minutes ago"`.
- **Timers** run on the frame clock, as a handler does: a toast that clears itself is
  `setTimeout(() => { state.toast = "" }, 1500)` in the handler or action that set it. Not
  in a value binding, a computed value, or at the top level. A layout's timers go when it
  reloads.
- `rda build --no-runtime-code` refuses all of it, for an application that wants none.
- **Logic shared by several layouts or the backend** goes in `state.ts` instead: a
  `compute`d field, or a command with a `run`. See `references/state.md`.

## Still not allowed

Each of these is a build error naming the property and the element.

| Written | What happens |
| --- | --- |
| `() => props.label` in a component | `props are resolved when the layout is compiled` |
| a local of the function around the JSX, read in a thunk | `'x' is not in scope` |
| the same, when the file's top level has an `x` too | `'x' is declared by the code around this binding` -- it would read the top-level one |
| `item.x` outside a `<list>` row | `'item' is not in scope` |
| `commands.save()` in a value binding | `a value binding cannot call a command` |
| `state.x = 1` in a value binding | `a value binding cannot write state` |
| JSX or `signal()` in a function a binding calls | `JSX builds the layout, which happens once` |
| `const one = new Thing()` read at run time | `'one' (main.tsx:3) is a Thing, and only plain data ... can be written out` |

The pattern behind the scope ones: a binding is compiled **from its own source text**, so
nothing it appears to close over comes with it -- only the top level, which it can name.

## What to do instead

**A value derived from two signals** is fine either way: arithmetic stays in the grammar,
anything more is a function. Prefer a function in the layout over a signal the backend
keeps up to date; prefer the backend for anything that touches files, the network or a
lot of data.

**A constant used twice** -- at the top of the file -- is readable from a thunk now, and
from a constant property as before:

```tsx
const WIDTH = 110
<button id="a" width={WIDTH} text="Save" />            // substituted at build time
<button id="b" width={() => WIDTH} text="Save" />      // a binding; WIDTH ships with it
```

**A component's props** work the same way: pass them through as constants.

```tsx
function Row(props: { label: string, value: RdaBound<string> }) {
  return (
    <stack arrange="horizontal" height="content" spacing={6}>
      <label width={90} height="content" text={props.label} />   {/* substituted: fine */}
      <label width="fill" height="content" text={props.value} /> {/* a thunk the caller wrote */}
    </stack>
  )
}

<Row id="a" label="Folder" value={() => state.folder} />
```

The caller's thunk is compiled where the caller wrote it, against the signals in scope
there — which is why a component can wrap something reactive without knowing what it
reads.

## Signals a layout owns

Not every flag belongs in `res/state.ts`. A panel being open, which tab is showing:

```tsx
signal("expanded", true)          // at the top of the file, outside the component

export default function Home() {
  return (
    <checkbox id="e" height="content" label="Details"
              value={() => state.expanded}
              onChange={(on) => state.expanded = on} />
  )
}
```

Scoped to that file, so two layouts may each have an `expanded`. Pass
`{ global: true }` to share it with the application and every other layout.

## Two-way

A field or a checkbox does not remember anything by itself. It shows what its binding
reads and reports what the user did; writing the signal back is the handler's job.

```tsx
<textfield id="f" height="content"
           text={() => state.filter}
           onChange={(typed) => state.filter = typed} />
```

Leave out the `onChange` and typing does nothing. Leave out the `text` binding and the
field ignores anything else that writes `state.filter`.
