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

---

Back to [the backend index](README.md) · [all documentation](../README.md)
