# Docking

## `<dockspace>`
An area whose children are movable, dockable panels.

| | |
| --- | --- |
| `arrange` | `"panes"` (default) or `"tiles"` — see below |
| `persist` | a file to remember the arrangement in |
| `columns` | tiles: how many columns the grid has (default 12) |
| `rowHeight` | tiles: how tall one row is, in pixels (default 60) |
| `gap` | tiles: pixels between tiles, and around them (default 8) |

Without `persist`, panels open where the layout says every time.

## Two arrangements

`arrange` picks how the space is divided up. It is the same `<dockspace>` and the same
`<dock>` either way — what changes is what a panel *is*.

### `"panes"` — the editor

The area is cut into nested splits. Every pane is somebody's half, so the whole area is
always covered, panels sharing a pane become tabs, and dragging the splitter between two
of them moves the boundary. Dropping a panel on a pane's edge splits it; dropping it on
the middle joins that pane's tab group; dropping it on the window's edge takes a strip
across the whole side.

This is what a code editor looks like — an explorer beside a viewport above a console —
and it is the default.

**The corner of a pane splits and joins it.** Every pane has a small grip in its
bottom-right, and dragging it is the gesture Blender uses for areas:

- **inward, over its own pane** → the pane cuts in two, and the panel showing moves into
  the new half. Drag mostly sideways for a vertical boundary, mostly up or down for a
  horizontal one; the boundary follows the pointer.
- **outward, over a neighbouring pane** → the two become one. Everything in this pane
  moves into that one as further tabs.

A split *moves* the active panel rather than copying it, which is where this parts
company with Blender: there an area splits into two views of the same editor, and here a
panel is a thing with an identity that can only be in one place. So a pane holding one
panel has nothing to split off, and its corner only joins. The grip is not drawn at all
when there is neither anything to split nor anyone to join.

### `"tiles"` — the dashboard

Each panel is a rectangle of whole cells in a column grid, with room between them.
Nothing has to be anybody's half and a row may be half empty. Dragging a tile pushes
whatever it lands on downward rather than re-cutting anything; letting go, everything
rises into whatever space is above it. The bottom-right corner resizes, in cells.

```tsx
<dockspace id="board" arrange="tiles" columns={12} rowHeight={54} gap={10}>
  <dock id="sales"  title="Sales"  col={0} row={0} cols={6} rows={3}> … </dock>
  <dock id="errors" title="Errors" col={6} row={0} cols={6} rows={3}> … </dock>
  <dock id="queue"  title="Queue"           cols={4} rows={3}> … </dock>
</dockspace>
```

Positions are in **cells, not pixels**, so an arrangement is the same arrangement at any
window size: a tile three columns wide is a quarter of the width whether the window is
900 pixels across or 2400. Only `rowHeight` is a pixel measurement, because rows do not
divide anything — they stack.

Leave `col` and `row` off and the tile takes the first place it fits, reading left to
right and then down. That is usually what you want: state the sizes and let the grid do
the placing, and a panel added later lands in the first hole rather than at the bottom.

Two rules are worth knowing because everything else follows from them:

- **Tiles are only ever pulled up, never sideways.** A tile that slid left to fill a hole
  would move because of something that happened elsewhere on the screen, and nobody can
  follow that. An empty half-row stays empty until something is placed in it.
- **What you are holding keeps its row.** While a tile is under the pointer it is the one
  thing that does not settle; everything else closes up around it. It joins them when you
  let go.

There are no tabs and no splitters in this arrangement — a tile is a rectangle, not a
share of its neighbour, so neither has anything to mean. `side` and `size` are ignored,
and `col` / `row` / `cols` / `rows` are ignored in `"panes"`.

Rows keep going past the bottom of the space, and what is below it is clipped. Compaction
pulls everything up, so this only happens when there is genuinely more than fits.

### What a drag shows you

While a panel is being carried, the space draws where it would land:

- **the four window edges**, banded, so docking across a whole side is discoverable at
  all. The one under the pointer lights up.
- **the pane under the pointer**, tinted, which is the pane the drop is about.
- **the panel itself**, as a figure: the rectangle it would take, outlined, wearing a bar
  with its name on it.

That last one changes shape with what the drop would do. Over a pane's edge it is the
half the panel would take. In a pane's middle -- where the drop joins that pane's tab
group -- it is the **tab it would become**, drawn at the end of that pane's strip, because
lighting up a whole pane there says nothing the tint has not already said.

The guidance fades in with the drag and out again after the drop, rather than blinking on
the frame a panel is picked up.

## `<dock>`
One dockable panel; its children are what it holds.

| | |
| --- | --- |
| `title` | what its tab and title bar say |
| `closable` | give it a close button |
| `variant` | which dock style |
| `side` | panes: where it starts — `floating` / `left` / `right` / `top` / `bottom` / `center` |
| `size` | panes: how wide or tall its pane starts |
| `col`, `row` | tiles: which cell it starts in; unset means wherever it fits |
| `cols`, `rows` | tiles: how many cells wide and tall it starts (default 3 × 3) |

All of these are where a panel *starts*. Once it has been placed — by the layout, or by a
restored `persist` file — where it sits belongs to whoever dragged it, and the layout does
not pull it back.

## `<header>` — a bar of your own

A `<dock>`'s bar is the engine's: a filled strip, the title as text, an `x` at the far
right. Every colour of that is themeable and none of its *shape* is. Put a `<header>`
inside the dock and the bar becomes a widget tree instead.

```tsx
<dock id="explorer" title="Explorer" side="left" size={230}
      visible={() => state.explorerOpen}>
  <header id="bar" height={30}>
    <stack id="row" arrange="horizontal" padding={6} spacing={8} vAlign="center">
      <image id="ic" src="res/icons/folder.svg" width={16} height={16} />
      <label id="t" width="fill" height="content" text="Explorer" />
      <button id="x" text="x" width={22} height={18}
              onClick={() => state.explorerOpen = false} />
    </stack>
  </header>

  <list id="files" … />
</dock>
```

Anything can go in it — an icon, a second line, a search field, three buttons. It is a
widget tree, so bindings, variants, hover and motion all work in it the way they work
anywhere else. `height` is the one thing it takes of its own, and without one it is
exactly as tall as the bar it replaced.

The engine keeps one thing: **dragging the bar still picks the panel up** and carries it
to another pane. A button on the bar takes its own press first — the band declares itself
before its contents draw, and whatever is under the pointer last is what gets it — so a
close button closes and does not drag.

**It is drawn wherever the panel is the only one in its pane.** That includes a floating
panel and it includes every tile. Sharing a pane with others, the tab strip is what the
pane wears and the header is not drawn at all: a strip belongs to the pane and a header
belongs to a panel, and two panels in one group cannot both dress it. Drag a dressed
panel into a tab group and it becomes an ordinary tab; drag it out again and its header
comes back.

`closable` is ignored on a panel with a header — you drew your own buttons, so the engine
does not add one over the top of them.

### Closing one

`visible` is how, and it means on a dock panel exactly what it means everywhere else: the
room it takes goes with it. Hiding the last panel in a pane closes that pane up — its
sibling takes the whole split and the splitter between them goes — and showing it again
puts it back where it was, at the size it was.

That is different from the engine's `x`, which *removes* the panel: gone from the tree,
and a new one would land wherever a new one lands. A close button you drew yourself
should almost always write a signal.

## What a theme can change

Both arrangements draw from the same `dock` theme: `pane`, `tabStrip`, `tab`, `tabActive`,
`tabText`, `titleBar`, `titleBarActive`, `close`, `closeHover`, `grip`, `splitter`,
`splitterHover`, the four drop colours, and `tabHeight`, `tabPadding`, `closeWidth`,
`radius` and `motion`. A `<dock>` names a `variant` to be drawn by a different one, so a
panel can look unlike its neighbours; the space's own chrome — splitters, drop guidance —
uses the default, since it belongs to no panel. See [themes](../themes.md).

In tiles, `dropPreview` is the colour of the rectangle showing where a carried tile will
land, and `splitter` is unused. A panel wearing a `<header>` uses none of the tab or title
colours, because it draws none of those things — only `pane` is still the engine's, as the
ground the header and the contents sit on.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
