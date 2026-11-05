# A layout as an object

A layout does not have to be JSX. The same elements can be written as plain objects, with
`type` naming the element and `children` what is inside it:

```tsx
export default function Main(): Layout {
  return {
    type: "stack", id: "root", spacing: 8,
    children: [
      { type: "label", id: "title", height: "content", text: () => `Clicked ${state.count} times` },
      { type: "button", id: "add", text: "Add one", width: 110, onClick: () => state.count++ },
    ],
  }
}
```

That is the same layout as this, and it compiles to the same bytes:

```tsx
export default function Main() {
  return (
    <stack id="root" spacing={8}>
      <label id="title" height="content" text={() => `Clicked ${state.count} times`} />
      <button id="add" text="Add one" width={110} onClick={() => state.count++} />
    </stack>
  )
}
```

Nothing else changes. Every element, property, binding and handler means what it means in
JSX, and a value, a binding or a handler is written the way it would be after the `=`. The object
is read when the layout is built, like JSX is, so a loop or an `Array.map` that makes children
runs then and not while the application runs.

## The `Layout` type

`Layout` is generated into `rda.d.ts` from the same table as the JSX elements, so `: Layout`
gets you the same checking: a misspelt element, a property the element does not have, or a value
of the wrong type is underlined, in a child as much as at the top. Without the annotation the
editor has nothing to check the object against.

## What goes where

| | |
| --- | --- |
| `type` | the element's name, or a component: `{ type: Card, title: "Orders" }` |
| `children` | one element or a list of them. A `false` or a `null` is dropped, as in JSX, so `state && {...}` reads as it does there |
| anything else | a property of the element, or a prop of the component |

- **A row template** returns an object too: `row: (item) => ({ type: "label", text: () => item.name })`.
- **The file** still ends in `.tsx`, because that is what `rda build` looks for in `res/layouts/`.
  A `.ts` file there is a helper to import. `rda layout` will compile either.
- **The default export** may be the object itself, if the layout takes no props:
  `export default { type: "stack", children: [...] }`.
- **A handler may be a method:** `onClick() { state.count++ }`. That form runs as JavaScript
  rather than in the grammar, so an arrow is the better habit for anything short.
- **One object can be used twice.** A constant shared by two parents makes two widgets.
- **The two forms mix** when the layout is built, but `Layout` only describes objects, so a JSX
  element inside one is underlined in the editor.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
