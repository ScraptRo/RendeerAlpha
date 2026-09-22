# Bindings and handlers

`{() => ...}` is not JavaScript that ships. It is parsed at build time into a short
sequence of stack operations over signals and constants. The running program has no
interpreter, so **the only things that exist inside a thunk are the ones listed here**.

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
error. They are in pixels, the same ones every width and height is in, and they follow a
resize by themselves.

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
| ask for work | `commands.save()` |
| the event's value | `(typed) => state.notes = typed` |
| several statements | `() => { state.open = false; commands.save() }` |

## Not allowed anywhere

Each of these is a build error with a message naming the property and the element.

| Written | What happens |
| --- | --- |
| `Math.round(state.kb)` | `'Math' is not in scope` |
| `state.kb.toFixed(1)` | `only a single level of state is readable: state.<name>` |
| `state.name.toUpperCase()` | the same — any method call on a signal |
| `state.user.name` | the same — state is one level deep, apart from `state.rda` |
| `[1, 2, 3]`, `{ a: 1 }` | `arrays and objects are not compiled yet` |
| `const step = 5` … `state.n += step` | `'step' is not in scope` |
| `() => props.label` in a component | `props are resolved when the layout is compiled` |
| `commands.save()` in a value binding | `a value binding cannot call a command` |
| `item.x` outside a `<list>` row | `'item' is not in scope` |

The pattern behind every one: a binding is compiled **from its own source text**, so
nothing it appears to close over comes with it.

## What to do instead

**Formatting a number.** There is no `toFixed`. Round in the backend and write the
result to a signal, or a text signal beside it:

```python
State.totalKb = round(total / 1024.0, 1)     # Python decides how it reads
```

```tsx
<label id="t" height="content" text={() => `${state.totalKb} KB`} />
```

**A value derived from two signals** is fine as long as it is arithmetic:
`text={() => `${state.done} of ${state.total}`}`. Anything harder belongs in the backend.

**A constant used twice.** A `const` at the top of the file cannot be read from a thunk,
but it *can* be used in a constant property, because that is substituted before there is
an expression to parse:

```tsx
const WIDTH = 110
<button id="a" width={WIDTH} text="Save" />            // fine
<button id="b" width={() => WIDTH} text="Save" />      // error: 'WIDTH' is not in scope
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
