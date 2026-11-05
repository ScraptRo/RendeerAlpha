# Reacting to change: `watch`, `onOpen` and `onClose`

**Status: `watch` in state.ts is built. `onOpen`, `onClose` and a backend's `Watch` are proposed,
and nothing of them is built yet.** Written on 2026-11-05 against the code as it stood, and what
it says about the engine was read out of the source. [Decided](#decided) records the three
questions it raised and their answers.

Code in an interface runs today because something happened to a widget, or a timer went off.
Two things a developer reaches for have no home:

- **"When this value changes, do this."** Save a setting, clear a field that depended on it,
  tell the backend.
- **"When this screen opens, do this."** Ask for its data, start something, set it up.

The nearest thing was a write inside a value binding, which looked as though it did both and
is refused now, because of what it did when measured:

| Layout | What happened |
| --- | --- |
| `text={() => state.count = 1}`, with a button adding 10 | the click landed and `count` stayed 1: the binding undid it |
| `text={() => state.count = state.count + 1}` | `count` climbed every frame and never settled |
| two labels each writing what the other reads | both climbed and never settled |

The wish was right and the place was wrong. A value binding runs because something it reads
moved, in no order anybody chose, and it is expected to have no effects. This page gives the
same wish two places where effects are safe.

## What exists today

| To run code when | There is |
| --- | --- |
| the reader does something | an event handler: `onClick`, `onChange`, and the rest |
| time passes | `setTimeout`, `setInterval` |
| a layout or a backend asks | an action: a command with `run` in `res/state.ts` |
| a value depends on others | `compute` in `res/state.ts`, which may not have effects |
| the application starts, turns or stops | the backend's start, update and shutdown callbacks |
| a value changes | `watch` on a field in `res/state.ts`, from this page |
| **a screen opens or closes** | **nothing yet: this page's `onOpen` and `onClose`** |

## `watch`

A field in `res/state.ts` may carry a `watch`, as it may carry a `compute`. Like those, it is
not a new kind of thing: it is a handler whose event is "this signal changed".

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

- **It runs as a handler does.** It may read and write state, call commands and start timers,
  under the same time budget. A value binding and a computed value stay read-only.
- **It is told `(now, was)`.** `was` is the value the last time this watch ran, or the starting
  value if it never has.
- **It runs once for a batch.** A handler that writes `count` three times is one call. A value
  that moves and comes back before the watch runs is no call at all: nothing changed.
- **It does not run on load.** A starting value is not a change. A backend's write between
  `define()` and the first frame is one.
- **Any writer counts:** a handler, a timer, an action, a computed value recomputing, another
  watch, a backend, `rda_signal_set_*`.
- **It watches its own field and nothing else, on purpose.** Several signals together are a
  `compute` with a `watch`, as `total` is above. A table is the same: a computed value that reads
  the table, watched. So nothing here tracks dependencies, and nothing here can surprise.
- **Watches run in the order they are declared.**

### When it runs

```
one turn of the loop                     at least every 50 ms, whether or not it draws
  ScriptHost::tick
    the timers that are due
    computed values that read a table
    watches                              <-- new, last
    a backend's watches                  <-- new, once those have settled
  Bindings::applyDirty                   writes to widgets
  Gui::begin                             the walk: handlers fire here
  onUpdate                               Router::update swaps screens here
  Gui::end, then draw if anything earned it
```

- **After the write, not inside it.** A computed value runs inside the write because a backend
  that sets `price` reads `total` on the next line. A watch has effects, so inside the write it
  would run between two lines of whatever is writing, and a handler that sets `a` and then `b`
  would have its reaction fire on a state nobody meant.
- **Before the bindings.** What a watch writes reaches the screen in the same frame, so a watch
  adds no frame of delay. Timers come first so that what they write is seen too.
- **Nothing new has to wake the loop.** `runFrame` runs on every turn and decides about drawing
  at the end, and an idle turn waits at most 50 ms. So a signal nothing displays is still
  watched, and so is an application with no window, because `tick` is outside the GUI.

A handler fires during the walk, after `tick`, so its watch runs at the top of the next turn,
which the handler has already asked for.

### Stopping a runaway

A watch may write the signal it watches. `volume` above has to, and it settles: the second
call changes nothing. So `tick` repeats while anything moved, at most **64 passes**.

Past that, watches are feeding each other. The engine logs an error naming the signals, and takes
their current values as settled, so the next turn does not start the same loop again. Without
that second part, `watch: (now) => { state.count = now + 1 }` would be the climbing binding from
the top of this page. Each message is logged once, as a computed value's error is.

### How it was built

- **`Signals`** has `version(signal)`, bumped when a value changes, the way a table already has
  one. A watch compares a number on each turn. It needs no observer and no place in the dirty
  list, which `Bindings::applyDirty` drains, so nothing about bindings changes.
- **The toolchain.** `StateReader.cpp` accepts `watch` beside `compute`. `ScriptPlanner::addWatch`
  sits beside `addAction`, with the same checks. The analyzer's `emit` writes a third export, `w`,
  beside the `c` and `a` it writes now. `--no-runtime-code` refuses it as it refuses any code in
  state.ts.
- **The types are unchanged, on purpose.** A watch names its parameter types, as an action's `run`
  does. Typing them from the field was tried against every sample in these docs: any constraint
  on `defineState` makes a `compute` that reads `state` circular, so all of those stopped
  type-checking. The editor does not check that `now`'s type matches the field.
- **The runtime.** `ScriptHost::loadState` reads `w` into a list beside the computed signals and
  starts each `was` at the current value, and `unloadState` frees it. `tick` calls the new step
  last, which means moving its early return: today it stops as soon as there are no computed
  values. A state.ts reload starts every `was` again, and a change pending at that moment is not
  delivered.
- **No ABI change.** State code is embedded in every generated module already, so `watch` reaches
  all four languages by regenerating, and `rda prototype` runs it with no backend.

### What was rejected

| Instead | Why not |
| --- | --- |
| Run it inside the write, as `compute` does | effects in the middle of whatever is writing, as above |
| Run it when a handler finishes | misses timers and backends, and runs twice for two handlers in one frame |
| An auto-tracked effect, `watch(() => ...)` | what it reacts to changes with its branches, and it has to run at load to find out. `compute` with `watch` says the same thing and can be read |
| A `<watch>` element in a layout | state outlives a layout, so the reaction would stop whenever its screen did |

## `onOpen` and `onClose`

Ordinary handlers on a layout's root element:

```tsx
export default function Catalogue() {
  return (
    <stack id="root"
           onOpen={() => commands.loadProducts(state.productId)}
           onClose={() => commands.saveDraft(state.draft)}>
      ...
    </stack>
  )
}
```

They go through the compiler like any other handler, so a plain one runs in the grammar and
needs no QuickJS. Anywhere except the root, including a row template, they are refused with the
reason: a widget has no opening of its own, because it is simply there or not.

**The rule: every `onOpen` gets exactly one `onClose`, unless the process dies.**

| What happened | `onClose` of the old | `onOpen` of the new |
| --- | --- | --- |
| The first layout is loaded | -- | yes |
| A route changes, including back and forward | yes | yes |
| Another layout is opened on the same host | yes | yes |
| A `.tsx` is saved, or a new blueprint appears: a reload | no | no |
| A screen fails to load | no: the old one stays | no |
| The application exits normally | yes, before `onShutdown` | -- |
| The process is killed or crashes | no | -- |

### The order during a route change

`Router::show` builds the arriving screen first, on purpose: one that will not load must not take
the running one down. Left alone, that would run the new `onOpen` before the old `onClose`, and
a screen that saves on close and another that reads on open would see each other backwards.

So `LayoutHost` separates building from announcing:

1. Build the new screen. If that fails, stop: neither handler runs.
2. The old screen's `onClose`.
3. The new screen's `onOpen`.

With a cross-fade, `onClose` runs when the screen starts leaving, not when the fade ends, so the
order holds. Its bindings and timers carry on until it is retired.

No first-frame trick is needed. The swap happens in `onUpdate`, after this frame's walk, so
what either handler writes is applied by the next frame's `applyDirty`, before the new tree is
walked for the first time. The reader never sees a screen before its `onOpen` has had its say. An
`onOpen` that writes `state.route` is fine too, because the swap waits for the next `update()`.

### A reload runs neither

The promise of hot reload is that the count survives a save, and `onOpen={() => state.count = 0}`
would break it on every keystroke that saved. A reload is not the reader opening anything.

That has one cost. A layout's timers die with its module on a reload, so
`onOpen={() => setInterval(poll, 1000)}` stops after a save and nothing starts it again. Two
answers, and no magic:

- A reload that cancels timers says so in the log, by file and count, rather than going quiet.
- A timer that has to outlive a reload belongs to state.ts, whose module a layout reload does not
  touch: `onOpen={() => commands.startPolling()}`, with `startPolling` an action.

### `onClose` is already a name

`<popup onClose>` means a press outside it, or Escape. On a layout whose root is a `<popup>`, it
keeps that meaning and the layout has no close handler. That is the only collision, a root popup
is unusual, and the type emitter already handles an element shadowing a common property.

### Bind before load

A command called from `onOpen` runs while the layout loads, so its handler has to be bound by
then, or the call reports "nothing answers commands.x". C++ and Python already bind first. The
Node and C# samples in [the tutorial](../tutorials/backends.md) bind after `loadInterface`, and
they would be reordered, with the rule stated in the backend docs.

### How it would be built

- **`WidgetSchema`:** two `PROP_EVENT`s in the common properties, so they autocomplete. The test
  holding the registry to the schema learns that these two belong to the layout, not to a widget.
- **`LayoutCompiler::readProps`:** refuses them off the root. The blueprint format does not change:
  events are stored by name.
- **`LayoutLoader`:** the root's two events become `EventRunner`s through `makeRunner`, which
  already gives a handler its frame and asks for the next one. They are kept in the instance's
  `ObserverSink` rather than on a widget, because hosts move and the sink moves with them.
- **`LayoutHost`:** `open()` is build and announce. `reload()` announces nothing. A flag makes
  closing once the only way to close.
- **`Router::show`:** the order above.
- **Exit:** a list of open instances, closed after the loop ends and before `onShutdown`. Not from
  a destructor: an application's `LayoutHost` is often a global, and static destruction order has
  caught this engine before.

## From a backend

A slider moves `gain` and the backend has to apply it. Today it polls in its update callback, or
the layout calls a command that only exists to carry the value. A generated `Watch`, the mirror
of `Commands`, says it directly:

```cpp
RDA::State::watchGain([](double now, double was) { audio.setGain(now); });
```

```python
@Watch.gain
def gain_changed(now, was):
    audio.set_gain(now)
```

```javascript
Watch.gain((now, was) => audio.setGain(now))
```

```csharp
Watch.gain((now, was) => audio.SetGain(now));
```

Generated from the declaration, so renaming `gain` is a compile error, as renaming a command is.

C ABI **1.9**, additive, with the same split commands have:

```c
RDA_C_API int rda_signal_bind(const char* name, rda_callback fn, void* user);  // on the loop thread
RDA_C_API int rda_signal_watch(const char* name);                              // queued, to poll
RDA_C_API int rda_signal_next(char* name, int capacity);                       // 0 when none waits
```

Python and C# bind, as they bind commands. Node polls, for the reason `rda_command_watch` gives: a
callback into JavaScript from the loop thread deadlocks.

- **Delivered after state.ts's watches have settled,** so a backend sees where the state ended up
  rather than the steps on the way.
- **The queue carries names, not values.** A backend reads the value when it looks, so each signal
  is queued at most once and the queue cannot overflow, unlike the 256 commands it sits beside.
- **`was` is the last value this backend saw,** kept by the generated code.
- **A backend's own write counts.** One that keeps writing in reaction keeps itself going, one call
  per turn. That is its own loop, and it does not hang the engine.
- **A bound callback runs on the loop thread,** so a slow one delays frames, as a slow command
  handler does.

## What this does not do

- **A layout's own `signal()` has no `watch`.** It would need a function to ship from a build-time
  call. If that is wanted, it can follow without changing anything here.
- **`state.rda.*` and `route` have no `watch`,** because neither is a field anybody declares. A
  screen changing is what `onOpen` is for.
- **There is no widget-level lifetime.** `onOpen` on a `<panel>` that `visible` shows is a
  different feature: `visible` is a value that changes, which is what `watch` is for.

## Build order

Each step stands alone and is useful without the next.

| | Touches | Tested by |
| --- | --- | --- |
| **1. `watch` in state.ts: built** | toolchain, `Signals`, `ScriptHost`. No ABI or blueprint change | unit cases for batching, a value that comes back, `(now, was)`, not running on load, declared order, a clamp settling, a runaway stopped and logged once, a table through `compute`; a ctest driving a click through `rda shot` |
| **2. `onOpen` and `onClose`** | schema, compiler, loader, `LayoutHost`, `Router`, exit. No ABI or blueprint change | a ctest refusing them off the root; `rda shot` reading what `onOpen` wrote; the close-then-open order, with and without a transition; a failed load running neither; a reload running neither; exit running `onClose` once |
| **3. `Watch` from a backend** | codegen in four languages, C ABI 1.9 | the three binding tests, plus a generated-code case per language |

Each step also updates what it changes: `docs/frontend/functions.md`,
`docs/backend/declaration.md`, `docs/frontend/screens.md`, the tutorials, and the skill's
references.

## Decided

Confirmed on 2026-11-05.

1. **`onClose` runs when the application exits,** so that a draft is saved however the screen
   ended. The cost is the list of open instances and one more thing happening during shutdown.
   Without it, the natural first use of `onClose` would quietly do nothing.
2. **A reload runs neither handler,** to keep "the count survives". The cost is the timer case
   above. The alternative was an `onOpen` told whether it was a reload, which would put that
   decision into every handler.
3. **The names stay `onOpen` and `onClose`,** accepting the one collision on a root `<popup>`.
   `onEnter` and `onLeave` would have avoided it, and read less plainly.
