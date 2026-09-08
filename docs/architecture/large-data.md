# Scrolling, tables and large data

## Scrolling, and large data

`<scroll>` is a window onto one child that is bigger than it. It does not lay anything
out: it clips to itself, moves the child, and draws the bars. The child does its own
layout and never learns it is being scrolled, so the same wrapper works for a column, a
row, or a container that does not exist yet.

```tsx
<scroll id="list" height="fill">
  <stack id="rows" arrange="vertical" spacing={2}> ... </stack>
</scroll>
```

Only what is visible is painted, but that is not the scroll widget's doing. A container
asks whether a child overlaps `gui.currentClipRect()` before painting it, so the saving
belongs to **everything clipped** — a column inside a dock panel gets it too — rather than
being a privilege of scrolling. The older `<scroll>`, which is a vertical stack that
scrolls itself, still exists and still works.

### Where this stops scaling

A layout is a compiled blueprint: a fixed tree. A build-time loop can generate structure,
so 200 rows is a `.map()` — and the cost is real and measurable. A layout that does
that for 200 rows compiles to:

```
631 nodes, 87985 bytes
```

That is 200 rows. Ten thousand products would be roughly 31,500 widgets and a 4 MB
blueprint, all of it built at start-up whether or not anyone scrolls to it. Clipping stops
the *painting* cost, not the existence cost. So generating a row per item is right for a
settings page and wrong for a catalogue.

Two different problems hide behind "large data", and they want different answers.

**Many items.** The missing piece is not a widget, it is a data source. Signals hold one
number, flag or string; a list needs N rows of named columns, owned by C++ the way signals
are, with its own change notification (the count changed, row *i* changed). Given that, a
list widget keeps a pool of row widgets about the size of the visible area and changes
*which index each row shows* as you scroll — which in this design is not a new mechanism
at all, because rebinding a widget to a different value is the one thing the whole system
already does. The row template's bindings would name their item the way a handler names
the value it was passed: one more scope, resolved at bind time, in a system that just grew
exactly that for `onChange`.

**One enormous text.** Handled. A text field in document or code mode computes `firstLine`
from its scroll offset and draws only the lines in view, so a 10 MB log renders forty lines.

The bookkeeping around that used to be the expensive part. Every walk of the widget tree
rebuilt the line index twice and hashed the whole string once more for syntax highlighting
— three passes over the document to draw a screenful. It never showed up when idle, because
the retained cache skips the walk entirely when nothing changed; it showed up whenever
*anything* changed, including a mouse moving over an unrelated button.

A `TextField` now carries a version, bumped whenever its text is written — by a binding, by
the application, or by the field editing itself. `Gui::textField` takes that version and
keeps the line index, the syntax spans and the line count across frames, rebuilding them
only when it changes. Passing zero means "cannot say" and restores the old behaviour, which
is what an immediate-mode caller holding a string it does not own has to do.

Measured with six seconds of cursor movement over a screen of buttons, touching no
text at all:

```
before: 122 rebuilds        after: 1
```

---

## Tables and lists

The missing piece for a catalogue was never a widget, it was a data source. A signal holds
one number, flag or string. A list needs N rows of named fields, owned by C++ the way
signals are — so it is declared where signals already are:

```xml
<state>
  <number name="count" value="0"/>
  <table  name="products" doc="the catalogue a list shows">
    <text   name="title"/>
    <number name="price"/>
    <bool   name="inStock"/>
  </table>
</state>
```

which generates a typed row struct and a bulk setter for C++, and a matching row interface
for TypeScript:

```cpp
std::vector<RDA::State::ProductsRow> catalogue;   // fill it however
RDA::State::setProducts(catalogue);               // one call, column order kept
```

### The row template is a function

```tsx
<list id="catalogue" of="products" rowHeight={26} height="fill"
      row={(item) => (
        <stack id="row" arrange="horizontal" spacing={10} vAlign="center">
          <label id="name"  width="fill"    text={() => item.title} />
          <label id="price" width="content" text={() => `$${item.price}`} />
        </stack>
      )} />
```

A function rather than nested children, for one reason that decides it: `item` is a real
parameter with a real type, so TypeScript checks `item.price` against the column declared
above. And the mechanism already existed — this is the handler parameter from `onChange`,
used a second time. `item.title` compiles to one instruction, `push.field #title`, which
fetches a column from whatever row is current, the way `push.event` fetches the value a
handler was passed.

The function runs **once**, when the layout is compiled, to produce the template. It is
never called per row and never ships.

### What it costs

Two hundred rows written out by a build-time loop, against ten thousand rows in a table:

```
scrolling.tsx    631 nodes, 87985 bytes     (200 rows)
catalogue.tsx      9 nodes,  1808 bytes  (10 000 rows)
```

The blueprint stops growing with the data, because the data is no longer in it. At run
time the list keeps a pool of row widgets about as large as the view, and scrolling
changes *which row index each copy shows* — nothing is created or destroyed.

That rebinding is not a new mechanism. It is the same evaluate-and-apply that every
binding uses, called with a row instead of waiting for a signal. Which is also why **a
table needs one version rather than a dirty set**: when anything in it changes the visible
rows are re-read, and the visible set is tiny by construction.

A binding inside a template that does *not* read a column stays an ordinary binding,
driven by signals like any other, and goes on working as the row is reused.

### What it does not do yet

- **Variable row heights.** Every row is `rowHeight` tall, which is what makes the visible
  range fall out of the scroll offset with no search. Variable rows need a running total
  maintained on every edit.
- **A pool that grows.** `poolSize` defaults to 40 copies, built when the layout loads. A
  taller view says so in the log rather than leaving a gap nobody can explain.
- **Narrowing `item` across several tables.** With one table declared its type is exact.
  With several, `RdaRow` is a union and a template annotates its parameter, because the
  element cannot be generic over a string prop.
- **Sorting and filtering.** The application owns the order. A view of indices into the
  table is the natural next step.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
