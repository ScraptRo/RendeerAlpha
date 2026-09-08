# Sizing

Two properties, `width` and `height`, each taking a number or one of three words.

| | |
| --- | --- |
| `200` | exactly 200 pixels |
| `"content"` | as big as the widget says it needs |
| `"fill"` | share of what is left over |
| `"fill:2"` | twice the share of a plain `fill` beside it |
| `"content-23"`, `"fill-40"`, `"content+8"` | the same, less or plus a number of pixels |

Plus `minWidth`, `maxWidth`, `minHeight`, `maxHeight` — bounds applied to whatever was
chosen.

```tsx
<stack arrange="horizontal" spacing={8}>
  <panel width={200} />          {/* fixed */}
  <panel width="fill" />         {/* the rest */}
  <label width="content" />      {/* as wide as its text */}
</stack>
```

**Saying nothing is not the same as saying zero.** Along the stacking axis, a widget with
no declared size falls back to its `rect` if it has one and is measured otherwise — so a
row of labels that declared no width is a row of labels, not a pile of them at the same x.
Across the axis, saying nothing means "stretch me to the line", which is what a child of a
column almost always wants.

## An offset on a word

`"content"` and `"fill"` are not values: one is a request to measure, the other a claim on
room nobody has divided yet. Both are answered during layout, long after every binding has
run — which is why `"content" - 23` cannot be written as arithmetic, and why the offset
rides along with the word instead.

```tsx
<label  width="content-23" />          {/* its text, less 23 */}
<panel  width="fill-40" />             {/* its share of the leftover, less 40 */}
<panel  width={() => `fill-${state.gap}`} />   {/* and the offset may be computed */}
```

The last one is an ordinary template literal, which a binding already builds, so nothing
new is needed to work out an offset at run time.

Applied after the word resolves and before `minWidth` and `maxWidth`, so a bound size
still obeys its bounds. The result never goes below zero.

A number needs no offset: `width={() => state.rda.width - 23}` is arithmetic on a signal
and has always worked. The offset is for the two words that hand the decision to the
layout.

`"content"` costs a measurement: a label measures its text, a text field counts its lines.
Worth knowing, not worth avoiding.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
