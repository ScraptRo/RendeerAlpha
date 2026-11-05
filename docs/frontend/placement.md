# Absolute placement

A widget that is not inside a layout container places itself. `<panel>` and `<container>`
do not arrange their children, so this is how you fill one.

| | |
| --- | --- |
| `x`, `y` | the top-left, relative to the parent's content origin |
| `width`, `height` | the size, as anywhere else -- see [sizing](sizing.md) |
| `w`, `h` | the size, as a plain number; these win over `width` and `height` |
| `anchor` | which parent edges it follows |
| `marginRight`, `marginBottom` | the distance kept from those edges |

`anchor` takes `"fill"`, `"stretchX"`, `"stretchY"`, `"bottomLeft"` or `"bottomRight"`.
Anchored to one edge a widget keeps a fixed distance from it; anchored to both edges of an
axis it stretches to follow them.

```tsx
<panel id="frame">
  <stack id="body" anchor="fill" arrange="vertical" padding={10}> … </stack>
</panel>
```

That is the common case by a long way: a panel with one `anchor="fill"` stack inside it,
and the stack doing the layout. The absolute properties are there for the times a layout
container is the wrong tool, not as the normal way to build a screen.

A widget that names no size on an axis and is not anchored on both edges of it takes the
room the parent has left, rather than coming out zero-sized and simply not appearing. So a
button dropped into a panel with nothing said fills the panel, and with a size it is that
size, at the panel's top-left:

```tsx
<panel width="fill" height="fill">
  <button width={() => state.rda.width / 2} height={30} text="Hello" />
</panel>
```

Every size means what it means in a stack, with one difference: nothing is shared, so `"fill"`
and `"fill:2"` are both all of the room past the widget's `x`, and `"fill-40"` is that less 40.
Anchored on both edges of an axis, the anchors decide and `width` has no say on that axis.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
