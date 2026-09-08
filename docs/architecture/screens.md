# Screens

A route is a screen. Declared beside state, because that is what it is:

```ts
export const routes = {
  home:      { layout: "hello",     doc: "the counter" },
  catalogue: { layout: "catalogue", params: ["productId"] },
}
```

Declaring them adds two things: a `route` signal holding the name of the screen showing,
and the commands `back` and `forward`. Navigating is then an ordinary write:

```tsx
<button text="Catalogue" onClick={() => state.route = "catalogue"} />
<button text="<" onClick={() => commands.back()} />
```

That is the whole feature, and the reason it is that small is that **which screen is
showing is state** — and this engine already had one answer for state. No new grammar, no
new concept, and reactive for free: a label bound to `state.route` follows the screen
without being told navigation exists.

`route` is typed as the union of the names declared, so a misspelling is an error where it
is written:

```
nav.tsx(23,30): error TS2820: Type '"cataloge"' is not assignable to type
'"catalogue" | "docking" | "home" | "scrolling"'. Did you mean '"catalogue"'?
```

## Params are signals

A screen's arguments are ordinary signals, named by the route rather than passed to it:

```tsx
onClick={() => { state.productId = item.id; state.route = "product" }}
```

Both writes are one navigation, because the router looks once a frame rather than once a
write. Naming them in the declaration is what makes **going back correct**: history records
the route and the value of every param that route declared, and restores both. A param has
to be a declared signal, checked when the declaration is compiled — restoring one means
writing it, and writing a name nothing declared writes nothing.

This is the same trade commands make, for the same reason: `commands.save(state.count)` is
refused because state is already the channel for data, and a second one would be two
answers to one question.

## Where the swap happens

The tree is thrown away and the new screen is built. That is only correct because signals
live outside the tree being replaced — the same property that makes hot reload here
simpler than Fast Refresh — so a screen can be left and returned to without anything being
preserved by hand.

Both the route signal and `back`/`forward` take effect in `Router::update()`, which an
application calls once a frame, never in a widget callback. `commands.back()` fires while
the tree that button lives in is being walked, and swapping it there would destroy the
walk from under itself; deferring is what lets a back button be an ordinary button.

Rebuilding also has to mark the interface dirty, not merely ask for a redraw:
`rendeerInterfaceChanged()`. The retained cache decides whether to walk the tree by
looking at input, and replacing the tree is not input — so presenting again presents the
geometry the old screen left behind. Hot reload had the same latent bug and now takes the
same path.

## Entry, and from C++

`Router::open` shows whatever the `route` signal already holds, so choosing the first
screen is just setting it first — an application that takes a screen name on its command line sets it before `open`.
`Router::navigate("home")` from C++ writes the same signal, so C++ navigating and the
interface navigating are one event rather than two paths that can disagree.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
