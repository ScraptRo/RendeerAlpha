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
| `poolSize` | how many row widgets to build before the list is first drawn. Optional: without it the list builds what its view needs, when it needs it |
| `barWidth`, `wheelStep` | the bar |
| `follow` | stay at the end when rows arrive — but only while already there |
| `revealRow` | scroll until this row index is on screen |
| `sortBy`, `sortDescending` | show the rows ordered by a column — see [Sorted and filtered](#sorted-and-filtered) |
| `filter`, `filterColumns` | show only the rows containing a string |
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
instantiated as many times as rows fit in the list's view, and no more — a list three rows
tall has three copies, one filling a tall window has what fills it — and scrolling does not
create or destroy anything: it changes which row index each pooled copy shows and
re-evaluates that copy's bindings against it.

The copies are built the first time the list is drawn, and more are built if the view
grows — a taller window, a list given more room — on the frame it grows, so a row never
appears late. They are kept when the view shrinks, since it is likely to grow back and an
undrawn row costs nothing per frame. `poolSize` builds a number ahead of time instead, for
a list that would rather do that work while the layout loads.

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

## Sorted and filtered

The table is the backend's, in the backend's order. How the reader wants to *see* it — by
size, only the ones mentioning "invoice" — is the interface's, and does not need a second
copy of the table to say:

```tsx
<list id="files" of="files" sortBy="size" sortDescending={true}
      filter={() => state.query} row={(f) => ...} />
```

`sortBy` names a column. Numbers sort by value, booleans false first, and text the way a
person sorts it: ignoring case, and with the digits in a name read as a number, so
`file2` comes before `file10`. The sort is stable — rows that tie keep the table's order —
and `sortDescending` reverses each comparison rather than the whole list, so ties keep it
that way round too.

`filter` keeps the rows that contain it, ignoring case, in every text column, or in the
columns `filterColumns` names, separated by `|`. A number column named there is searched
as it is shown, so `"120"` finds a size of 120.

The table is never reordered or copied: the list keeps the order as a list of row numbers,
worked out again when the table or one of these changes and at no other time. `revealRow`
is still a *table* row, and does nothing for one the filter has hidden. These are for a
`<list>`; a `<tree>`'s order is its hierarchy.

Ten thousand rows cost ten thousand rows and about thirty widgets. The alternative — a
widget per item — costs 631 nodes and 88 KB for **two hundred**, all built at start-up
whether or not anyone scrolls to them.

## `<tree>` — rows that hang off one another

A file tree, an outline, a scene graph. It is a `<list>` that shows some of its rows: the
table holds every node, each followed by everything under it, and a `depth` column says how
far in each one is. Nothing is nested, so a tree of fifty thousand nodes is fifty thousand
rows and about thirty widgets, and a backend fills it with the loop it already has for a
list plus one number per row.

```tsx
<tree id="files" of="nodes" openDepth={1}
      selected={() => state.chosen} onSelect={(path) => state.chosen = path}
      onActivate={(path) => commands.open()}
      row={(node) => <label id="name" y={3} text={() => node.name} />} />
```

| id | name | depth |
| --- | --- | --- |
| `src` | src | 0 |
| `src/core` | core | 1 |
| `src/core/a.cpp` | a.cpp | 2 |
| `docs` | docs | 0 |

| | |
| --- | --- |
| `of` | the table |
| `depthColumn` | the number column saying how far in a node is; `depth` unless said |
| `keyColumn` | what a node is known by; `id` unless said. What it remembers being open by, and what every handler is given |
| `childrenColumn` | a bool column for "has children that are not loaded yet"; `hasChildren` unless said |
| `indent` | how far in each level is. 16 by default |
| `openDepth` | nodes shallower than this start open. 0, the default, starts everything shut |
| `selected` | the chosen node's key |
| `multiSelect`, `selectedKeys`, `onSelectionChange(string)` | several nodes chosen at once — see [Choosing several](#choosing-several) |
| `rowHeight`, `spacing`, `poolSize`, `barWidth`, `wheelStep`, `revealRow`, `variant` | as on `<list>` |
| `onSelect(string)` | a node was clicked or arrowed to |
| `onActivate(string)` | a node was double-clicked, or had Enter pressed on it |
| `onExpand(string)`, `onCollapse(string)` | a node was opened or shut |

A node has an arrow when the row after it is deeper, or when its `hasChildren` is true.
Clicking the arrow opens or shuts it; clicking anywhere else on the row chooses it; double
clicking opens or shuts a node that has children and reports `onActivate` either way, so the
same gesture opens a folder and a file. The row template is laid out to the right of the arrow,
already moved in by its depth.

**What is open belongs to the tree**, the way what is typed belongs to a field: it is what
the reader did. It is remembered by key, so it survives the table being refilled, and a
node that `openDepth` opened and the reader shut stays shut.

**Loading children when they are wanted.** Give a node `hasChildren: true` and nothing
under it, and it has an arrow and starts shut whatever `openDepth` says. Opening it runs
`onExpand` with its key — the moment to read the folder and insert its rows after it, one
level deeper. They appear under it as they arrive.

**The keyboard**, once it has been clicked or tabbed to: Up and Down move the choice,
Home and End go to the ends, Right opens a node and then steps into it, Left shuts it and
then steps out to its parent, Space opens or shuts, and Enter activates. The choice is
kept in view as it moves.

`selected` is a binding the tree never writes back: it reports the choice with `onSelect`
and draws it at once, and the layout writes what it wants to keep. `revealRow` is a *table*
row, as on a list, and does nothing for one inside a shut node.

## `<grid>` — a table as a table

Columns under titles: click a title to sort by it, again to turn it round, drag the edge
between two titles to make a column wider. A file browser's detail view, a list of orders,
a log.

```tsx
<grid id="files" of="files" height="fill" alt="Files"
      selected={() => state.chosen} onSelect={(id) => state.chosen = id}
      onActivate={(id) => commands.open()}
      filter={() => state.query}>
  <column id="name" field="name" title="Name" width="fill" minWidth={120} />
  <column id="kind" field="kind" title="Kind" width={90} />
  <column id="size" field="size" title="Size" width={90} hAlign="end" />
  <column id="done" field="done" title="Done" width={60} hAlign="center" sortable={false} />
</grid>
```

A `<list>` builds a few row widgets and re-binds them as it scrolls. A grid builds **none**:
a cell is a value from the table drawn as text, so a grid of a hundred thousand rows and ten
columns is one widget, and drawing it costs the rows on screen. That is also the trade — a
cell cannot hold a button or a checkbox. A row that needs widgets is a `<list>`, which now
sorts and filters the same way.

| | |
| --- | --- |
| `of` | the table |
| `keyColumn` | what a row is known by; `id` unless said. What `selected` holds and every handler is given; without such a column, the row's position |
| `selected` | the chosen row's key |
| `multiSelect`, `selectedKeys`, `onSelectionChange(string)` | several rows chosen at once — see [Choosing several](#choosing-several) |
| `sortBy`, `sortDescending` | the order, as on `<list>`. Clicking a title sets both |
| `filter`, `filterColumns` | as on `<list>` |
| `rowHeight` | 24 by default |
| `headerHeight` | how tall the titles are; a line of text and some room unless said |
| `barWidth`, `wheelStep`, `variant` | as on `<list>` |
| `onSelect(string)` | a row was clicked or arrowed to |
| `onActivate(string)` | a row was double-clicked, or had Enter pressed on it |
| `onSort(string)` | a title was clicked: the column, with a `-` in front for descending — `"size"`, `"-size"` |

**`<column>`** says which field, and how to show it. It draws nothing itself and belongs
only inside a `<grid>`.

| | |
| --- | --- |
| `field` | the table column it shows |
| `title` | what its title says; the field's name unless said |
| `width` | a number, or `"fill"` for a share of what the other columns leave — `"fill:2"` for twice the share. 120 unless said |
| `minWidth` | narrower than this it is not squeezed: the grid scrolls sideways instead |
| `hAlign` | `start`, `center` or `end`, for the values and the title together. Numbers read best at `end` |
| `sortable` | clicking its title sorts by it; true unless said |
| `decimals` | places a number is shown to; as many as it has unless said |

A number with no fraction shows as a whole number, a bool as a tick or nothing, and text as
it is. Clicking a title sorts by it, ascending, and clicking it again descending; the
grid does this itself and says so with `onSort`, so a layout that binds `sortBy` to keep the
order in state and one that binds nothing both work. Dragging the line at a title's right
edge resizes that column, double-clicking the line gives it back the width it was written
with, and a resized column keeps its width while the grid is open. When the columns are
wider than the grid there is a second bar along the bottom, and Shift with the wheel
scrolls sideways.

**The keyboard**, once it has been clicked or tabbed to: Up and Down move the choice, Page
Up and Page Down move it a screen, Home and End go to the ends, and Enter activates. The
choice is kept in view as it moves. While a grid has the keyboard, Home, End and the page
keys are its own rather than a shortcut's.

**A screen reader** reads a row as one sentence, every column titled — "Name: notes.txt,
Kind: text, Size: 120" — and a bool column by its title when it is ticked. The rows scrolled
out of view are in the tree too, the way a list's are.

## Choosing several

A `<grid>` or a `<tree>` with `multiSelect` lets the reader choose more than one row: the
files to delete, the orders to mark shipped.

```tsx
<grid id="files" of="files" multiSelect={true}
      selectedKeys={() => state.picked} onSelectionChange={(keys) => state.picked = keys}
      selected={() => state.current} onSelect={(key) => state.current = key}>
  ...
</grid>
```

| | |
| --- | --- |
| a click | this row and nothing else |
| **Ctrl**+click | this row in or out, keeping the rest |
| **Shift**+click | every row from the last one clicked to this one, and nothing else |
| **Ctrl+Shift**+click | the same reach, added to what is already chosen |
| **Shift** with Up, Down, Page Up, Page Down, Home or End | reach from the last row clicked, the way Shift+click does |
| **Ctrl** with the same keys | move without choosing anything |
| **Ctrl+Space** | the row the keyboard is on, in or out |
| **Ctrl+A** | every row shown |

The chosen rows are `selectedKeys`, **one key per line** — `"f2
f4
f5"` — in the order
they were chosen. A signal holds one value, and a line is something a key does not
contain; it is how a drop of files is handed over too. `onSelectionChange` hands over the
same string whenever it changes. As with `selected`, the widget never writes the binding
back: it shows the change at once and reports it, and the layout keeps what it wants.

`selected` goes on meaning one row: the one the keyboard is on, outlined so it can be seen
whether or not it is one of those chosen. `onSelect` reports it as it moves and
`onActivate` still activates it, so a double click opens one file in a grid of several
chosen.

Rows are chosen by key, so sorting a grid or opening a node does not lose them, and a reach
works out "from there to here" from where the rows are *now*. A reach in a tree covers the
nodes that are shown: sweeping past a shut folder chooses the folder, not what is inside it.
A key the table no longer has stays in `selectedKeys` until the layout writes something else
— which rows still exist is the backend's to know.

A screen reader is told the list or tree is multi-selectable, and can add a row, take one
out, choose all of them or none: UI Automation's Selection pattern on Windows, AT-SPI's
Selection interface on Linux.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
