# The common properties

Accepted by every element above.

| | |
| --- | --- |
| `id` | this node's name. The compiler turns it into a path, which is what hover and focus are keyed on |
| `visible` | drawn and interactive when true |
| `animate` | milliseconds it eases over; inherited by children |
| `width`, `height` | see [Sizing](../sizing.md) |
| `minWidth`, `maxWidth`, `minHeight`, `maxHeight` | bounds on the result |
| `hAlignSelf`, `vAlignSelf` | this child's own answer to its container's `hAlign` / `vAlign`, for that axis |
| `x`, `y`, `w`, `h` | see [Absolute placement](../placement.md) |
| `anchor`, `marginRight`, `marginBottom` | which parent edges it follows |

`id` is worth taking seriously. It is not decoration: it is what hover, focus, animation
and the headless probe are keyed on, and two siblings sharing one are one widget as far as
all four are concerned.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
