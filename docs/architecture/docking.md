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

## Two arrangements, one set of elements

`arrange` chooses how the space is divided, and the two answers are different enough to
be different code:

- **`panes`** is a tree of splits (`DockTree.h`). Every pane is somebody's half, the area
  is always fully covered, panels sharing a pane are tabs, and a drop has four meanings
  depending on which part of a pane it lands on.
- **`tiles`** is a column grid (`TileGrid.h`). A panel is a rectangle of whole cells with
  room around it, nothing has to be anybody's half, and dragging one pushes what it lands
  on downward rather than re-cutting anything.

They are not one mode with a flag because they disagree about what a panel *is*. In panes
a panel is a share of its neighbour, which is where tabs and splitters come from; in tiles
it is a rectangle, so neither has anything to mean. A single arrangement would have to
answer both and would be a worse version of each.

What they do share is everything above that: the same `<dockspace>` and `<dock>`, the
same containers, the same theme, the same close buttons, the same `persist` file. A saved
file carries both blocks, so a space switched from one to the other finds what the reader
had done on each side still there.

The grid's own rules are two, and the rest follows from them. **Tiles are pulled up into
free space and never sideways** -- a tile that slid left to fill a hole would move because
of something that happened elsewhere on the screen, and nobody can follow that. And
**whatever is under the pointer keeps its row** while everything else settles around it;
pulling it up would take it out from under the hand holding it. Those two are what
`TileGrid::compact` and its `settling` argument are.

A tile follows the pointer in pixels while the grid follows it in whole cells behind,
which is the whole feel of the arrangement: what you carry is smooth, what it does to the
others snaps, and a translucent rectangle shows where it will land.

## A panel can wear its own bar

Every colour of a panel's bar is themeable and none of its *shape* was: a filled strip,
the title as text, an `x` at a fixed offset, all literal in `Docking.cpp`. A `<header>`
inside a `<dock>` replaces it with a widget tree, and the engine keeps only the part that
cannot be handed over — the hit test that picks the panel up and carries it somewhere
else.

Which press that is, is the whole of the design. A button on the bar has to win over the
bar, and the GUI already answers that: hot is last-writer-wins, and the header's contents
run after the band declares itself. So the band asks *afterwards* — it reads which widget
the pointer went down on before painting its contents and again after, and if that has
not changed, nothing inside claimed it. Asking first would hand every press to the bar
and a close button would drag the panel instead of closing it.

It is drawn wherever the panel is the only one in its pane, which includes floating and
includes every tile. Sharing a pane, the strip wins and the header is not drawn: a strip
belongs to the pane and a header belongs to a panel, and two panels in one group cannot
both dress it. That is the one rule the feature needed, and everything else follows from
it — including that a dressed panel dragged into a tab group becomes an ordinary tab, and
gets its bar back when it is pulled out again.

The close button that came with it is `visible`, which now means on a dock panel what it
means everywhere else: the room goes with it. `DockLayout::layout` takes an optional
predicate saying which ids are showing, and a pane holding nothing that shows is given no
room at all — its sibling takes the whole split and no splitter is kept between them.
That is reversible in a way the engine's `x` is not: the pane is still in the tree at zero
size, so showing the panel again puts it back exactly where it was rather than wherever a
new one would land.

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
