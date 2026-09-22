# Motion

## `animate`

Milliseconds a widget eases over when the layout moves it. **Inherited by its children**,
so one number on a container animates everything under it. Zero moves at once.

```tsx
<stack id="root" animate={180}> … </stack>
```

Only what is *drawn* lags. What a widget measures as, and what its parent measures it as,
is the value the layout computed — otherwise a stack would size itself to a transient
number and the whole layout would wobble on its way to settling.

A `<list>` does not pass it down to its rows: a list puts its pooled rows in new places
every frame as it scrolls, and easing that is not a slide, it is a smear.

On a `<tabs>` it is how long a page takes to cross-fade into the next.

## `visible`

```tsx
<stack visible={() => state.expanded}> … </stack>
```

False means not drawn and not clickable. Inside a `<stack>`, and with `animate` set, a
widget becoming invisible **collapses**: the room it takes shrinks over the same duration,
so everything after it moves up rather than jumping. Elsewhere — in a scroll view, a list,
a dock panel — it still disappears at once.

**What is inside it travels with the edge.** A collapsing widget's contents keep their
full size and slide, so the closing edge pushes them along rather than eating into them:
a panel's padding survives, a centred label stays centred, and no text re-wraps on the
way out. A sidebar closing to the left takes its insides left; a panel closing upwards
takes them up. What has slid past the near edge is clipped, and the whole thing fades as
it goes.

**A collapse is the one move `route` and `pace` do not shape.** Its size is already being
animated by the collapse itself, and easing towards an easing value arrives late and
overshoots nothing — so the two do not compose. This is worth knowing because a panel
that appears and disappears is the first thing anybody reaches for a `pace` on, and it is
exactly the case that ignores it.

## The shape of a move, and the speed of it

`animate` says how long a move takes. Two more properties say what the move *is*.

| | |
| --- | --- |
| `route` | the shape it travels, as an SVG path |
| `pace` | how fast it travels that shape: `"linear"`, `"in"`, `"out"`, `"inOut"`, or `"cubic-bezier(x1,y1,x2,y2)"` |

```tsx
<panel id="card" animate={900}
       route="M0 0 C 80 -90 220 -90 300 0"
       pace="cubic-bezier(0.9, 0, 0.1, 1)"
       x={() => (state.open ? 330 : 30)} />
```

These are two curves and they are deliberately independent: the same arc at a different
pace, and the same pace along a different arc, are different motions, and neither should
require re-authoring the other.

**Why this needed a property at all.** Before it, a widget's `x` and its `y` were eased
separately. That makes the route a straight line always — not as a decision, but because
there was nowhere for a decision to live. Everything a designer means by "motion" beyond a
fade was in the part that was missing.

**The route is fitted, not absolute.** Its first point lands on wherever the move starts
and its last on wherever it ends, turned and scaled to match. So an arc bends relative to
the direction of travel, and one route works between any two places — write it once at any
size, in any drawing tool that exports a path.

It is measured by **distance**, not by the curve's parameter. Those are not the same: a
bezier's control points bunch its parameter up, so travelling at a constant parameter rate
visibly speeds up and slows down on its own. That would fight whatever pace you asked for,
and the pace would get the blame.

**A pace is a cubic through (0,0) and (1,1)**, x being time and y being distance covered —
what every design tool draws and what CSS spells `cubic-bezier()`. A `y` above 1 is an
overshoot, which is how something arrives by settling into place rather than stopping dead.

Position travels; **size does not**. A route is a route through space, and a widget growing
is not going anywhere — so both are paced together and only one follows a shape.

Both need `animate` to be more than zero. A route is a shape to travel, and something
arriving instantly does not travel.

Where it applies: children of a `<stack>`, which is where motion has always been eased, and
widgets a `<container>` places by their own `x`/`y` — the second being new, and opt-in,
since it only happens when a route is written.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
