# The declaration is the source of truth

One file, `res/state.ts`, and the build generates every side from it.

```ts
export const state = {
  count:  { value: 0,  doc: "how many times the button has been pressed" },
  title:  { value: "", doc: "what the window is showing" },
  ready:  false,
}

export const commands = {
  save: "write the notes out",
}

export const tables = {
  products: {
    title: { value: "", doc: "what it is called" },
    price: { value: 0,  doc: "in whole currency units" },
    inStock: false,
  },
}

export const routes = {
  home:      { layout: "home" },
  catalogue: { layout: "catalogue", params: ["productId"] },
}
```

A field's type is whatever its initial value is. The long form `{ value, doc }` takes the
place of a bare value where one is worth documenting — and that documentation reaches
every generated side: on the accessor in C++, on the property in Python, on the getter in
JavaScript, and in the tooltip an editor shows for all three.

| the build runs | and generates |
| --- | --- |
| `rda state  decl.ts out.h` | `RdaState.h` — the C++ |
| `rda python decl.ts out.py` | `state.py` — the Python |
| `rda node   decl.ts out.mjs` | `state.mjs` — the JavaScript |
| `rda csharp decl.ts out.cs` | `state.cs` — the C# |
| `rda types  out.d.ts theme.ts decl.ts` | `rda.d.ts` — what a layout is checked against |

So `state.count` in a layout, `State::count()` in C++ and `State.count` in Python, Node or
C# are the same signal **by construction**, and a misspelling is an error on whichever side
made it rather than a value that quietly holds zero forever.

Declaring `routes` adds two things for free: a `route` signal holding the name of the
screen showing, and `back` / `forward` as commands.

## Code in the declaration

A declaration may also say what is worked out and what is answered, and the generated
module carries that code to every backend:

```ts
export const state = defineState({
  price: 0,
  qty:   { value: 1, doc: "how many" },
  total: { compute: () => state.price * state.qty, doc: "what the cart costs" },
})

export const commands = defineCommands({
  save:     "write the cart out",
  rename:   { doc: "rename a line", params: { id: 0, name: "" } },
  validate: { doc: "is this a code we sell", run: (code: string) => /^[A-Z]{3}-\d{4}$/.test(code) },
})
```

| | what it is | on the backend |
| --- | --- | --- |
| `total: { compute }` | a computed signal: worked out by state.ts, current the moment what it read changes | read like any signal; no setter, and the C ABI refuses a write |
| `rename: { params }` | a command with arguments, declared by example and in order | a handler typed from them -- `onRename([](double id, const std::string& name) {...})` |
| `validate: { run }` | an action: state.ts answers it | callable -- `call("validate", "ABC-1234")` returns `true` -- and bindable, to hear of it after |

A computed value's type and starting value are what it returned when the build ran it once,
against the declared starting values with every table empty; one that cannot run there gives
`value` as well. `defineState`, `defineTables` and `defineCommands` change nothing that is
built -- they make the editor type `state` in the file the way it is when the code runs.

The code is compiled to QuickJS bytecode by `rda build` and **embedded in the generated
module** -- `RdaState.h`, `state.py`, `state.mjs`, `state.cs` -- and `define()` loads it, so
it is running before any interface is loaded and there is no second file to ship. A rule
written once here -- `validate` above -- is then the interface's rule and the backend's
rule in every language, which is the point of putting it here. See
[code that runs](../architecture/runtime-code.md#logic-in-the-state-phase-2).

## "So I write my state twice?"

The usual first reaction, and worth answering precisely, because half of it is wrong and
half of it is a real cost.

**The declaration is not written twice.** It is one file, and `State::count()` in C++,
`State.count` in Python and `state.count` in a layout are all generated from it. Nothing is
kept in step by hand, and a misspelling is a compile error on whichever side made it.

**What can be a second copy is your own model.** If an application already holds the number
-- `document.count` in its own struct -- then the interface's `count` is a *second* place
that number lives, and something has to write one when the other moves. That is real, and
it is the price of where the seam is.

It is where it is on purpose. The engine knows signals, tables and commands, and nothing
about your types. That is exactly what lets the backend be C++, Python, Node, C# or anything
with an FFI, in its own process, built by its own toolchain -- an interface that read your
model directly would have to be compiled against your headers, which is the one thing four
languages cannot share. The declaration is the interface's state, which is a *view* of
yours, and the generated accessors are the only thing that crosses.

In practice three things keep it small:

| | |
| --- | --- |
| state only the interface cares about | which panel is open, what is typed, which tab is showing. The signal table is its only home, and there is nothing to mirror |
| rows | a `<list>` reads a table. A backend fills it from whatever it already has, rather than declaring a signal per row |
| commands | a command keeps no state at all: `commands.save()` is a function call, and what it needs travels with it as arguments |
| derived values | a total, a label, a flag worked out from others is `compute`d in the declaration, once, rather than kept in step by every backend |

What is left over is the handful of values a screen actually shows, and those are the ones a
view model would have had anyway.

And it is not the no-JavaScript-at-runtime rule that puts it there. That rule is why the
declaration is *read at build time* and generated into each language rather than parsed by
the running program -- which is what removed the duplication that would otherwise exist.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
