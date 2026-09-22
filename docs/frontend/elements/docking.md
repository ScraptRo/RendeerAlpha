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

## What a theme can change

Both arrangements draw from the same `dock` theme: `pane`, `tabStrip`, `tab`, `tabActive`,
`tabText`, `titleBar`, `titleBarActive`, `close`, `closeHover`, `grip`, `splitter`,
`splitterHover`, the four drop colours, and `tabHeight`, `tabPadding`, `closeWidth`,
`radius` and `motion`. A `<dock>` names a `variant` to be drawn by a different one, so a
panel can look unlike its neighbours; the space's own chrome — splitters, drop guidance —
uses the default, since it belongs to no panel. See [themes](../themes.md).

In tiles, `dropPreview` is the colour of the rectangle showing where a carried tile will
land, and `splitter` is unused.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
