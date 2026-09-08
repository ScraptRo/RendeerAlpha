# Layout that slides

`animate` is a number of milliseconds, on any element, inherited by everything inside it:

```tsx
<stack id="root" arrange="vertical" spacing={8} animate={220}>
```

Say it once on the container whose children rearrange, and hiding something makes what is
below it slide up rather than jump. Measured on the demo, four frames after the click, the
top edge of the button below the notes: **597 → 458 → 311 → 298**.

**Only the drawn rect lags.** This is the rule the whole feature stands on. `placed` is
what the layout computed; it is what the cursor advances by, what the next sibling is
positioned from, and what everything measures against. What eases is a separate value that
is only ever drawn with. If the smoothed rect fed back into measurement, a stack would size
itself to a transient number and the layout would wobble its way to settling instead of
arriving.

**It eases in the parent's frame, not in absolute coordinates.** If the parent is itself
sliding, the child's absolute position already carries that; easing it again would ease the
same lag twice, and a child inside a moving panel would trail it rubberily. Each level
smooths only its own change.

**And it belongs to the container, not the child.** The first attempt put it in
`Widget::placement()`, which looked like the obvious single seam — every computed rect
becomes a drawn one there. It animated nothing at all, because a child is painted with its
own position as its origin: by the time it reads its rect there is no parent frame left to
be relative to, and the offset being eased was always zero. The container is the thing that
moves its children, so the container is where the easing goes.

**Two places deliberately do not ease their children.** A scroll view moves its child by
the scroll offset, and a list puts its pooled rows in new places every frame — easing where
those land would not be a slide, it would be a smear. What they ease instead is the offset
itself, which is the next section. A list is also a barrier for inheritance: children stop
looking for a duration there, so `animate` on an outer container does not reach the rows.
The list itself still slides, because the barrier is about seeing *past* a widget rather
than about the widget itself.

---

Back to [things that move](README.md) · [all documentation](../../README.md)
