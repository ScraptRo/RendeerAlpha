# Large data

## `<list>`
Rows from a declared table, showing only as many widgets as fit.

| | |
| --- | --- |
| `of` | which declared table the rows come from |
| `row` | the template one row is built from |
| `rowHeight` | how tall a row is, and the fallback for any row the column below has no answer for |
| `rowHeights` | a number column holding each row's own height, for rows that are not all alike |
| `spacing` | gap between rows |
| `poolSize` | how many row widgets to keep; enough to fill the view |
| `barWidth`, `wheelStep` | the bar |
| `follow` | stay at the end when rows arrive — but only while already there |
| `revealRow` | scroll until this row index is on screen |
| `variant` | a textfield variant |

```tsx
<list id="catalogue" of="products" rowHeight={26} spacing={2} height="fill"
      row={(item) => (
        <stack id="row" arrange="horizontal" spacing={10} padding={4} vAlign="center">
          <label id="name"  width="fill" height="content" text={() => item.title} />
          <label id="price" width={90}   height="content" hAlign="end"
                 text={() => `$${item.price}`} />
        </stack>
      )} />
```

`row` is a function called **once**, at build time. The template it returns is
instantiated a fixed number of times — about as many as can be visible — and scrolling
does not create or destroy anything: it changes which row index each pooled copy shows and
re-evaluates that copy's bindings against it.

`item` is a real function parameter, so TypeScript checks `item.price` against the column
declared in `res/state.ts`. Reading `item.` outside a row template is an error naming the
name that was not in scope.

## Rows that are not all the same height

A transcript is the obvious case: one message is a line, the next is a paragraph. Name a
number column and each row is as tall as that column says.

```tsx
<list id="log" of="messages" rowHeight={28} rowHeights="height" spacing={4}
      row={(item) => (
        <panel id="bubble" anchor="fill">
          <label id="body" width="fill" height="fill" wrap text={() => item.body} />
        </panel>
      )} />
```

The **sender** fills that column, and can, because the engine will measure for it:
`rda.measure_text(body, 0)` gives the width a string would be drawn at, which over the
width of the row is the number of lines, which is the height. Python `measure_text`, Node
`measureText`, C# `MeasureText`.

**Not measured by the engine, on purpose.** Finding out how tall a row came to means
binding it and laying it out, and a list exists precisely so that ten thousand rows are
not ten thousand binds every time the table moves. A column is one read per row.

A row whose height is missing, zero or negative is `rowHeight` tall, so a column that is
only half filled in gives a sensible list rather than a list with holes in it. Naming a
column that does not exist, or one that is not a number column, says so in the log and
leaves every row at `rowHeight`.

**What it costs.** The list keeps a running total — where each row begins — rebuilt in one
pass whenever the table changes, and finds the first visible row by searching it instead of
dividing. A list with no `rowHeights` does neither: that path is exactly what it was, a
multiply, and it stays the default.

## Scrolling to a row

`revealRow` is how an application points at something: a search hit, the row a keyboard
selection just moved to, the line an error is on.

```tsx
<list id="results" of="matches" rowHeight={24} revealRow={() => state.selected} ... />
```

It scrolls the smallest distance that brings the row into view, and does nothing when the
row is already visible — so walking a selection down a long list moves one row at a time at
the bottom edge instead of jerking the row to the middle on every step. A row already on
screen is left exactly where it is.

It acts when the value **changes**, not while it holds. Asking for row 40 and then dragging
the bar away does not snap back to 40; asking for 40 again does. `-1` means nothing is being
asked for, which is what an application that has no selection should write.

Ten thousand rows cost ten thousand rows and about thirty widgets. The alternative — a
widget per item — costs 631 nodes and 88 KB for **two hundred**, all built at start-up
whether or not anyone scrolls to them.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
