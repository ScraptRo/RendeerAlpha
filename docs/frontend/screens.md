# Screens

More than one layout, one showing at a time, with the name of that one held in a signal
called `route`.

```ts
// res/state.ts
export const routes = {
  home:      { layout: "home" },
  catalogue: { layout: "catalogue", params: ["productId"] },
}
```

Navigating is an ordinary write:

```tsx
<button text="Catalogue" onClick={() => state.route = "catalogue"} />
<button text="Item 12"
        onClick={() => { state.productId = 12; state.route = "catalogue" }} />
```

It needed no new grammar and no new concept because "which screen is showing" is state,
and this engine has one answer for state. It is reactive for the same reason: a label
bound to `state.route` follows the screen without being told navigation exists.

Declaring routes also declares `back` and `forward` as commands, and the router binds
them, so a back button is an ordinary button:

```tsx
<button text="<" onClick={() => commands.back()} />
```

`params` names signals that are a screen's arguments. They are ordinary signals; naming
them is what makes going back correct, because history records the route *and* the value
of every param that route declared, and restores both.

The swap is total — the widgets are thrown away and the new layout is built — which is
only correct because signals live outside the tree being replaced. That is the same
property that makes hot reload here simpler than it looks, and it is why a screen can be
left and returned to without anything being preserved by hand.

The application drives it: `Router::open` once, `Router::update` once a frame. See
[the backend](../backend/README.md).

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
