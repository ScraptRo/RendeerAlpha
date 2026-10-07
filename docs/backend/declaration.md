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
| actions | a command carries no state at all. `commands.save()` is a function call, not a value to keep in step |

What is left over is the handful of values a screen actually shows, and those are the ones a
view model would have had anyway.

And it is not the no-JavaScript-at-runtime rule that puts it there. That rule is why the
declaration is *read at build time* and generated into each language rather than parsed by
the running program -- which is what removed the duplication that would otherwise exist.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
