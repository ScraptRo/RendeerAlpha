# Functions: code that runs

A binding that the [expression grammar](expressions.md) covers -- `state.a * state.b`, a
ternary, a template -- is compiled to a few stack operations and runs with no engine
behind it. Anything more is JavaScript, and it runs too: a function this file or one it
imports defines, `.split()`, `.toFixed()`, a loop over a table. It is compiled to QuickJS
bytecode when you build, and the blueprint carries it.

```ts
// res/layouts/util.ts
const UNITS = ["B", "KB", "MB", "GB"]

export function formatSize(bytes: number) {
  let i = 0
  while (bytes >= 1024 && i < UNITS.length - 1) { bytes /= 1024; i++ }
  return `${bytes.toFixed(i ? 1 : 0)} ${UNITS[i]}`
}

export function total() {
  return tables.files.reduce((sum, file) => sum + file.size, 0)
}
```

```tsx
// res/layouts/main.tsx
import { formatSize, total } from "./util"

export default function Main() {
  return (
    <stack id="root" arrange="vertical">
      <label id="size" text={() => "size: " + formatSize(state.size)} />
      <label id="tags" text={() => state.tags.split(",").map((t) => t.trim()).join(" · ")} />
      <label id="sum"  text={() => `${formatSize(total())} in ${tables.files.length} files`} />
      <button id="add" text="Add" onClick={() => { const next = state.count + 1; state.count = next }} />
    </stack>
  )
}
```

A formatter written here works the same whichever language the backend is in, and is
written once.

## What runs when

**Code reached from a binding or a handler runs while the application runs. Everything else
runs when the layout is built.**

| | |
| --- | --- |
| the file's top level, and every component | build time, as always: what it produced is the layout |
| a function a binding or handler calls -- and whatever that calls | run time |
| a top-level value such a function uses, with no call in it: `const UNITS = [...]` | run time too: it ships as code |
| a top-level value made by a call: `const UNITS = load()` | build time: the call ran then, and **the value it made** ships |

So a component is a function and still runs only while building, because no binding calls
it. And `load()` above is never called at run time -- what ships is the array it returned.
A value like that has to be data that can be written out: numbers, text, booleans, arrays,
plain objects, `Map`, `Set`, `Date`, `RegExp`, and top-level functions by name. A class
instance, or a function made by calling something, is refused with its name and line.

A binding is still compiled from its own text, so it still sees nothing of the code around
it -- not a component's `props`, not a local. What it can name is the top level of the
file and its imports.

## What code that runs can reach

| | in a value binding | in a handler |
| --- | --- | --- |
| `state.<name>` | read | read and write |
| `item.<column>` in a row template | read | read |
| `tables.<name>` -- `.length`, `.at(i)`, `for (const row of ...)`, `.map`, `.filter`, `.find`, `.reduce` | read | read |
| `commands.<name>(...)` | refused | call, with arguments; an action returns its result |
| `format.number`, `.percent`, `.bytes`, `.duration`, `.date`, `.relative` | yes | yes |
| `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` | refused | yes |
| the event's value | -- | the handler's parameter |
| `Math`, `String`, `Array`, `JSON`, `Date`, `RegExp`, `Map`, `Set`, classes | yes | yes |
| `rgb`, `mix`, `fade` and the other colour helpers | yes | yes |
| `console.log`, `.warn`, `.error` | to the engine's log | to the engine's log |

Nothing reaches past the interface: no files, no network, no `eval`. And no JSX or
`signal()` -- those build the layout, which has already happened.

## What the build refuses

Each with the binding, or the function and its line:

- a name that is nothing this file, its imports or the language defines;
- `props` inside a binding;
- a name the code *around* the binding declares, when the top level has one too -- the
  binding would read the top-level one, which is not what that code says, so it is refused
  rather than compiled quietly wrong;
- a value binding that writes state or calls a command -- it runs whenever what it reads
  changes, so it has to have no effects. Write from a handler;
- JSX or `signal()` in code that runs later;
- a top-level value that cannot be written out, as above.

## When it goes wrong

A function that throws is reported once, with where in your source it was:

```
binding: text on 'root/risky' - Error: one is not allowed
    at risky (main.tsx:7)
```

The binding keeps the last value it showed and runs again when what it reads changes; a
handler stops where it threw, with its writes so far standing.

One call may run for 50 ms. Past that it is stopped and reported -- a loop that does not end
is a line in the log, not a frozen window. `RDA_SCRIPT_BUDGET_MS` changes the limit.

## What it depends on

A grammar binding's dependencies are read off its text when it is built. A function decides
what it reads as it runs, so a binding that calls one is watched while it runs instead, and
re-runs when exactly what it read changes. `() => state.advanced ? fancy(state.a) : state.b`
depends on `a` while `advanced` is true and on `b` while it is not. A table read makes it
re-run when the table changes.

## Reloading

Saving a file a layout imports rebuilds it, functions included, and the window shows the new
code within a moment. Nothing you did is lost: state lives in signals and tables, which a
reload does not touch, so a counter keeps its count while the function that formats it
changes. A top-level `let` that run-time code changes is the exception -- it starts over,
and the build says so.

## What it costs

Measured on a Release build:

| | |
| --- | --- |
| a grammar binding, as before | 0.07 µs |
| the same, calling a function of yours | 0.13 -- 0.15 µs |
| a whole binding in JavaScript | 0.4 µs |
| starting the runtime, once, the first time a layout needs it | 0.1 ms and about 90 KB |
| loading a layout's functions | well under a millisecond |
| a write that two computed values in state.ts follow | 1.2 µs, against 0.006 µs for one nothing computes from |
| an action, called with its arguments as JSON | 1.3 µs |

A layout that defines no functions starts no runtime at all.

## Saying no to all of it

`rda build --no-runtime-code`, or `-DRDA_RUNTIME_CODE=OFF` for a CMake build, turns every
binding that would need run-time code into a build error naming it -- for an application
that wants *a shipped binary evaluates no JavaScript* to stay true.

`RDA_SHOW_RUNTIME_MODULE=1` prints the module the compiler wrote for each layout, before it
became bytecode: which of your code runs later, and what was snapshotted.

**A blueprint with functions in it is code.** It is trusted the way the executable that
loads it is: load blueprints you built, not ones you were sent.

## In state.ts: computed values and actions

Logic more than one layout needs, or that the backend needs too, goes beside the state it
works on:

```ts
// res/state.ts
export const state = defineState({
  price: 0,
  qty:   { value: 1, doc: "how many" },
  total: { compute: () => state.price * state.qty, doc: "what the cart costs" },
})

export const commands = defineCommands({
  save:   "write the cart out",
  rename: { doc: "rename a line", params: { id: 0, name: "" } },
  add:    { doc: "put more of one in", run: (id: number, count = 1) => { state.qty += count } },
})
```

```tsx
<label text={() => `total: ${state.total}`} />
<button text="Add" onClick={() => commands.add(item.id)} />
<button text="Rename" onClick={() => commands.rename(item.id, state.draft)} />
```

- **`total` is a computed signal.** A binding reads it like any other, the backend reads it
  through its generated accessor, and nothing writes it but the engine. It is recomputed
  inside the write that changed what it read, so it is never a frame behind.
- **`rename` carries its arguments.** What the event meant travels with it, and the backend's
  handler receives `(id, name)` typed in its own language -- no signal declared just to
  carry a value across.
- **`add` is an action.** state.ts answers it, with nothing behind it -- `rda prototype` runs
  it -- and the backend can call it too, through `rda_call`, or bind it to hear of it after.
  Called from code that runs, it returns what it returned.

`defineState`, `defineTables` and `defineCommands` change nothing that is built: they are
there so the editor types `state.qty` in state.ts as the number it is when the code runs,
rather than as the `{ value, doc }` it was declared as. See
[the declaration](../backend/declaration.md#code-in-the-declaration) for the backend's side.

## Formatting

QuickJS has no `Intl`, so `toLocaleString` is not much use. `format` is there instead, in a
binding, a function, state.ts, and at the top level while the layout is built:

```tsx
<label text={() => format.bytes(state.size)} />                         // "1.5 KB"
<label text={() => format.number(state.total, { decimals: 2 })} />      // "1,234.50"
<label text={() => format.date(state.saved, "EEE d MMM, HH:mm")} />     // "Tue 20 Oct, 14:05"
<label text={() => "edited " + format.relative(state.saved)} />        // "edited 5 minutes ago"
```

| | gives | options |
| --- | --- | --- |
| `format.number(x)` | `"1,234,567.891"` | `decimals`, `grouping`, `group`, `point`, `sign` |
| `format.percent(f)` | `"26%"` from `0.256` | the same; no decimals unless asked |
| `format.bytes(n)` | `"1.5 KB"` | `decimals`; `si: true` for 1000 and `kB` |
| `format.duration(ms)` | `"1:02:03"`, `"1:05"` | `style: "short"` for `"1h 2m"` |
| `format.date(t, pattern)` | `"2026-10-20 14:05"` unless a pattern says otherwise | `utc`; `months`, `days`, `am`, `pm` for other languages |
| `format.relative(t)` | `"just now"`, `"5 minutes ago"`, `"in 2 hours"`, `"yesterday"` | a `now` to measure from |

A date pattern has `yyyy yy`, `M MM MMM MMMM`, `d dd`, `E EEE EEEE`, `H HH`, `h hh`,
`m mm`, `s ss`, `S SSS`, `a`, and `'quoted text'`. A time is milliseconds, as `Date.now()`
gives, a `Date`, or text `Date.parse` reads.

`format.relative` is worked out when its binding runs, and nothing about it says "run again
in a minute". To keep one current, read a signal that a timer moves.

## Timers

`setTimeout` and `setInterval` work, on the frame clock: the callback runs on the loop
thread at the first frame at or after its time -- an idle window is woken for it -- and runs
as a handler does.

```ts
// res/state.ts
let hide: number | undefined
export const commands = defineCommands({
  saved: { run: () => {
    state.toast = "Saved"
    clearTimeout(hide)
    hide = setTimeout(() => { state.toast = "" }, 1500)
  } },
})
```

- Only from a handler, an action, or what they call. A value binding and a computed value
  cannot start one -- they run whenever what they read changes -- and neither can the top
  level of a file, which runs once, while it is built.
- A timer goes when the layout or state.ts that started it is unloaded or reloaded.
- An interval runs once a frame at most, and one that falls behind does not catch up in a
  burst.
- `let hide` above is a top-level value run-time code changes, which the build mentions: it
  lives as long as the file is loaded and starts over when it is rebuilt -- exactly what a
  timer's handle wants.

## Not yet

Writing tables from code that runs. See [the design](../architecture/runtime-code.md).

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
