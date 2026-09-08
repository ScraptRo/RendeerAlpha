# Docking that moves

## The dock chrome moves

`transitionMs` and `easing` on `dock`, the same two fields the other elements take:

```ts
export const dock: RdaDockTheme = {
  default: { /* colours */ transitionMs: 160, easing: "out" },
}
```

Four things follow from it. Tabs fade between active and inactive, and a close button
between its two colours — keyed on the panel rather than on its position in the strip, so
reordering tabs does not make two of them trade colours on the way past each other. A
splitter fades under the pointer, keyed on where its handle is, which is its identity for
as long as it needs one. The drop guidance fades in with a drag and out again after the
drop, instead of blinking on the frame a panel is picked up.

And a pane glides to wherever it has just been docked.

## A panel and its pane are one position

A pane's rect is keyed on the panel it holds, and a **floating** panel writes that same
value while it is being carried. So the two are one continuous position: let go of a panel
over the right-hand edge and the pane grows out of where your hand left it, rather than the
panel vanishing in one place and a pane appearing in another.

**Nothing eases while something is being dragged.** A splitter drag recomputes these rects
from the pointer every frame and a carried panel follows the hand; easing either means the
thing you are holding trails you. Same rule as a scroll bar's thumb.

But suppressing an ease is not the same as not asking. The first version simply skipped the
animation while dragging, and the pane still snapped into place on the drop — because a
value nobody asks for is dropped, so the entry was gone by the time the drag ended and was
recreated at the destination, already there. While a drag is in progress the value is
*tracked* rather than eased: written exactly, every frame, so that when the drag ends the
glide starts from where the hand left it.

That is the third time the same shape of bug has appeared — a hover that would not fade
because the retained cache had forgotten the colour, a collapse that would not grow back
because a hidden widget had been evicted, and now this. The rule underneath all three: an
animated value has to be asked for on every frame it might matter, including the frames
where the answer is not being used.

## A panel on its own

A floating panel was the last part of the dock that still only ever appeared, jumped and
vanished. Three things move now, and the interesting one is the second.

**It arrives.** A panel that has never been drawn — spawned, restored from a saved
arrangement, or shown again after being hidden — fades in and grows the last 6% into place.
It is the closing ghost's shrink played the other way, and unlike the ghost it is the real
panel: the frame, the title and everything inside it fade together, because opacity is
applied where all GUI geometry passes through and covers a whole subtree at once.

**Its size glides, its position does not.** Pull a tab out of a pane and the panel that
comes away is the pane's shape, shrinking to a window's while you carry it:

```
w  168.6 → 208.9 → 235.0 → 254.0 → 260      (the pane's width, to a floating one)
h  732   → 505.7 → 359.7 → 253.4 → 220
x  1080  → 1049  → 1018  →  ...  → 610      (the pointer, exactly, every frame)
```

That is the drop's glide run backwards, and it falls out of the same shared value: the
entry keyed on this panel already held the pane's rect, because the pane had been writing
it every frame it was drawn. Nothing had to be handed over.

**The split is what makes it feel right.** *Where* it is belongs to the hand and is tracked
exactly — a carried panel that eased would lag the pointer like rubber. *How big* it is
does not: nobody is holding the size, so it can take its time. The one exception is the
resize grip, where the hand is holding the size itself, and there both are tracked.

**Its chrome fades like a pane's.** The title bar and the close button now use the same two
animated values a docked tab and its close button use — the same keys, so a panel dropped
into a pane does not also change colour in the moment it changes shape.

### Arriving and being pulled out are the same panel

A panel that has just been detached must not fade in. It was on the screen a frame ago, and
fading it in would fade out the thing under the pointer. Nothing about the panel's *state*
distinguishes the two cases — both are "floating, not drawn last frame" — so the container
carries a `fresh` flag: set until it has been drawn once, cleared by the detach, and set
again whenever it goes invisible so that being shown again is an arrival too.

### Input follows what was drawn

While a panel is arriving or still shrinking out of a pane, where it *is* and where it was
asked to be are two different rectangles, and grabbing one has to agree with what is on the
screen. So a floating panel records what it drew and the next frame's hit-testing uses
that, the same field a docked panel has always filled in. It is also what the closing ghost
starts from, so a panel closed mid-arrival fades from where it actually was.

## A panel closing

Closing one used to remove it at the end of the frame, and the panes beside it took the
space at once. Now it fades and shrinks a little toward its own middle while they glide in
underneath.

It works the way the router's screens do — keep it alive while it goes — but with a
difference worth stating, because it is what made this small instead of large: **the panel
leaves the tree immediately.** Only a picture of it stays. Closing destroys the container,
so nothing survives to be drawn; what is kept is what it looked like — where it was, what
it was called, and which variant drew it — and the ghost drawn from that is chrome only,
which is what a panel going away looks like anyway.

The alternative would have been to keep it docked while it shrinks, the way a `<stack>`
collapses a child. A stack can do that because a child's share of a stack is a number. A
dock pane's share is a position in a tree of splits, and "a fraction of a split" is not
something the tree can express. Leaving at once and fading over the top gets both halves of
the effect — the panel going, and the neighbours arriving — without teaching the tree to
hold something that is half there.

It is drawn last, so it fades over the panes already moving into the space it left.

## Collapsing

A widget turning invisible inside a stack now shrinks out of the layout instead of
vanishing from it. Nothing new is written in a layout: it is `visible` and `animate`, both
already there.

```tsx
<stack id="root" arrange="vertical" animate={220}>
  <textfield id="notes" visible={() => state.expanded} height="fill" />
```

Measured on the demo, with the duration stretched so it could be photographed — the field's
own bottom edge, and the top of the button below it:

```
notes bottom   785 → 737 → 633 → 555
button below   597 → 546 → 443 → 365 → 298
```

298 is exactly where the button lands when the field is hidden outright, so it collapses
all the way and finishes in the layout it should.

## Why this one is allowed to change what is measured

Everything else in the animation layer is careful never to touch measurement: what a widget
measures as is what the layout computed, and only what is *drawn* lags. A collapse cannot
work that way — the space itself has to shrink, or nothing below it moves.

What made it safe was noticing which loop the rule was actually guarding against. The
danger is measure → place → smooth → **measure**: a value derived from a measurement being
fed back into measuring. A collapse factor is not that. It comes from `visible` and a
clock, and nothing measured feeds it, so a stack sized from it is sized from a number that
would have been the same whatever the stack turned out to be. Measurement reads it;
nothing writes it from measurement. No loop.

So `Widget::presence()` is the one animated value measurement is allowed to read: 1 while a
widget is there, 0 once it has collapsed out, and the fraction in between. It scales the
room the child asks for, the gap after it, and the share of the remainder it claims if it
was filling — so what it was holding is handed back over the same time it takes to go.

Two details that are only obvious once they are wrong:

- **Its gap collapses with it.** A child that kept its full spacing while shrinking would
  leave a hole and then close it in one frame at the end.
- **It is asked for every frame, gone or not.** Values nobody asks for are dropped, and a
  hidden widget that was dropped would forget it was hidden — the next time it was shown it
  would appear at full size rather than growing into place.

While it is collapsing it is clipped to the room it has left and faded by the same factor,
so a half-height text field reads as leaving rather than as broken. It is also not
*slid* while collapsing: its size is already being animated, and easing toward an easing
value would only arrive late.

Only a `<stack>` collapses its children. A scroll view and a list place theirs from a
scroll offset, where "taking up less room" has no meaning that would help.

---

Back to [things that move](README.md) · [all documentation](../../README.md)
