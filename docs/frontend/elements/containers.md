# Containers

## `<container>`
Groups children and draws nothing. Placement is absolute; use it when you want the
grouping without the background.

## `<panel>`
A framed background that clips its children. Placement inside it is absolute — put an
`anchor="fill"` stack in it to lay things out.

| | |
| --- | --- |
| `variant` | which panel style from the theme |

## `<stack>`
The workhorse. Lays children in a row or a column.

| | |
| --- | --- |
| `arrange` | `"vertical"` (default) or `"horizontal"` |
| `spacing` | gap between children |
| `padding` | inset around the whole row or column |
| `hAlign` | horizontally: `stretch` (default) / `start` / `center` / `end` |
| `vAlign` | vertically: the same four |
| `spread` | spreads the children along the stacking axis: `spaceBetween` / `spaceAround` |

Nothing in a stack carries a hand-computed position, which is what makes "insert a cell
between two others" a one-line change: inserting shifts the rest automatically.

## `<scroll>`

A clipped window onto content bigger than itself. It stacks its children in a column, the
way a `<stack arrange="vertical">` does, and scrolls to reach what does not fit. One child
is the ordinary case of that, and is how a single thing too wide for its place is shown.

| | |
| --- | --- |
| `vertical` | scrolls down when its content is taller. **On by default** |
| `horizontal` | scrolls sideways when its content is wider |
| `spacing` | gap between stacked children |
| `padding` | inset around the content |
| `hAlign` | where children sit across the column |
| `barWidth`, `wheelStep` | the bars, and pixels per wheel notch |
| `variant` | a **textfield** variant — that is where the scroll colours live |

Children scrolled out of sight are skipped rather than painted, so a long column costs
what is on screen. The wheel scrolls down; held with shift, or in a view that only
scrolls sideways, it scrolls across.

For rows from a table use [`<list>`](list.md) instead: it keeps about as many widgets as
fit and re-binds them, so ten thousand rows cost thirty widgets rather than ten thousand.

## `<splitter>`
A draggable bar that resizes what is above or beside it.

| | |
| --- | --- |
| `arrange` | which way it splits |
| `value` | the size it controls |
| `min`, `max` | how far it drags |

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
