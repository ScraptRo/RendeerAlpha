# Scrolling that glides, and the bar itself

## Scrolling that glides

The same `animate`, on the thing that scrolls:

```tsx
<scroll id="list" height="fill" vertical={true} animate={110} />
<list id="catalogue" of="products" rowHeight={26} height="fill" animate={110} />
```

A wheel notch used to be a jump of `wheelStep` pixels. Now the notch moves `offset` the
whole way at once — everything that reasons about scrolling still works on the value that
actually moved, including clamping, `atBottom()`, `scrollToBottom()` and the drag
arithmetic — and what is *drawn* glides to it. The same division the layout makes.

A list works from the drawn offset for both halves of what it does: which rows to bind and
where to put them. Choosing rows from one offset and placing them at another would show the
right rows in the wrong places, which is worse than either.

**A drag is not eased.** While a thumb is held, the offset is being computed from where the
pointer is, and easing toward the pointer means the content trails the hand holding it. A
drag is direct manipulation; a wheel notch is a request. The scroll bar's thumb follows the
drawn offset either way, so the bar and the content never disagree about where the view is.

## The bar itself

Where it *is* along the track came free with the offset above. The bar's other three
states did not, and they are the ones that made it read as a different piece of software
from the rest of the interface: it appeared, it snapped, and its handle jumped.

`transitionMs` and `easing`, on `textfield`:

```ts
export const textfield: RdaTextfieldTheme = {
  default: { transitionMs: 140, easing: "out" },
}
```

That element, because a scroll bar's three colours already live there — `scrollTrack`,
`scrollThumb`, `scrollThumbHover` — and are what *every* scroll bar is drawn with, not just
a text field's. A `<scroll>` and a `<list>` read the same three, so their timing belongs in
the same place rather than in a fourth.

Three things follow from it.

**The handle fades under the pointer**, like every other thing that reacts to one. Measured
on the demo, moving onto the handle and off again:

```
(70, 78, 96) → ... 19 frames ... → (120, 130, 160) → ... 18 frames ... → (70, 78, 96)
```

**The bar fades in and out with the need for it.** Content that grows past its view used to
make a bar appear between one frame and the next, and content that shrank to fit made one
vanish. Typing 45 lines into the demo's editor:

```
needed=0  presence 0.000     the bar is not there
needed=1  presence 0.000 → 1.000 over about 20 frames
```

A bar nobody needs is one whose handle fills it, so what fades out is a full track rather
than a handle collapsing to nothing — the difference between "all of it is showing" and
something going wrong.

**The handle's length glides.** Its length is the view as a fraction of the content, so it
changes whenever the content does. From the same run, the handle trailing its target
through 45 keystrokes and arriving exactly:

```
drawn  559.07 → 522.45 → 461.16 → 405.88 → ... → 354.37
wanted 559.07 → 510.71 → 456.95 → 394.64 → ... → 354.37
```

**Only what is drawn is animated.** Whether a bar is needed, whether its handle is under
the pointer, and the handle's true length are all worked out from geometry nothing has
eased, and they stay the values input is tested against — the same division `offset` and
`drawnOffset` already make. Grabbing a handle mid-glide grabs it where the layout put it.

There are four scroll bars in this engine — a scroll view's, a two-axis scroll's, a list's
and a text field's — which had four copies of the same two lines between them. What they
share now is `Gui::scrollBar()`: it answers those three questions and draws nothing, so
each of the four keeps its own geometry, its own inset, and in the text field's case its
own rounded handle.

Each of them asks on every frame there could be a bar, needed or not. A value nobody asks
for is dropped, and a bar that had been dropped would appear at full strength instead of
fading in — which is the same rule, in a fifth place.

---

Back to [things that move](README.md) · [all documentation](../../README.md)
