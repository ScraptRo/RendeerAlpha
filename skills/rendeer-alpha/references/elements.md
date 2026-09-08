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
| `hAlignSelf` `vAlignSelf` | `auto` `stretch` `start` `center` `end`, each with an optional offset | this child's answer to its container, per axis |
| `x` `y` `w` `h` | number | absolute placement inside the parent's content box |
| `marginRight` `marginBottom` | number | distance kept from that parent edge when anchored |
| `anchor` | `fill` `stretchX` `stretchY` `bottomLeft` `bottomRight` | which parent edges this follows |

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
| `hAlign` | `start` `center` `end` | across its box |
| `vAlign` | `start` `center` `end` | down its box |
| `variant` | string | theme variant (`label`) |

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
| `variant` | string | theme variant (`textfield`) |
| `onChange` | handler(string) | runs on each edit, with the new contents |

Two-way is `text={() => state.notes}` plus `onChange={(t) => state.notes = t}`. Without
the `onChange` it is read-only in effect; without the `text` binding it does not follow
anything else that writes the signal.

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

### `<list>` — rows from a table

| Property | Type | Meaning |
| --- | --- | --- |
| `of` | string | which declared table the rows come from |
| `row` | `(item) => element` | the template one row is built from; called **once**, at build time |
| `rowHeight` | number | height of one row; every row is the same |
| `spacing` | number | gap between rows |
| `poolSize` | number | how many row widgets to keep; enough to fill the view |
| `barWidth` | number | thickness of the scroll bar |
| `wheelStep` | number | pixels per wheel notch |
| `variant` | string | theme variant (`textfield` styles) |

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

### `<image>` — a picture from a file

| Property | Type | Meaning |
| --- | --- | --- |
| `src` | string | path, relative to the program's working directory |
| `fit` | `contain` `stretch` | keep its shape, or fill the box |

### `<dockspace>` and `<dock>` — movable, dockable panels

| `<dockspace>` | Type | Meaning |
| --- | --- | --- |
| `persist` | string | file to remember the arrangement in; omitted, panels open where the layout says every time |

| `<dock>` | Type | Meaning |
| --- | --- | --- |
| `title` | string | what its tab and title bar say |
| `side` | `floating` `left` `right` `top` `bottom` `center` | where it starts |
| `size` | number | how wide or tall its pane starts |
| `closable` | boolean | give it a close button |
| `variant` | string | theme variant (`dock`) |

`<dock>` goes only inside `<dockspace>`. Floating panels are drawn and take input in the
order they were created: clicking one does not bring it to the front.

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
