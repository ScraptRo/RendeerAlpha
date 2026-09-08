# Stacks, and what a size means

A `<stack>` puts its children in a line. Which line is `arrange`:

```tsx
<stack id="bar" arrange="horizontal" spacing={10} vAlign="center">
```

It used to be `vertical={true}`, which had the flaw that reading `vertical={false}` meant
working out what the other direction is called, and that the two spellings of one decision
did not read alike in a file. `arrange` is one of two words the schema knows, so a typo is
a TypeScript error rather than a silently vertical stack. `<scroll>` keeps two independent
booleans — `vertical` and `horizontal` — because it can do both at once and a stack cannot.

Along the stacking axis, a child's size is one of four answers:

| declaration        | means                                     |
| ------------------ | ----------------------------------------- |
| `width={110}`      | that many pixels                          |
| `width="fill"`     | share out whatever the row has left       |
| `width="content"`  | measure what is in it                     |
| *(nothing said)*   | also: measure what is in it               |

The last row is the one worth stating. Saying nothing is not the same as asking for
nothing, and a stack that read it that way put every child of an unsized row at the same
x — labels drawn on top of each other, which looks like a paint bug and is a measurement
bug. Across the axis it is the other way round: a child with no opinion is stretched to
the line's width, and the alignment across the axis is what decides between those.

## Alignment is one question asked at three levels

The vocabulary is the same at each — `start`, `center`, `end` — because it is the same
arithmetic each time: divide the room left over, and never divide it negatively.

| where | property | what it moves |
| --- | --- | --- |
| a container | `hAlign` / `vAlign` | its children, on each axis, whichever way it runs |
| a container | `spread` | spreads them apart along the stacking axis |
| a child | `hAlignSelf` / `vAlignSelf` | itself, overriding the container for that axis |
| a text node | `hAlign` / `vAlign` | its own text, inside its own box |

`stretch` is the fourth answer only a container can give, and the reason it is not a
child's is that it is not a position: it is "take the whole line". Under `stretch` a child
that named no cross size is given the line; under any of the other three it is measured,
because alignment is only meaningful once a child is allowed to be narrower than its
container. That rule is why alignment on a stack changes *sizes* as well as positions, which
surprises people once and then never again.

`hAlignSelf` exists because the one thing a container's single setting cannot say is "all
of them like this, except that one". Without it the answer is a wrapper container holding
the exception, which is a node in the blueprint whose only purpose is to disagree.

Two things the arithmetic has to get right, and both are tested:

- **Nothing is ever moved to a negative offset.** Content bigger than its box starts at
  the near edge and runs past the far one. Centring it properly would push the beginning
  out of view to make room for the middle, and the beginning is the part worth reading.
- **A stretched child is never moved.** It is already the size of its container, so there
  is nothing to divide — and a child that declined the stretch by naming its own size sits
  at the start rather than being centred, because "I have my own width" is not also "move
  me".

A `<scroll>` answers `hAlign` too, because it is a column that may be taller than
itself. It defaults to `stretch`, which is what it did before the choice existed and what
a scrolling page almost always wants. `<panel>` and `<container>` do not: they place their
children absolutely, and making them lay out would be a different element rather than a
new property on these.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
