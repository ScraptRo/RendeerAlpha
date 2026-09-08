# Large data

## `<list>`
Rows from a declared table, showing only as many widgets as fit.

| | |
| --- | --- |
| `of` | which declared table the rows come from |
| `row` | the template one row is built from |
| `rowHeight` | every row is the same height |
| `spacing` | gap between rows |
| `poolSize` | how many row widgets to keep; enough to fill the view |
| `barWidth`, `wheelStep` | the bar |
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

Ten thousand rows cost ten thousand rows and about thirty widgets. The alternative — a
widget per item — costs 631 nodes and 88 KB for **two hundred**, all built at start-up
whether or not anyone scrolls to them.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
