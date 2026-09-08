# Fading and cross-fading

## Fading a whole tree

`Gui::pushOpacity(alpha)` / `popOpacity()` make everything drawn until the pop that much
more transparent, nesting by multiplying — a half-faded panel inside a half-faded page is
a quarter there.

It works by scaling the alpha of the vertex colours in `addQuad`, which is the one thing
every piece of GUI geometry goes through: a solid fill, a glyph and a picture are all a
quad with a colour on it. So one multiply fades an entire widget tree without any widget
knowing it is being faded, and nothing had to grow an `opacity` field.

## Tabs that cross-fade

`animate` on a `<tabs>` is how long a page takes to give way to the next:

```tsx
<tabs id="pages" value={() => state.tab} animate={200}>
```

The outgoing page fades out and slides a little the way it came from; the incoming fades
in and slides to meet it. **Nothing is kept alive specially to do this.** Both pages
already exist the whole time — a Widget persists, which is the same property that lets a
tab keep what was typed into it — so a transition needs only the index of what is leaving
and the outgoing page painted for as long as it takes.

## Screens that cross-fade

```cpp
router.setTransitionMs(200.0f);   // zero, and a swap is instant, as it was
```

This one needed a change to how a swap works. A route swap used to destroy the outgoing
tree and build the incoming one in its place, which left no moment where both existed —
and a cross-fade is exactly a moment where both exist.

So each screen now gets a container of its own to live under, and the outgoing one is
kept, painted and faded rather than taken down on the spot. Inserting a container changes
no widget's id: the compiler baked full paths into the blueprint, so `root/nav/home` is
`root/nav/home` however deep the tree it hangs from. The incoming tree is built **before**
the outgoing one is let go of, which is the same rule reload follows — a screen that will
not load must not take the running one down with it.

The screen still fading out is taken away in `update()`, not in paint: removing a widget
from the tree that is being walked is exactly what is not allowed.

Both trees are live for the length of a transition, which is the honest cost and why this
is a number rather than always on — a screen holding ten thousand rows is one worth taking
down promptly. Their bindings both run, harmlessly, on the signals they share.

**A duration belongs to the screen, not to the swap that made it.** A fader used to be
given the duration of the transition it was *born* in, so a screen created without a
cross-fade kept a duration of zero — and when it was later asked to leave, it had nothing
to leave over and vanished on the spot. A departure now takes its duration at the moment
it is asked to leave, which is also the only way `setTransitionMs()` can be called after
the first screen is open and mean anything. What is fixed at birth is only whether it
fades *in*, because there is nothing to fade in from on launch and a splash screen is not
what anybody asked for.

## Four things a transition got wrong

All four show up the same way — navigate quickly, or navigate while something is still
moving — and all four were found by logging what each screen's fader did, frame by frame,
with the duration stretched so the shape was legible.

**A screen leaves from where it had got to.** Told to leave while it was still arriving, a
screen restarted its transition from the beginning — from *solid* — so navigating twice
quickly made the screen in the middle flash to full and only then begin to go. Measured,
over a stretched transition:

```
                        arrived to      then left from
before   #screen2          0.0924            1.0000
after    #screen2          0.1170            0.1256
```

It is the same rule the animation layer already had for a colour and for a dock pane, in a
third place: an interrupted transition continues from where it is, and going somewhere
else is not the same as starting again.

**A screen already leaving is not thrown away by the next navigation.** Only one outgoing
screen was kept, so a second navigation removed the first one on the spot, mid-fade. There
is now a list of screens on their way out, oldest first, which is also the order they are
painted in. It empties itself and is bounded by how many navigations fit inside one
transition — three fast clicks now leave four screens on the screen at once, each finishing
its own fade.

**A screen on its way out takes no input.** Both trees are under the pointer during a
transition and both are walked, so a click aimed at the arriving screen could land on the
one being left. Driven at the demo: a press on a button belonging to `home`, landing
*after* the route had already changed to `widgets`. A leaving screen is now painted inside
`gui.pushInert()`, which is a subtree that hover, presses, the wheel and typing all stop
at.

That is done by taking the input away rather than by giving every widget a flag to
consult: a pointer that is nowhere fails every `contains` there is, and no button and
nothing typed is a widget with nothing to react to — so a widget written the ordinary way
is already inert inside one, including widgets written since and knowing nothing about it.
The clock is left alone, because something that takes no input still moves.

**Both screens slide the same way.** The screen leaving was told which direction the
navigation went and the screen arriving was not, so it always entered from the right: on a
backwards navigation the two slid *past* each other. They are now told the same direction,
worked out once before either is built, and the one arriving comes from the side the one
leaving is going to.

Which direction that is is also no longer only the route table's order. Going back through
history slides back, whatever order the routes happen to be declared in — where you have
been is a better answer than where a route sits in a table, and it is the only answer
available when both ends of the move are the same route.

### What a transition does not fix

A screen arriving takes input where it is *drawn*, slide and all — so a button can slide
out from under a press and cancel it, the same way moving the pointer off a button does.

Testing it where it will land instead was tried and is worse: the pointer is then over one
button and pressing another, and a click that does the wrong thing is worse than a click
that does nothing. The slide is 4% of the width over the transition, so the window in
which this can happen is a couple of frames near a button's edge.

---

Back to [things that move](README.md) · [all documentation](../../README.md)
