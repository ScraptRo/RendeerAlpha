# The common properties

Accepted by every element above.

| | |
| --- | --- |
| `id` | this node's name. The compiler turns it into a path, which is what hover and focus are keyed on |
| `visible` | drawn and interactive when true |
| `animate` | milliseconds it eases over; inherited by children |
| `route` | the shape it travels when it moves, as an SVG path — see [Motion](../motion.md) |
| `pace` | how fast it travels that shape |
| `width`, `height` | see [Sizing](../sizing.md) |
| `minWidth`, `maxWidth`, `minHeight`, `maxHeight` | bounds on the result |
| `hAlignSelf`, `vAlignSelf` | this child's own answer to its container's `hAlign` / `vAlign`, for that axis |
| `x`, `y`, `w`, `h` | see [Absolute placement](../placement.md) |
| `anchor`, `marginRight`, `marginBottom` | which parent edges it follows |
| `onHover(boolean)` | true when the pointer arrives, false when it leaves |
| `dragWindow` | dragging this moves the window — see [The window](../window.md) |

`onHover` fires on the edges only, not every frame the pointer is inside, so a handler that
writes a signal is not rewriting it sixty times a second. It answers for the rectangle the
widget was drawn at, which means a widget hidden behind a popup still reports hover —
the pointer is over it. Use it for a tooltip, a detail panel that follows the pointer, or a
row that has more to say when it is looked at; do not use it to build your own buttons,
which have `onClick` and a hovered colour in the theme already.

`id` is worth taking seriously. It is not decoration: it is what hover, focus, animation
and the headless probe are keyed on, and two siblings sharing one are one widget as far as
all four are concerned.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
