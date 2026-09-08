# Docking

A `<dockspace>` is an element like any other. It sits in the layout, takes the space the
layout gives it, and its children are movable panels rather than stacked widgets.

```tsx
<dockspace id="space" height="fill" persist="dock_layout.ini">
  <dock id="explorer" title="Explorer" side="left" size={190}> ... </dock>
  <dock id="editor"   title="Editor"   side="center">          ... </dock>
  <dock id="output"   title="Output"   side="bottom" size={130}> ... </dock>
</dockspace>
```

That is what makes the windowing system optional and placeable. An application that wants
none never writes one; one that wants panels in a corner puts a dock space in that corner,
and everything outside it lays out normally.

Two things had to change for it:

- `DockSpace::update` took the window's *size*, with the origin assumed to be zero — which
  is exactly why docking could only ever be the whole window. It takes a rectangle now,
  and the full-window case is the rectangle that covers everything.
- `Widget::addChild` is virtual, because a docked panel is positioned by the dock space
  rather than by the parent's layout. `DockHost` hands panels over as they arrive and
  everything that builds a tree goes on working unchanged; anything that is not a panel
  stays an ordinary child and paints underneath, which is how a dock area gets a backdrop.

## The chrome follows the theme

A dock panel's tab, its title bar when it floats, the splitters between panes and the
guidance shown while one is dragged all come from a `dock` style in the theme:

```ts
export const dock = {
  default: { tabActive: "#3A6AD0", tabHeight: 26, radius: 5, /* ... */ },
  tool:    { tab: "#1B212B", tabActive: "#46506A" },
}
```

`<dock variant="tool">` picks one, so panels in the same pane can look different from each
other. The space's own chrome — splitters, drop guidance — uses the default, since it
belongs to no single panel.

This was the one part of the interface a theme could not touch: the colours were literals
in `Docking.cpp`, `rgba(58, 92, 168)` and the rest, with a fixed tab height. Every default
in `DockStyle` is the value that was hardcoded there, so a theme that says nothing about
docking looks exactly as it did before.

Where a panel sits is decided twice, and the split is deliberate: **the layout says where a
panel starts, and `persist` says where it ended up.** Once somebody drags a panel, the
arrangement is theirs. A saved arrangement wins over the layout, and a panel it does not
mention keeps what the layout said — so adding a panel to a layout does not oblige anyone
to delete their saved file. The save happens on teardown, which includes the teardown a hot
reload does, so an arrangement survives editing the layout that declared it.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
