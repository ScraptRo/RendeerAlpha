# Elements

The complete list. There are no others. Generated from the same table the loader reads,
so anything missing here is missing at run time.

Every property may be a constant or a binding: `width={200}` or
`width={() => state.wide ? 400 : 200}`. Handlers (`onClick`, `onChange`) are always a
thunk.

`Size` means a number of pixels, `"content"`, `"fill"`, or `"fill:2"` (twice the share of
a plain `fill`). Either word may carry an offset — `"content-23"`, `"fill-40"`,
`"content+8"` — applied after the layout resolves the word. A binding may compute one:
`width={() => `content-${state.gap}`}`.

Prefer all of that over arithmetic on the window's size; `state.rda.width` is there for
decisions a layout cannot express, such as changing a stack's direction.

## On every element

| Property | Type | Meaning |
| --- | --- | --- |
| `id` | string | required in practice; unique among siblings |
| `visible` | boolean | drawn and interactive when true |
| `width` `height` | Size | how big, when the container is deciding |
| `minWidth` `maxWidth` `minHeight` `maxHeight` | number | bounds on the resolved size |
| `animate` | number | ms this eases over when the layout moves it; inherited by children; 0 = instant |
| `route` | string | the shape it travels when it moves, as an SVG path; fitted to wherever it is going |
| `pace` | string | how fast it travels it: `linear` `in` `out` `inOut` or `cubic-bezier(x1,y1,x2,y2)` |
| `hAlignSelf` `vAlignSelf` | `auto` `stretch` `start` `center` `end`, each with an optional offset | this child's answer to its container, per axis |
| `x` `y` `w` `h` | number | absolute placement inside the parent's content box |
| `marginRight` `marginBottom` | number | distance kept from that parent edge when anchored |
| `anchor` | `fill` `stretchX` `stretchY` `bottomLeft` `bottomRight` | which parent edges this follows |
| `onHover` | handler(boolean) | true when the pointer arrives, false when it leaves |

`onHover` fires on the edges only, never every frame the pointer is inside, so a handler
that writes a signal is not rewriting it sixty times a second. It answers for the rectangle
the widget was drawn at. Buttons already have a hovered colour in the theme -- reach for
this for tooltips and detail panels, not for rebuilding a button.

`route` and `pace` are two curves and they are independent: the route is the shape of the
move, the pace is the speed along it. The route is an SVG path **fitted** to the journey --
first point on where it starts, last on where it ends, turned and scaled -- so one arc works
between any two places and bends relative to the direction of travel. It is measured by
distance, not by the curve's parameter, so a pace means what it says. A pace is a cubic
through (0,0) and (1,1), x time and y distance; a y above 1 overshoots. Position travels,
size does not. Both need `animate` > 0.

Use `x/y/w/h` and `anchor` only inside a `<container>` or `<panel>`. Inside a `<stack>`
the stack decides positions.

**Every alignment takes an offset**, the way a size does: `hAlign="center+20"`,
`hAlignSelf="end-40"`, `` hAlign={() => `start+${state.indent}`} ``. It is the only way to
nudge a child inside a stack. A child cannot align itself along the stacking axis, only
across it.

## Containers

### `<stack>` — a row or a column

| Property | Type | Meaning |
| --- | --- | --- |
| `arrange` | `vertical` `horizontal` | which way children are laid out |
| `spacing` | number | gap between children |
| `padding` | number | inset around the whole row or column |
| `hAlign` | `stretch` `start` `center` `end` | where children sit horizontally (default `stretch`) |
| `vAlign` | `stretch` `start` `center` `end` | where children sit vertically (default `stretch`) |
| `spread` | `spaceBetween` `spaceAround` | spreads them apart along the stacking axis |

The workhorse. Nest them: a vertical stack of horizontal stacks is most interfaces.

`hAlign` and `vAlign` are absolute: they mean the same thing whichever way the stack runs.
Whichever lies across the stacking axis sizes and places each child; the other moves them
all together. So a column takes `hAlign` to place its children side to side, and a row
takes `vAlign`.

### `<panel>` — a framed background that clips its children

| Property | Type | Meaning |
| --- | --- | --- |
| `variant` | string | theme variant (`panel` in the theme file) |

### `<container>` — groups children, draws nothing

| Property | Type | Meaning |
| --- | --- | --- |
| `visible` | boolean | the only one worth setting |

For absolute placement, and for turning a group on and off with one `visible`.

### `<scroll>` — a clipped window onto content bigger than itself

| Property | Type | Meaning |
| --- | --- | --- |
| `vertical` | boolean | scrolls down when its content is taller; on by default |
| `horizontal` | boolean | scrolls sideways when its content is wider |
| `spacing` | number | gap between stacked children |
| `padding` | number | inset around the content |
| `hAlign` | `stretch` `start` `center` `end` | where children sit across the column |
| `barWidth` | number | thickness of the bars |
| `wheelStep` | number | pixels per wheel notch |
| `variant` | string | theme variant (`textfield` styles) |

It stacks its children in a column, so one child is simply a column of one. The wheel
scrolls down, and sideways with shift. For rows from a table use `<list>`, which stays
cheap at ten thousand rows.

### `<splitter>` — a draggable bar that resizes what is above it

| Property | Type | Meaning |
| --- | --- | --- |
| `arrange` | `vertical` `horizontal` | which way it splits |
| `value` | number | the size it controls |
| `min` `max` | number | how far it drags |

## Text and input

### `<label>` — a line of text

| Property | Type | Meaning |
| --- | --- | --- |
| `text` | string | what is drawn |
| `wrap` | boolean | break to its width instead of running past |
| `spans` | string | runs drawn in their own colour: `start:length:colour` triples separated by `;` |
| `hAlign` | `start` `center` `end` | across its box |
| `vAlign` | `start` `center` `end` | down its box |
| `variant` | string | theme variant (`label`) |

`spans` is how one label carries more than one colour -- syntax colour inline, a diff, a
search highlight. Offsets are **bytes**, the way the caret and the wrap ranges are; for
ASCII they are character counts. It is an ordinary bindable string, so a `<list>` row keeps
its own colouring in a column. Text no span covers keeps the label's colour, a run crossing
a wrap is coloured on both lines, and anything malformed -- half a triple, a colour that is
not one, a length of zero -- is skipped rather than fatal, because this arrives from a
backend a character at a time. Out of range is clipped, a negative start is pulled to zero,
and overlaps go to whichever starts first.

### `<button>` — clickable

| Property | Type | Meaning |
| --- | --- | --- |
| `text` | string | the label on it, when it holds nothing else |
| `hAlign` | `start` `center` `end` | where that text sits; centred unless said otherwise |
| `padding` | number | inset around its children, when it has any |
| `variant` | string | theme variant (`button`) |
| `onClick` | handler | runs on each completed click |

A button may hold children instead of text, laid out inside it like a `<panel>`'s. Put a
`<stack>` inside one to arrange them:

```tsx
<button id="save" variant="primary" width={220} height={64} onClick={() => commands.save()}>
  <stack id="c" arrange="horizontal" spacing={8} anchor="fill" vAlign="center">
    <image id="i" src="res/save.png" width={24} height={24} />
    <label id="t" width="fill" height="content" text="Save" />
  </stack>
</button>
```

The children draw and the button takes the input, so a clickable widget inside one does
not receive its own clicks.

### `<textfield>` — editable text

| Property | Type | Meaning |
| --- | --- | --- |
| `text` | string | the contents |
| `mode` | `line` `document` `code` | one line, a text area, or an editor with a gutter |
| `submitKey` | `enter` `ctrlEnter` `both` `none` | which keystroke runs `onSubmit` |
| `language` | string | the grammar to colour with, overriding the variant's |
| `variant` | string | theme variant (`textfield`) |
| `onChange` | handler(string) | runs on each edit, with the new contents |
| `onSubmit` | handler(string) | the send keystroke, with the contents |

Two-way is `text={() => state.notes}` plus `onChange={(t) => state.notes = t}`. Without
the `onChange` it is read-only in effect; without the `text` binding it does not follow
anything else that writes the signal.

**Undo** is built in and needs nothing declared: **Ctrl+Z**, with **Ctrl+Y** or
**Ctrl+Shift+Z** to redo. A run of edits of one kind with no pause is a single step, and a
step restores the caret as well as the text. Per field, capped at 100 steps or 1 MB, and
nothing about it reaches the backend. A read-only field has no history.

`onSubmit` is what a search box or a chat composer wants. It carries the text, so the
handler need not read the signal back, and it does not clear the field -- clear it yourself
if finishing means that here.

**Which key sends.** Say nothing and it follows the mode: Enter on a `line` field, which
has nowhere to put a newline, and **Ctrl+Enter** on a `document` or `code` one. `submitKey`
says otherwise -- and `submitKey="enter"` on a multi-line field is the chat composer:
Enter sends, **Shift+Enter** makes a line. Shift+Enter is the line break wherever Enter
sends, whatever `submitKey` says. Sending does not move the keyboard on, so the next
message can be typed straight away; a `line` field still gives the keyboard back.

**Which grammar.** `code` takes its palette from the theme variant and its grammar from
`language` on the element, so one `code` variant serves every language an application
shows. A variant may still name one; the element's wins. A language that was never loaded
says so in the log rather than quietly drawing plain text.

**Completion.** `suggestion` is a bindable string drawn after the caret in the theme's
`suggestion` colour and never part of `text`. **Tab** takes it (an ordinary edit, so
`onChange` sees it) and fires `onAccept`; **Escape** drops it and fires `onDismiss`. The engine drops its own copy
so the ghost does not flicker, but a *bound* suggestion comes back from its signal --
clear that signal in the handler, which is what the handler is for. `onCaret(number)` reports where the caret is in
bytes whenever it moves. Both keys keep their old meanings when nothing is on offer. One
line only -- a suggestion with a newline is cut at it.

### `<checkbox>` — a labelled toggle

| Property | Type | Meaning |
| --- | --- | --- |
| `label` | string | the text beside the box |
| `value` | boolean | ticked when true |
| `variant` | string | theme variant (`checkbox`) |
| `onChange` | handler(boolean) | runs when toggled, with the new state |

### `<slider>` — a draggable number

| Property | Type | Meaning |
| --- | --- | --- |
| `value` | number | where the knob starts |
| `min` `max` | number | the ends |
| `variant` | string | theme variant (`slider`) |
| `onChange` | handler(number) | runs while dragged, with the new value |

### `<select>` and `<option>` — a box that opens a list of choices

| `<select>` | Type | Meaning |
| --- | --- | --- |
| `value` | string | the chosen option's value |
| `placeholder` | string | shown when the value matches no option |
| `variant` | string | theme variant (`button`) |
| `onChange` | handler(string) | runs when one is chosen, with its value |

| `<option>` | Type | Meaning |
| --- | --- | --- |
| `text` | string | what the row says |
| `value` | string | what choosing it sets |

`<option>` goes only inside `<select>`. A dropdown has no keyboard: no arrow keys, no
type-to-find, no escape.

## Data and pages

Choices may come from a table instead of `<option>` children, for a set the machine
finds rather than one the layout knows:

| Property | Type | Meaning |
| --- | --- | --- |
| `of` | string | a declared table to take the choices from |
| `textColumn` | string | which column a row shows; default `text` |
| `valueColumn` | string | which column a row means; default `value` |

With `of` set the `<option>` children are ignored. A hidden `<option>` is skipped rather
than drawn as a blank row.

### `<list>` — rows from a table

| Property | Type | Meaning |
| --- | --- | --- |
| `of` | string | which declared table the rows come from |
| `row` | `(item) => element` | the template one row is built from; called **once**, at build time |
| `rowHeight` | number | height of a row, and the fallback where the column below has no answer |
| `rowHeights` | string | a number column holding each row's own height, for rows that vary |
| `spacing` | number | gap between rows |
| `poolSize` | number | how many row widgets to keep; enough to fill the view |
| `barWidth` | number | thickness of the scroll bar |
| `wheelStep` | number | pixels per wheel notch |
| `follow` | boolean | stay at the end when rows arrive, while already there |
| `revealRow` | number | scroll until this row index is on screen; `-1` asks for nothing |
| `variant` | string | theme variant (`textfield` styles) |

`rowHeights` is how a transcript gets one row per message instead of one per wrapped line.
The **sender** fills the column -- `rda.measure_text(text, 0)` gives the width a string
draws at, and that over the row's width is the number of lines. The engine does not measure
the rows itself: that would mean binding every row on every table change, which is the cost
`<list>` exists to avoid. A height that is missing, zero or negative falls back to
`rowHeight`; a column that is not there, or is not a number column, warns and leaves the
list uniform. With no `rowHeights` the old path is untouched -- a multiply, no running
total, no search.

`revealRow` acts when the value **changes**, not while it holds, and scrolls the smallest
distance that brings the row into view -- a row already visible is left where it is. That is
what makes a keyboard selection walk one row at a time at the bottom edge instead of
jerking to the middle on every step.

Inside `row`, the parameter is the only way to read a cell: `item.price`. It is resolved
to a column index when the layout loads, so nothing is looked up by name while scrolling.
See `references/recipes.md`.

### `<tabs>` and `<tab>` — a row of titles and one page

| `<tabs>` | Type | Meaning |
| --- | --- | --- |
| `value` | number | which page is showing, from zero |
| `barHeight` | number | height of the row of titles |
| `variant` | string | theme variant (`button`) |
| `onChange` | handler(number) | runs when a title is clicked, with its index |

| `<tab>` | Type | Meaning |
| --- | --- | --- |
| `title` | string | what its tab says |

`<tab>` goes only inside `<tabs>`; its children are the page.

### `<image>` — a picture from a file, or from the backend

| Property | Type | Meaning |
| --- | --- | --- |
| `src` | string | a path relative to the working directory, an `.svg` to draw, or `mem:<name>` for a registered picture |
| `fit` | `contain` `stretch` | keep its shape, or fill the box |
| `tint` | string | a colour multiplied into the picture; white leaves it alone |

An `.svg` carrying **SMIL animates** with nothing said in the layout: `<animate>` on any
attribute, `<animateTransform>` (rotate, translate, scale), `<set>`, with `dur`, `begin`,
`repeatCount`, `values`/`keyTimes`, `from`/`to`/`by`, `calcMode` and `fill="freeze"`.
`stroke-dasharray`/`stroke-dashoffset` work, which is how an icon draws itself. The loop is
rasterised once into 30fps frames at the widget's size, so playing it costs what a still
icon costs; it keeps the window awake while it runs. No `<animateMotion>`, no CSS.

An `.svg` is **drawn at the size the layout gave the widget**, so one file is sharp at 16px
and at 96px and at whatever a 150% display makes of them. `tint` is a vertex colour, not a
second raster, so one white icon in six colours is one texture. `currentColor` and an
unstated `fill` come out **white** rather than the specification's black, because an icon
set means "the text colour" and black would be invisible on a dark theme. Paths (arcs
included), rects with `rx`, circles, ellipses, lines, polylines, polygons, groups,
transforms, both winding rules, strokes with caps and joins, per-shape opacity. No text,
gradients, patterns, filters, masks or CSS.

A picture a backend made needs no file: `rda.define_image(name, png_bytes)` (Node
`defineImage`, C# `DefineImage`) registers encoded bytes -- PNG, JPEG, BMP, TGA, GIF, PSD,
HDR, PNM -- and `define_image_pixels(name, rgba, w, h)` takes raw pixels, four bytes each,
rows packed. The layout then says `src="mem:<name>"`. The engine owns it, so two `<image>`s
naming one share a texture and registering the same name again replaces what both draw --
and it reaches the screen with no click and no signal write. `src` is bindable, so one
`<image>` can show a file now and a model's answer a moment later. `forget_image` drops
it; a name that is not registered draws nothing and says so once.

### `<popup>` — over everything, beside something, closes when unwanted

| Property | Type | Meaning |
| --- | --- | --- |
| `open` | boolean | showing or not; the layout owns it, the popup never writes it |
| `anchor` | string | the **full id path** of the widget to hang off (`root/toolbar/model`) |
| `placement` | `below` `above` `right` `left` `over` | which side of the anchor |
| `gap` | number | pixels between the anchor and this |
| `padding` | number | inset around its children |
| `blocking` | boolean | while open, nothing underneath hovers or clicks; default true |
| `onClose` | handler | a press outside it, or Escape |
| `variant` | string | theme variant (`panel`) |

A container, so what is inside is an ordinary layout -- a `<stack>` of buttons is a menu, a
`<list>` a picker, a `<label>` a tooltip. It is as big as what is inside plus `padding`, and
a child that names a fixed `width`/`height` is taken at its word rather than measured.

It **flips** to the opposite edge rather than sliding when it would fall off the one it is
growing towards, and is kept inside the window either way. With no `anchor` it is placed by
its own `x`/`y`.

`onClose` is where the layout closes itself: `open` is a binding, and a widget writing its
own bound property would be overwritten by the signal next frame.

While a blocking popup is open the interface behind it runs with **no pointer**, which is
what removes the transparent catch-all button every application otherwise builds. That also
settles the anchor: blocking, a press there cannot reach the button so it counts as outside
and closes the popup; not blocking, it does reach the button, so the popup ignores the
anchor and the button's own handler closes it.

### `<stream>` — a picture that keeps arriving

| Property | Type | Meaning |
| --- | --- | --- |
| `name` | string | which stream to show, by the name the backend pushes to |
| `fit` | `contain` `stretch` | keep the frame's shape, or fill the box |

`rda.push_frame(name, jpeg_bytes)` or `push_frame_pixels(name, rgba, w, h)` (Node
`pushFrame` / `pushFramePixels`, C# `PushFrame` / `PushFramePixels`). The difference from
`<image src="mem:...">` is the second frame: registering an image **builds** a texture,
pushing a frame **writes into** the one already there, and at thirty a second that is the
difference between a feed and a stall. Use `<image>` for a still, `<stream>` for a feed.

`rda.stream_wanted(name)` says whether anybody is looking -- ask before decoding the next
frame. True for a name nothing has drawn yet, so a producer is not stopped before it
starts; false, with a line in the log, once frames are arriving and no `<stream>` shows
them. `rda.stream_counts(name)` gives (pushed, shown); the gap is what is being wasted.

**Filters.** `rda.define_effect(name, glsl)` compiles a compute shader; `apply_effect(name,
source, into)` runs it into another stream. You write the filter only: `src` is the first
input, `tap(i, at)` the i'th, `store(c)` the output, and `uv()`, `coord()`, `size()`,
`param(0..7)` are provided. `source` may be a list of up to four -- a blend, a mask, a
difference -- and the output is the size of the first, the rest sampled in 0..1. Colours
inside an effect are linear light. The destination is sized for you and may not be one of
the sources. `rda.read_frame(name)` copies a frame back as (pixels, width, height), RGBA8
and as it looks on screen. See `docs/backend/effects.md`.

### `<dockspace>` and `<dock>` — movable, dockable panels

| `<dockspace>` | Type | Meaning |
| --- | --- | --- |
| `arrange` | `panes` `tiles` | how the space is divided; `panes` is the default |
| `persist` | string | file to remember the arrangement in; omitted, panels open where the layout says every time |
| `columns` | number | tiles: how many columns the grid has (default 12) |
| `rowHeight` | number | tiles: how tall one row is, in pixels (default 60) |
| `gap` | number | tiles: pixels between tiles, and around them |

| `<dock>` | Type | Meaning |
| --- | --- | --- |
| `title` | string | what its tab and title bar say |
| `closable` | boolean | give it a close button |
| `variant` | string | theme variant (`dock`) |
| `side` | `floating` `left` `right` `top` `bottom` `center` | panes: where it starts |
| `size` | number | panes: how wide or tall its pane starts |
| `col` `row` | number | tiles: which cell it starts in; unset means wherever it fits |
| `cols` `rows` | number | tiles: how many cells wide and tall (default 3 x 3) |

`<dock>` goes only inside `<dockspace>`.

**`panes`** is the editor: the area is cut into nested splits, panels sharing a pane
become tabs, splitters move the boundaries, and a drop has four meanings depending on
which part of a pane it lands on. Floating panels are drawn and take input in the order
they were created: clicking one does not bring it to the front.

**`tiles`** is the dashboard: each panel is a rectangle of whole cells in a column grid,
with room between them. Dragging one pushes what it lands on downward; letting go,
everything rises into the space above it. The bottom-right corner resizes, in cells.
There are no tabs and no splitters -- a tile is a rectangle, not a share of its
neighbour. Positions are in cells, so an arrangement is the same at any window size.

Two rules everything else follows from: tiles are only ever pulled *up* into free space,
never sideways, so an empty half-row stays empty; and whatever is under the pointer keeps
its row while everything else closes up around it.

```tsx
<dockspace id="board" arrange="tiles" columns={12} rowHeight={54} gap={10}>
  <dock id="sales" title="Sales" col={0} row={0} cols={6} rows={3}> ... </dock>
  <dock id="queue" title="Queue" cols={4} rows={3}> ... </dock>
</dockspace>
```

### `<viewport>` — a surface the application draws into itself

| Property | Type | Meaning |
| --- | --- | --- |
| `name` | string | how the backend addresses this surface; default `main` |
| `visible` | boolean | draws it; hidden it takes no space of its own |

A hole in the interface. What fills it: the engine's 3D scene if nobody claims it; a C++
program recording Vulkan (`RDA::viewports().onDraw(name, ...)`, which needs
`ViewportMode::Widget`); or a list of 2D commands from any other language
(`rda.draw(name, drawing)`), which needs no config at all. One viewport at a time can hold
the Vulkan target; any number can be 2D.

## The keyboard

Nothing to declare. Every widget that takes a click takes the keyboard, and Tab walks them
in the order they were drawn.

| | |
| --- | --- |
| Tab / Shift+Tab | the next / previous one |
| Space or Enter | presses a `<button>`, toggles a `<checkbox>` |
| Enter / Ctrl+Enter | sends from a `<textfield>` -- see `submitKey` |
| arrows | move a `<slider>`; move the highlight in an open `<select>` |
| Enter | opens a `<select>`, and takes the highlighted row |
| Escape | closes a `<select>`; leaves a `<textfield>` |

A keyboard press runs the same handler a click runs: `onClick` fires either way, and there
is no separate keyboard event to write. The ring around the focused widget is themed once,
as the `focus` export. See `references/theme.md`.

## Components

A function that returns one element. Called while the layout is compiled, so it leaves no
trace in what ships.

```tsx
function Field({ id, label }: { id: string, label: string }) {
  return (
    <stack id={id} arrange="horizontal" height="content" spacing={6} vAlign="center">
      <label id="l" width={90} height="content" text={label} />
      <textfield id="v" width="fill" height="content" />
    </stack>
  )
}

export default function Home() {
  return (
    <stack id="root" arrange="vertical" spacing={6} padding={12}>
      <Field id="name" label="Name" />
      <Field id="city" label="City" />
    </stack>
  )
}
```

An `id` at the call site wins over the one the component gave its own root, which is what
makes two calls two widgets rather than one name twice. A component may live in another
file and be imported; the compiler bundles it.
