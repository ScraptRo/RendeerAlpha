# Things that move

A button's fill does not jump from one colour to another; a tick grows into its box. That
is the whole of "fluid", and it is not a new kind of state — it is the same value, arrived
at over a few frames.

It is asked for in the theme, because how a thing moves is part of how it looks:

```ts
export const button: RdaButtonTheme = {
  default: { transitionMs: 140, easing: "out" },
  primary: { normal: "#3A6AD0", hovered: "#4C7CE6", transitionMs: 140 },
}
```

`transitionMs` of zero is instant, which is what everything did before this existed and
what a theme that says nothing still gets. Four curves: `out` (fast then settling — the
default, and what a thing responding should do), `in`, `inOut`, `linear`.

Nothing in a paint function had to change shape. A widget still says what it *should* look
like given what is happening to it; `Motion` answers what it looks like **now**:

```cpp
uint32_t color = s.normal;
if (mActive == wid)   color = s.pressed;
else if (mHot == wid) color = s.hovered;
color = mMotion.colour(wid ^ kMotionFill, color, s.motion.seconds, s.motion.curve);
```

## What it costs: frames, and only while moving

This engine draws on demand. An animation is the one thing that changes with no input at
all, so two places had to learn about it: the retained cache walks the tree while anything
is in motion, and the frame loop gives a window frames while its interface is still
arriving. Both stop the moment it has. Measured over a hover sweep and two three-second
idles: **194 frames drawn, 288 skipped** — the transitions cost frames, the idling costs
none.

A value that has arrived is dropped, so a Gui with nothing in motion holds nothing.

## Four things that had to be right, and were not at first

**A starting animation counts immediately.** `step()` runs at the top of a frame, before
any widget has asked for anything. A value that first hears about its new target *during*
the walk has to mark itself moving there and then — otherwise the window looks at a
settled table after the walk, skips its next frame, and the animation never gets a second
frame to move in. It arrived instantly, one frame late.

**One step cannot swallow a whole animation.** In an on-demand loop the frame time is
wall-clock since the last frame, and a window that has been idle wakes with *seconds* on
the clock. Handed to an animation, that spends the entire duration in one step. Clamped to
a thirtieth of a second: a genuinely slow frame makes animations run slightly slow, which
nobody notices, instead of skipping them, which everybody does.

**Ageing only happens on a frame that walked the tree.** This is the one that looked most
like the feature simply not working. A retained GUI walks on change and reuses its geometry
otherwise, so on a reused frame nobody asks for anything — and a value evicted for going
unasked forgot the colour of a button sitting perfectly visible on screen. The pointer
then arrived to find no history to animate from, and every transition started already
finished. `step()` advances the clock every frame; `forget()` ages the table, and only
where a walk is about to happen.

**An interrupted colour is not a number.** Reported as buttons flashing a random colour
for a frame or two when the pointer was moved quickly across them. A transition that is
interrupted leaves from where it had got to, and that position was worked out the way it is
for a number: `from + (to - from) * t`. For a colour it is worth being precise about why
that fails, because it very nearly works. A packed colour is a base-256 number, so
interpolating the whole word *does* give every channel its correct value — but as a
*fraction*, and the fractional part of one channel is worth up to 255 of the channel below
it. Truncating that back to a word pours green into red.

Only interrupted fades were affected, which is why it needed a fast pointer: a fade allowed
to finish leaves from an exact colour. Swept across the demo's buttons, 52 of 79 frames
that were mid-fade drew a colour from outside the pair being mixed — a dark slate button
drawing `(243, 160, 77)`, which is orange.

So "where is it now" is asked per kind: numbers interpolate, colours mix per channel, and
the value's own record of which it holds decides. It also means the invariant the mixing
code claims — that the stored word is only ever an exact colour, never a blend of two — is
now actually true.

All four are covered by cases in `ctest`, because all four are arithmetic over a clock and
none of them needs a device to be wrong.

---

The rest of this folder, one page per thing that moves:

- [Layout that slides](layout.md)
- [Scrolling that glides, and the bar itself](scrolling.md)
- [The caret, and the selection under it](text-field.md)
- [Fading, tabs, and screens that cross-fade](transitions.md)
- [Docking: chrome, panels, closing and collapsing](docking.md)

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
