# Alignment

Where a thing sits inside the room it was given. One idea, asked at three levels, with the
same vocabulary at each: **`start`, `center`, `end`**.

Named for the **axis**, not for the container's direction. `hAlign` is horizontal and
`vAlign` is vertical, wherever they are written and whichever way the thing holding them
happens to run.

## A container: `hAlign` and `vAlign`

On `<stack>` — and `hAlign` on `<scroll>`, which is a column that may be taller than
itself, and so has only one axis to have an opinion about.

```tsx
<stack arrange="vertical" hAlign="center"> … </stack>
<stack arrange="horizontal" hAlign="center" vAlign="center"> … </stack>
```

Both take the same four words:

| | |
| --- | --- |
| `"stretch"` | every child as wide, or as tall, as the line. **The default** |
| `"start"` | against the near edge, each as big as it needs |
| `"center"` | in the middle |
| `"end"` | against the far edge |

What each one *does* depends on which axis the stack runs along, and the answer is the one
the words already promised. Whichever of the two lies **across** the stacking axis decides
how wide or tall each child becomes. The other lies **along** it, and moves the children
as a group into the room left over.

So in a column, `hAlign` sizes and places each child left to right, and `vAlign` slides
them all up or down together. In a row it is the other way round. Either way,
`hAlign="center"` means the same thing to a reader: horizontally centred.

`stretch` is not a position — it is "take the whole line" — which is why it is a
container's answer and not a child's, and why it reads as `start` on the axis the children
are stacked along. Stretching a child along that axis is what its own `fill` is for.

### Why absolute names

`arrange` may be a binding:

```tsx
<stack arrange={() => state.rda.width < 700 ? "vertical" : "horizontal"} hAlign="center">
```

A stack written like this changes axes while the application is open. An alignment named
for the cross axis would silently come to mean the other one at that moment, turning a row
centred horizontally into a column centred vertically, with nothing in the log. An
alignment named for the axis cannot.

## A word, and however far past it

Every alignment takes an offset, written the way a size takes one:

```tsx
<panel hAlignSelf="center+60" />     {/* sixty to the right of centred */}
<panel hAlignSelf="end-40" />        {/* forty short of the far edge */}
<label hAlign="start+12" text="indented" />
<panel hAlignSelf={() => `start+${state.indent}`} />
```

The word is clamped and the offset is not. Something too big for its box still starts at
the near edge, and `center+20` is twenty past the middle wherever that lands, including
when there was no room to move it in the first place.

This is the only way to move a child **inside** a stack. `x` and `y` place a widget
absolutely, and a stack ignores them because it decides positions itself; an offset on
the alignment is how you say "there, but a little further".

A child cannot place itself **along** the stacking axis, with or without an offset. In a
column, a child's `vAlignSelf` is not read: where the children sit down the column is the
column's decision, and the alternative is a child that overlaps the one after it.

## Spreading them apart: `spread`

The one thing that has no meaning across an axis, and so is the one thing still tied to
it: sharing out the room left over **between** the children rather than moving them as a
group.

| | |
| --- | --- |
| `"spaceBetween"` | first and last against the edges, gaps equal |
| `"spaceAround"` | equal space around each, so the edges get half a gap |

```tsx
<stack arrange="horizontal" spread="spaceBetween" vAlign="center">
  <label text="Files" />
  <button text="Refresh" />
</stack>
```

Only meaningful when there *is* room left over. A child sized `fill` takes the remainder,
so a row containing one has nothing to distribute and `spread` does nothing — which is
the right answer rather than a special case.

`spread` no longer takes `start`, `center` or `end`: those are `hAlign` and `vAlign`
now, on whichever axis you meant. A layout that still writes one says so in the log.

## A child: `hAlignSelf` and `vAlignSelf`

Accepted by **every** element. Each overrides its container's setting for that axis, for
one child. The container reads whichever of the two lies across its stacking axis.

```tsx
<stack arrange="vertical" hAlign="end">
  <button width={90}  text="one" />
  <button width={130} text="two" />
  <button width={130} text="the exception" hAlignSelf="start" />
</stack>
```

`"auto"` — the default — means the container decides. The other four are the same words
the container takes. They are on the child rather than the container because the one thing
a container's single setting cannot say is "all of them like this, except that one", and
the alternative is a wrapper that exists only to hold an exception.

## Text: `hAlign` and `vAlign`

On `<label>` and `<button>`, the same pair again, meaning the same thing: where the text
sits inside the box the layout gave it.

```tsx
<label hAlign="center" vAlign="center" height={32} text="centred both ways" />
<label hAlign="end"    width="fill"    text="a column of numbers" />
<button hAlign="start" text="a menu entry" />
```

A label defaults to start and top; a **button defaults to centred**, because that is what
a button looks like — `hAlign="start"` is for a column of them used as a menu, where
centred text makes every entry begin somewhere different.

The two questions are separate on purpose. A label in a column is usually as wide as the
column and as tall as one line, so `hAlign` moves the text within a box wider than it, and
`vAlign` matters the moment a row is taller than a line — which is any row with a button
in it.

Text bigger than its box starts at the near edge and runs past the far one. What overflows
is the end, never the beginning.

## If you have older layouts

These names changed, and the old ones are gone rather than quietly accepted:

| Was | Is |
| --- | --- |
| `align` on a container | `hAlign` or `vAlign` — whichever axis it used to mean |
| `spread="start\|center\|end"` | `hAlign` or `vAlign` on that axis |
| `spread="spaceBetween\|spaceAround"` | unchanged |
| `alignSelf` | `hAlignSelf` or `vAlignSelf` |
| `align` / `valign` on text | `hAlign` / `vAlign` |

Each one is named in `RDA_DEBUG.txt` when a layout using it loads, with what to write
instead. A renamed property that was silently ignored would be worse than one that broke:
the layout would keep working and quietly lose its alignment.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
