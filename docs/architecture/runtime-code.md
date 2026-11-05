# Code that runs: functions in a layout

**Status: built** -- functions in a layout, logic in the state, and the helpers QuickJS
lacks: everything in [Phases](#phases). The page was written before the code, so the
decisions were made once, here; where building it changed one, the text below says what it
became and why. How to use it is
[functions](../frontend/functions.md), and the backend's side is
[the declaration](../backend/declaration.md#code-in-the-declaration).

## Why

A binding can say `state.price * state.qty` and nothing longer. Everything past the
[expression grammar](../frontend/expressions.md) -- formatting a size, validating a field,
summing a column, deciding which of four messages to show -- has to be written in the
backend, as a signal the backend keeps up to date. So a small feature is written twice:
once as the computation in C++, Python, Node or C#, and once as the signal and the binding
that shows it. And it is written again for every backend language, because a formatter
written in Python does nothing for the same interface driven from C++.

That trade bought speed and a short promise -- *nothing in a shipped binary evaluates
JavaScript* -- and it costs more development time than it saves running time. An interface
is mostly code that runs when somebody does something, a few times a second at most, and a
microsecond spent there is invisible.

So a layout gets to define functions, and they run.

## The rule: where it is reached from

> **Code reached from a binding or a handler runs at run time. Everything else runs when
> the layout is built.**

A binding is anything written as `{() => ...}` on a property; a handler is the same on an
event. Whatever function such a binding calls, and whatever *that* calls, is run-time code.
The module's top level, and every component the default export calls, runs at build time
exactly as it does today.

```tsx
const UNITS = ["B", "KB", "MB", "GB"]          // build time: evaluated once, shipped as data
signal("query", "")                            // build time: a declaration, as today

function formatSize(bytes: number) {           // run time: reached from a binding below
  let i = 0
  while (bytes >= 1024 && i < UNITS.length - 1) { bytes /= 1024; i++ }
  return `${bytes.toFixed(i ? 1 : 0)} ${UNITS[i]}`
}

function Field(props: { label: string; value: () => string }) {   // build time: a component
  return <stack id="f"> ... </stack>
}

export default function Main() {              // build time
  return <label id="size" text={() => formatSize(state.size)} />
}
```

This is the rule asked for -- *a defined function can run, a call at the top level is
compile time* -- made precise in the one place it was not. A component is a defined function
and must not run at run time, so the test cannot be *defined*. It is *reached*: the compiler
starts from the bindings and follows names. A function reached both ways (a helper a
component also calls while building) is both, which is fine -- it is code, and code can run
twice.

What a run-time function refers to at the top level comes along in one of two ways:

| It refers to | It arrives as |
| --- | --- |
| a function (declared, `const f = () =>`, or imported) | code, compiled to bytecode |
| a class | code |
| a value whose initializer calls nothing -- a literal, an array of them, a name | code too: evaluating it again does exactly what it did the first time |
| a value whose initializer calls something | **its value at the end of the build**, written into the bytecode as a literal |

The middle-but-one row is the one building it added. The plan snapshotted every value, and
esbuild's own helpers -- `var __defProp = Object.defineProperty` -- would then have been a
native function that cannot be written out. An initializer with no call, no `new` and no
assignment in it has no work to repeat and nothing to lose by running again, so it ships as
written; only a value a *call* made is a snapshot. That is the rule applied to data: a
top-level call ran at build time, and what it produced is the thing that exists.

A snapshot may be numbers, text, booleans, arrays, plain objects, `Map`, `Set`, `Date`,
`RegExp`, and a top-level function by name. Anything else -- a class instance, a function
made by calling something, a value that contains itself -- is a build error naming it and
its line. A top-level value that run-time code assigns to is warned about at build: it
belongs to the run time from then on, and starts over on hot reload, which nothing else a
layout holds does.

### What does not change

- **A binding still closes over nothing.** It is compiled from its own text, which is why it
  can name a top-level function -- names resolve at the module's top level -- and why it
  still cannot read a component's `props` inside a thunk. That rule and its error stay as
  they are in [components](components.md).
- **A binding the grammar already accepts is compiled exactly as before**, to the stack
  VM, with its dependencies known at build time. An interface that calls no function pays
  nothing, not even a JavaScript runtime: one is created the first time a layout that
  needs it is loaded.
- **State lives in signals and tables, never in JavaScript.** See below; most of the design
  follows from it.

## Two ways a binding runs

| The binding | Compiled to | Dependencies |
| --- | --- | --- |
| inside the grammar, no calls: `state.a * state.b` | the stack VM, as today | known at build time |
| inside the grammar, calling a function: `formatSize(state.size)` | the stack VM, with one `CallScript` instruction | the arguments' at build time, plus whatever the function reads, at run time |
| outside the grammar: `state.tags.split(",").length`, a block body, a local | a JavaScript function, as a whole | recorded at run time |

The third row is what makes the boundary disappear for whoever writes the layout: a thunk
that does not fit the grammar is no longer a build error, it is JavaScript. A refusal is
kept only for what is wrong rather than merely bigger -- an undeclared `state.` name, a
`commands.` call in a value binding, a free name that is nothing at the top level.

`CallScript` takes a function index and an argument count, pops the arguments, and pushes
the result converted to a `Value` -- number, bool or text, the three a binding has always
produced. A function returning anything else is converted the way a template literal would
convert it, and warned about once.

### Dependencies read while running

A JavaScript binding cannot be read for the signals it uses, because a function can decide
at run time what to read. So it is watched instead. While it runs, every read through
`state`, `item` and `tables` is recorded; afterwards the binding observes exactly that set,
and the next run replaces it. This is how Solid and MobX track dependencies, and it is
correct for branches: `() => state.advanced ? fancy(state.x) : state.y` depends on `x` or
on `y`, never on both at once.

Tables have no per-signal observers. A binding that read a table records the table and its
version, and is re-run when that version moves -- the same rule a `<list>` already uses.

## What run-time code can reach

| | In a value binding | In a handler, or a function a handler calls |
| --- | --- | --- |
| `state.name` | read | read and write |
| `item.column` (inside a row template) | read | read |
| `tables.name` -- `.length`, `.at(i).column`, iteration | read | read; writing stays the backend's (see [decided](#decided)) |
| `commands.name()` | refused: a binding that has an effect runs whenever anything it reads changes | call |
| the event's value | -- | the handler's parameter |
| `Math`, `String`, `Array`, `JSON`, `Date`, `RegExp`, `Map`, `Set`, classes | yes | yes |
| `format.number`, `.percent`, `.bytes`, `.duration`, `.date`, `.relative` (see below) | yes | yes |
| `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` (see below) | refused | yes |
| `console.log`, `console.warn` | yes, to the engine log | yes |

Writes from a value binding are refused at run time with the binding's name, because
nothing at build time can see them coming. A write in a handler goes straight to the
signal, synchronously, as `StoreSignal` does; the backend sees it the way it sees any
other write.

Not there, on purpose: files, the network, processes, `eval` and `new Function`, and `h()`
or `signal()` -- run-time code reads and
writes the interface's state; it does not build widgets or declare anything.

### Why state stays outside

A function is code, and code holds nothing between calls that anybody else needs. Every
value worth keeping is a signal or a table cell, which is to say the engine's. Three things
come from that for nothing:

- **Hot reload swaps code without losing state.** A rebuilt layout throws its run-time
  module away and loads the new one, and every signal is still where it was.
- **The backend sees every change.** There is no second store for a script to write to
  that the backend would have to be told about.
- **Every backend gets the logic.** A formatter written in a layout works the same under a
  C++, Python, Node or C# backend -- written once, which is the whole point.

## The pipeline

What changes in [what the compiler does](two-halves.md#what-the-layout-compiler-actually-does):

1. **Transform.** esbuild bundles the `.tsx` and its imports as an **ES module** into one
   file, rather than an IIFE. Every top-level name of every imported file then lives in
   one flat scope, renamed apart where they collide -- which is the scope a binding's names
   resolve in.
2. **Evaluate.** The bundle's closing `export { ... }` clause is taken off and the rest
   wrapped in a function that returns the default export and a reader -- `get(name)`, a
   direct `eval` inside the wrapper, which is the one thing that can see a function's local
   names from outside. Its top level stays its own, as it was in the IIFE. The default
   export is called and the tree flattened, as before.
3. **Parse, if needed.** The bundle is parsed with [acorn](https://github.com/acornjs/acorn),
   run inside the QuickJS the compiler already has: QuickJS can compile JavaScript but not
   say what is in it, and acorn gives the top-level declarations, each function's free names
   and each one's exact range. **Only when a binding needs it** -- the first `name(` the
   grammar meets, or the first binding it refuses. A layout whose bindings are all grammar
   never loads acorn, and compiles exactly as fast as before (2 ms against 60). acorn is
   vendored and embedded as bytes; nothing at run time calls it.
4. **Reach.** Every binding is offered to the expression parser first. One it accepts and
   that names no function is done. Otherwise its free names, and transitively those of
   every function it reaches, are resolved against the top-level table: functions become
   code, other values are read out of the evaluated script and written as literals.
5. **Emit.** One run-time module per layout: the snapshotted values, the reached functions'
   source cut out of the bundle by range, and a table naming each export and each JavaScript
   binding. QuickJS compiles it (`JS_EVAL_FLAG_COMPILE_ONLY`) and writes it as bytecode
   (`JS_WriteObject`, `JS_WRITE_OBJ_BYTECODE`).
6. **Store.** The blueprint gains a script section after its other sections: the QuickJS
   version, the bytecode, and one `file:line` per line of the module, worked out through
   esbuild's source map -- column by column for a binding, since a line of reprinted JSX can
   start with a tag that opened lines earlier. A binding that is JavaScript is a one-
   instruction program, `RunScript "%3"`, so it is still a *VM* binding to everything that
   holds one; the blueprint needed no binding kind. A layout with no functions is written as
   version 2, byte for byte what it was; one with them is version 3, which an older engine
   refuses by its version.

TypeScript is never parsed at run time and no source text is either: what ships is
bytecode. QuickJS bytecode is tied to the QuickJS that wrote it, so the section carries the
engine's version and a blueprint from another build is refused with "rebuild" rather than
read.

## At run time

- **One QuickJS runtime per process, created lazily**, the first time a blueprint with a
  script section loads. Each layout's module is loaded into a namespace of its own, so two
  layouts may each have a `formatSize` and a rebuilt layout replaces only its own.
- **Only on the loop thread.** Bindings and handlers already run there; nothing else
  touches the runtime. Backend calls into it (below) are queued to the loop.
- **A sandbox.** A raw context given the language's own objects and nothing else -- no
  `eval`, no file, process or network -- and one object of natives, `__rda`, through which
  the module's prelude builds `state`, `commands`, `tables` and `console`. Its own host,
  `Layout/ScriptHost`, rather than `src/Scripting/`, which is the 3D scene's and off by
  default.
- **A budget.** An interrupt handler stops any one call that runs past 50 ms with "took too
  long" and the function's name, so an infinite loop is a line in the log instead of a
  frozen window. A memory limit does the same for a runaway allocation.
- **Failures are local.** A value binding that throws logs once and keeps its last value; a
  handler that throws logs and stops, its writes so far standing, the way a handler's
  statements do today. Messages carry the `.tsx` line through the source map.

Nothing here allocates per frame: a binding runs when something it read changed, and a
handler when something happened.

## Logic in the state (phase 2)

Phase 1 put code in a layout. Logic that more than one layout needs, or that the backend
needs as well, belongs with the state it works on: `res/state.ts`. Three things go there,
and none of them is a new kind of thing -- a computed value is a signal, an action is a
command, and a command's arguments are data travelling with the event that asked for it.

```ts
export const state = defineState({
  price: 0,
  qty:   { value: 1, doc: "how many" },
  total: { compute: () => state.price * state.qty, doc: "what the cart costs" },
})

export const commands = defineCommands({
  save:   "write the cart out",                                   // the backend answers
  rename: { doc: "rename a line", params: { id: 0, name: "" } },  // the backend, with data
  add: {                                                          // state.ts answers
    doc: "put more of one in the cart",
    run: (id: number, count = 1) => { state.qty += count; return state.qty },
  },
})
```

### Commands carry their arguments

A command used to take nothing: the interface wrote what the backend needed into state,
then asked. That is two steps for one intent, a signal declared only to carry a value
across, and a race in a list -- two clicks on two rows before the backend has looked, and
it reads the second row twice. So the data travels with the event:

```tsx
<button text="Rename" onClick={() => commands.rename(item.id, state.draft)} />
```

A command's parameters are declared by example and in order, the way state is: `params:
{ id: 0, name: "" }` is a number, then a text. The call is positional, in the grammar and
in JavaScript alike, and what the backend receives is typed in its own language:

| | A handler with arguments |
| --- | --- |
| C++ | `State::onRename([](double id, const std::string& name) { ... })` |
| Python | `@Commands.rename` over `def rename(id: float, name: str)` |
| Node | `Commands.rename((id, name) => { ... })` |
| C# | `Commands.Rename((double id, string name) => { ... })` |

Underneath it is one JSON array, the one shape every one of those languages reads without
help. The C ABI hands it out with `rda_command_args()`: inside a bound callback, the
arguments of the command running; after `rda_poll_command`, those of the command it just
returned. A callback written before this existed keeps working and simply never asks.

Signals and tables stay what they were for: what the interface shows, kept and observed.
An argument is what one event meant at the moment it happened, and nothing observes it.

### Actions: commands state.ts answers

A command with a `run` is an **action**: state.ts answers it, inside the engine, with
nothing behind it. A layout calls it exactly as it calls any other command --
`commands.add(item.id)` -- and cannot tell which kind it named. That is the point: whether
the work is a line of TypeScript or a database write is the application's business, and it
can move from one to the other without a layout changing.

- It runs as a handler does: on the loop thread, reading and writing state, calling other
  commands, within the same 50 ms. Called from JavaScript it returns what it returned.
- Its parameters are its function's, typed by TypeScript.
- The backend may bind it too, and then watches it: the action runs first, and if it did
  not throw, the backend's handler runs after it with the same arguments.
- The backend can call it. **`rda_call(name, args_json)`** runs a command and returns what
  it returned, as JSON -- which is how a validation rule written once in state.ts is also
  the backend's rule before it trusts what it was sent. A backend command called this way
  runs its handler and returns `null`.
- `rda prototype` runs actions, so an interface with no backend no longer does nothing.

### Computed signals

A field with `compute` is a **computed signal**. It is a real signal: a binding reads it
with no script call, the backend reads it through its generated accessor, and nothing
writes it but the engine -- a handler, a script and the C ABI are all refused, and no
setter is generated.

- Its type and starting value are what it returns when run once at build, against the
  declared starting values with every table empty. One that cannot run there names its
  type with `value`, which is then the starting value as well.
- It is recomputed **inside the write that changed what it read**, before that write
  returns. A backend that sets `price` and reads `total` on the next line reads the new
  total, and a frame is never drawn with an old total beside a new price. What it reads is
  recorded while it runs, as a JavaScript binding's is; a table it read is checked once a
  frame.
- One that reads itself, directly or around a loop of others, is stopped and logged rather
  than recomputed for ever.

### Watches

A field with `watch` runs that function when its value changes. Where a computed value runs
inside the write and may have no effects, a watch runs after the writes have finished and may
have any a handler may. It runs at the end of `ScriptHost::tick`, once timers and computed values
have settled and before the bindings are applied. [Reacting to change](watch-and-on-open.md) is
the reasoning, and what the same idea becomes for a screen and for a backend.

### Typed for TypeScript: defineState, defineTables, defineCommands

Run-time code in state.ts names `state`, `tables` and `commands` and gets the live ones --
the same objects a layout's code gets. TypeScript, left alone, types `state` as the
declaration, so `state.qty` is `{ value: number, doc: string }` and `state.price *
state.qty` is an error. The three helpers change nothing that is built; they are typed so
that what the editor sees is what the code sees when it runs. A file with no code in it
needs none of them, and one with code but without them still builds -- only the editor
complains.

`rda.d.ts` declares them, and types a layout's `commands` from state.ts itself --
`typeof import("../state")` -- so an action's signature is its function's, with no copy to
drift.

### Where it ships

`rda build` compiles state.ts's run-time code the way it compiles a layout's: the same
bundle, reachability, snapshots and bytecode. The bytes go **into the generated state
module** -- `RdaState.h`, `state.py`, `state.mjs`, `state.cs` -- and `define()` loads them,
through `rda_load_state_code` for the languages behind the C ABI. So the code is there
before any interface is loaded, whichever language the backend is written in, and there is
no second file to ship or path to get wrong. A blueprint does not change; state code is not
in one.

Hot reload covers it wherever something can compile it: `rda prototype` reloads
state.ts's code when it is saved and keeps every value, as a layout's reload does. A change
to what it *declares* -- a new signal -- needs the backend regenerated, and is said so
rather than half applied.

### What changes in the C ABI

1.8: three calls added, nothing else moved.

```c
const char* rda_command_args(void);                             // a JSON array; "[]" for none
const char* rda_call(const char* name, const char* args_json);  // the result as JSON, or NULL
int         rda_load_state_code(const void* bytes, int size);
```

The two strings are the engine's, valid on the calling thread until its next call of the
same function -- the shape `rda_last_error` already has. A string the engine keeps, rather
than a buffer the caller sizes, because the two-call shape the other readers use would run
an action twice.

## Engine helpers (phase 3)

### Formatting

QuickJS has no `Intl`, so `toLocaleString` and friends are not what an interface needs.
`format` is:

| | |
| --- | --- |
| `format.number(1234567.891)` | `"1,234,567.891"` -- up to three decimals unless `{ decimals }`; `{ grouping, group, point, sign }` |
| `format.percent(0.256)` | `"26%"` |
| `format.bytes(1536)` | `"1.5 KB"`; kilo is 1024 unless `{ si: true }` |
| `format.duration(3723000)` | `"1:02:03"`, or `"1h 2m"` with `{ style: "short" }` -- milliseconds |
| `format.date(t, "EEE d MMM yyyy, HH:mm")` | `"Tue 20 Oct 2026, 14:05"`; local time unless `{ utc: true }` |
| `format.relative(t)` | `"just now"`, `"5 minutes ago"`, `"in 2 hours"`, `"yesterday"` |

The plan said natively. It is JavaScript instead, compiled into a module **only when the
module's code names `format`**, the way the colour helpers are -- so a layout that formats
nothing carries none of it -- and the same text is defined while a layout or state.ts is
built, so `const LIMIT = format.bytes(1 << 20)` at the top level gives at build what it
would give later. Native would have meant two implementations, one for each side of the
build, and what these do is string work QuickJS already does well. Names are English;
`format.date` takes `{ months, days, am, pm }` for any others.

### Timers

`setTimeout`, `setInterval`, `clearTimeout` and `clearInterval`, with their usual
signatures, on the frame clock:

- **A callback goes off on the loop thread, at the first frame at or after its time**, and
  runs as a handler does: it may write state and call commands, within the same 50 ms. A
  timer due while the window idles wakes it -- the idle wait is never longer than the time
  to the next one, so a 400 ms timer in an idle window went off at 400 to 412 ms.
- **Only where something may happen.** A value binding and a computed value cannot start
  one, at build where that can be seen and at run time where it cannot: each runs whenever
  what it read changed, and a timer is something happening.
- **A timer belongs to the module that started it** -- a layout, or state.ts -- and goes
  when that module is unloaded or reloaded, so a rebuilt layout does not keep running the
  old one's code. The prelude passes the engine its module's id with every call; that is
  all the extra the module wrapper takes.
- **Bounded.** An interval runs once a frame at most, never sooner than a millisecond
  apart; one that falls behind does not fire in a burst to catch up; ten thousand waiting
  at once is refused, since that many is a timer started in a loop and never cleared.
- **At the top level they are refused** with a message saying why: the top level runs
  once, while building, and a timer started there would have nowhere to go off.

What it makes possible is mostly small and constant: a toast that clears itself, a search
that waits for the typing to stop, a clock, a retry. An interval started by an action keeps
going for as long as state.ts is loaded.

## The promise, reworded

*A shipped binary evaluates no JavaScript* becomes:

> **No TypeScript at run time.** What you write is compiled to bytecode when you build. A
> binding that calls no function runs exactly as it always has, and an interface that
> defines none never creates a JavaScript runtime at all.

For an application that wants the old promise whole, `rda build --no-runtime-code` (and
`RDA_RUNTIME_CODE=OFF` for a CMake build) fails the build at the first binding that would
need run-time code, naming it.

`RDA_ENABLE_SCRIPTING` and `src/Scripting/` are a separate, older thing -- a host for the 3D
scene -- and stay as they are.

## Phases

1. **Functions in a layout** -- built. The ES-module bundle, acorn, reachability and snapshotting;
   the script section; `CallScript`; JavaScript bindings and handlers; run-time dependency
   tracking for signals and tables; the sandbox, the budget, source-mapped errors; hot
   reload; `--no-runtime-code`.
2. **Logic in the state** -- built. Computed signals, kept current inside the write; actions,
   which are commands state.ts answers; arguments on every command, from the grammar,
   JavaScript and the C ABI; `rda_call` and `rda_load_state_code`; the code embedded in all
   four generated modules, with read-only computed accessors and handlers typed from
   `params`; `defineState`, `defineTables` and `defineCommands` for the editor; state.ts
   reloading in `rda prototype`.
3. **What an interface needs that QuickJS lacks** -- built. `format`, compiled in where it
   is named and defined at build time too; `setTimeout` and `setInterval` on the frame
   clock, owned by the module that started them, waking an idle window on time.

## How it is checked

- The compiler: which names are code and which are values; a snapshot that cannot be
  written; a component that must not be reached; the grammar fast path still taken where
  it applies. Run through the existing `rda_*` tests, which already have esbuild.
- The run time, in `rendeer_tests`: a dependency set that changes with a branch, a table
  read re-running on a version change, a write refused in a value binding, the budget
  stopping a loop, a throw keeping the last value.
- End to end with `rda shot`: a formatter, a validator under a `<textfield>`, a sum over a
  table, and hot reload keeping state while the code changes.
- Measured, on a Release build: a grammar binding 0.07 µs; the same calling a layout
  function 0.13 -- 0.15 µs; a whole binding in JavaScript 0.41 µs, 0.46 µs watched. The
  runtime and its context, created once, 0.11 ms and 91 KB; a small module loaded in
  0.013 ms. A layout's bytecode is a few kilobytes, about three of them the module's
  prelude. A write that two computed signals follow, 1.2 µs, where a write nothing computes
  from is 0.006 µs; an action called with JSON arguments, 1.3 µs.

What building it found, besides:

- **The compile needed a bigger stack.** acorn is a recursive-descent parser running in an
  interpreter that recurses in C for every JavaScript call, and in a Debug build those
  frames are large: reading a small bundle overflowed the main thread's megabyte and took
  `rda` down before QuickJS's own limit noticed. A compile runs on a thread of its own with
  64 MB reserved, and QuickJS's limit is set just under it, so running out is an error.
- **A reload could follow freed widgets.** The loop walks every window, calls `onUpdate`,
  then ends each frame -- and `onUpdate` is where a layout reloads. Everything the walk noted
  by pointer for the end of the frame, a screen reader's tree among it, pointed at widgets
  the reload had just destroyed. It took a screen-reader client and a press to show, which
  is why it had not. The GUI now counts widgets destroyed since its walk began and, if any
  were, follows none of those pointers that frame. Not this feature's bug; this feature's
  hot-reload test found it.

What building phase 2 found:

- **Observer ids moved into Signals.** A computed signal observes the signals it read the way
  a binding does, but it is not a binding, and the binding runtime was where ids came from --
  so the script host would have pulled every widget type into anything that loads state code,
  the unit tests included. Signals indexes its bookkeeping by observer id already, so it now
  hands them out, and the binding runtime keeps a hole where another kind of observer has one.
- **Eager observers run after the write has finished marking, at the outermost write.** A
  recompute writes its own signal, which queues whoever reads that; one loop runs the queue
  in order rather than a recursion as deep as the chain, and a pair that keep changing each
  other is stopped after ten thousand runs with a line in the log. Running one in the middle
  of marking would have walked an observer list that the recompute was rewriting.
- **A computed value's type is found by running it.** At build its function closes over the
  module's own `state`, which is the declaration -- so the declaration objects are turned,
  in place, into the starting values they describe, and only after every one of them has been
  read: the first version did it before the tables were read and lost them. `commands` is
  replaced too, by functions that throw the refusal a computed value calling one deserves. A
  starting value of NaN -- an average over an empty table -- is written as 0, since no
  language spells NaN the same way and C++ does not spell it at all.
- **The helpers' types were checked against the TypeScript the project uses** (7.0): the
  self-reference `defineState` needs, `typeof import("../state")` from `rda.d.ts`, and an
  action's signature reaching a layout. A plain `export const state = {...}` still builds; the
  editor is all it costs.
- **`rda build` checks state.ts's code whenever it writes the types**, not only when a backend
  module is asked for, or a mistake in an action would wait for whoever generated one.
- **The C++ header is compiled into the unit tests and run**, from a declaration with a
  computed value over a table, a command with three typed arguments and an action, because
  nothing else in the build compiles a header with code in it.
- **Checked end to end** in all three FFI languages against a running engine: a computed value
  current as soon as what it reads is, and refused a write; arguments reaching a typed
  handler, non-ASCII included; `rda_call` returning an action's result; a bound backend
  watching an action. And live: state.ts edited under a running `rda prototype`, the new code
  in 0.36 s, every value kept.

What building phase 3 found:

- **The helpers a file is built with were in front of its source**, so a frame inside one
  was reported as a line of the layout -- the timer refusal at first said `main.tsx:239`
  about a file of three lines. They are evaluated as a script of their own now, named
  `<rda helpers>`, for layouts and state.ts alike.
- **The frame clock had to tick without a window.** Computed values over a table and timers
  are both asked once a frame, and that was inside the GUI's part of the frame; an
  application with state and no window would have had neither. It is ahead of the GUI now.
- **A timer started by a timer waits for the next frame**, and one cleared by an earlier
  callback in the same frame does not go off -- each due timer is looked up again before it
  runs, since a callback can start and clear others and move the list under it.

## Decided

These were the open questions; what they became, and why.

1. **Data travels with the event; tables stay the backend's.** The question was whether
   run-time code may write tables. The better answer was to the question underneath it --
   how data gets from the interface to the backend -- and it is
   [arguments](#commands-carry-their-arguments). Preallocated blocks, signals and tables,
   remain what the interface shows; what an event means arrives with the event. Writing a
   table from run-time code would make the backend one of two owners of its own rows, and
   with arguments most of the reason to want it is gone. Revisited if a backend-less
   prototype turns out to need it.
2. **Timers, on the frame clock** -- built, see [timers](#timers). `setTimeout` and `setInterval`, run on the loop
   thread at the first frame at or after their time, an idle window woken for them. Not
   `std::async`: QuickJS is one runtime on one thread, and so is the state it touches, so a
   callback on another thread could only queue itself back to the loop -- the frame clock
   with extra steps and a lock. Work that is genuinely slow, a file or a request, is the
   backend's, and now reaches it with its data.
3. **Actions are commands.** Answered by state.ts, called like any other command, watched
   by the backend if it binds one. See [actions](#actions-commands-statets-answers).

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
