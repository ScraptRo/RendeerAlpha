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

## Opening and closing

A layout says what happens when it opens and closes, on its root element:

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

They are ordinary handlers, so they may write state, call commands and start timers. Anywhere
but the root they are refused: a widget has no opening of its own, because it is simply there
or not. For something that shows and hides, `watch` the signal behind it.

**Every `onOpen` gets exactly one `onClose`, unless the process dies.**

| What happened | `onClose` of the old | `onOpen` of the new |
| --- | --- | --- |
| The first layout is loaded | -- | yes |
| The route changes, back and forward included | yes | yes |
| Another layout is opened in its place | yes | yes |
| A `.tsx` is saved, or a new blueprint appears: a reload | no | no |
| A screen fails to load | no: the old one stays | no |
| The application exits normally | yes, before the backend's shutdown | -- |

- **The old screen is told first.** A screen that saves on close always comes before one that
  reads on open, cross-fade or not. With a cross-fade, `onClose` runs as the screen starts to
  leave rather than when it has gone.
- **The move is already decided by then.** In `onClose`, `state.route` holds the screen you are
  going to, and on `back` so do the route's params. In `onOpen`, both are as they will stay.
- **What they write is on the first frame.** A screen is never seen before its `onOpen` has run.
- **A reload runs neither,** so that a saved edit keeps your state. It does stop the layout's
  timers, and nothing starts them again; the log says so when that happens. A timer that has to
  outlive a reload belongs to an action in state.ts.
- **A command called from `onOpen` runs while the layout loads,** so its handler has to be bound
  by then. Bind first, then load.
- **On a root `<popup>`, `onClose` is the popup's own event:** a press outside it, or Escape.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
