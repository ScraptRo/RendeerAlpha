# The text field

## The caret

The same `transitionMs`, on the same element, because a caret is the other thing in a text
field that moves.

**Blinking is no longer a square wave.** It keeps its timing — half a second solid, half a
second gone — but the change at each end takes the theme's duration instead of one frame,
which is the difference between a cursor and something flashing at you. Measured on the
demo, across one boundary:

```
blink 0.000 → 0.359   alpha 1.000            solid
blink 0.367 → 0.504   alpha 0.950 → 0.000    going
```

The fade is clamped to 200ms however long the theme's duration is: longer than the
half-second it is a fade *of* and the caret would never reach either end, and a cursor that
only ever pulses is harder to find than one that blinks.

**It slides between positions.** Arrow keys, Home, End, a line above or below: the caret
travels there rather than appearing there. Four presses of Up in the demo's editor, one
line each:

```
y 54.00 → 53.31 → 52.15 → 48.76 → 44.93 → 40.86 → ... → 36.00
```

Two things about *which* position is eased.

**The content's, not the screen's.** Scrolling the view moves the caret across the window
without moving it through the text; easing the screen position would have it drift out of
its line every time the view moved. What is eased is where it sits in the content, and the
scroll is subtracted after — so it stays welded to the character it is beside.

**And not while the pointer is doing it.** A click puts the caret where you pointed and a
drag carries it with you; easing either means the caret trails the hand. The same rule as a
scroll bar's thumb, and as with the thumb the value is *tracked* through the drag rather
than skipped — a value nobody asks for is dropped, and the next keystroke would then glide
from nowhere. So clicking is direct and typing is smooth, which is the right way round: you
pointed at that place, but you only asked for the next character.

A caret in motion is solid, because moving it is activity and activity resets the blink.
Nothing new was needed for that; it is what the blink already did.

## The selection under it

A highlight has one edge that moves and one that does not, and the one that moves is the
caret's. So it is drawn to the value the caret is already being eased to, rather than to
its own idea of where the caret is:

```
caret   drawnX     highlight x1
  1      0.283        0.283
  1      4.779        4.779
  1      8.535        8.535
  1      9.075        9.075     (arriving at 9.076, one character along)
```

Left to compute its own edge the block jumped to the new column while the caret glided
there, and for the length of the glide the caret sat *inside* its own selection — a defect
the previous section created, and this is the other half of it. Only along the line the
caret is actually on: part-way through a move between lines the drawn x belongs to a line
it is passing over rather than to either end. Box mode keeps its own edges, being a
rectangle rather than a caret.

**It fades in.** Select-all, over about 17 frames:

```
shown 0.0000 → 0.1363 → 0.4793 → 0.7451 → 0.9538 → 1.0000
```

**Nothing fades while the pointer is drawing it out.** A drag is the hand, and a highlight
that lagged would trail the words being swept over. A double-clicked word arrives solid for
the same reason — that is a pointer gesture too. What fades is a selection asked for from
the keyboard.

**There is no fade out.** A selection going is the one moment you need to be certain it has
gone, because the next thing typed either replaces it or does not, and a highlight lingering
over text about to be overwritten says the opposite of what is true. The value still runs
down to zero while nothing is drawn from it, so a selection made straight after another one
starts from where that one left rather than from nothing.

That last point is the reason this needed no ghost, unlike a closing dock panel. A panel on
its way out has to be remembered because its container is destroyed; a selection on its way
out would have to be remembered as a *range*, and the usual way a selection ends is by
being typed over — so the range would point at characters that are no longer there.

---

Back to [things that move](README.md) · [all documentation](../../README.md)
