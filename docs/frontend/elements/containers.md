# Containers

## `<container>`
Groups children and draws nothing. Placement is absolute; use it when you want the
grouping without the background.

## `<panel>`
A framed background that clips its children. Placement inside it is absolute — put an
`anchor="fill"` stack in it to lay things out.

| | |
| --- | --- |
| `variant` | which panel style from the theme |

## `<stack>`
The workhorse. Lays children in a row or a column.

| | |
| --- | --- |
| `arrange` | `"vertical"` (default) or `"horizontal"` |
| `spacing` | gap between children |
| `padding` | inset around the whole row or column |
| `hAlign` | horizontally: `stretch` (default) / `start` / `center` / `end` |
| `vAlign` | vertically: the same four |
| `spread` | spreads the children along the stacking axis: `spaceBetween` / `spaceAround` |

Nothing in a stack carries a hand-computed position, which is what makes "insert a cell
between two others" a one-line change: inserting shifts the rest automatically.

## `<scroll>`

A clipped window onto content bigger than itself. It stacks its children in a column, the
way a `<stack arrange="vertical">` does, and scrolls to reach what does not fit. One child
is the ordinary case of that, and is how a single thing too wide for its place is shown.

| | |
| --- | --- |
| `vertical` | scrolls down when its content is taller. **On by default** |
| `horizontal` | scrolls sideways when its content is wider |
| `spacing` | gap between stacked children |
| `padding` | inset around the content |
| `hAlign` | where children sit across the column |
| `barWidth`, `wheelStep` | the bars, and pixels per wheel notch |
| `variant` | a **textfield** variant — that is where the scroll colours live |

Children scrolled out of sight are skipped rather than painted, so a long column costs
what is on screen. The wheel scrolls down; held with shift, or in a view that only
scrolls sideways, it scrolls across.

For rows from a table use [`<list>`](list.md) instead: it keeps about as many widgets as
fit and re-binds them, so ten thousand rows cost thirty widgets rather than ten thousand.

## `<splitter>`
A draggable bar that resizes what is above or beside it.

| | |
| --- | --- |
| `arrange` | which way it splits |
| `value` | the size it controls |
| `min`, `max` | how far it drags |

## `<popup>`

A panel over everything, beside the widget it names, that closes when the pointer goes
somewhere else. Every dropdown, menu, tooltip, autocomplete list and dialog wants the same
three things, and without this each application rebuilds them out of a `<container>`, a
transparent full-area `<button>` and arithmetic against the window's width.

| | |
| --- | --- |
| `open` | showing or not. The **layout** owns it; the popup never writes it |
| `anchor` | the full id path of the widget to hang off — `"root/toolbar/model"`, not `"model"` |
| `placement` | `"below"` (default), `"above"`, `"right"`, `"left"`, `"over"` |
| `gap` | pixels between the anchor and the popup |
| `padding` | inset around its children |
| `blocking` | while open, nothing underneath hovers or clicks. Default true |
| `onClose` | a press outside it, or **Escape** |
| `variant` | which panel style from the theme |

```tsx
<button id="model" text="Model" onClick={() => { state.menuOpen = !state.menuOpen }} />

<popup id="menu" anchor="root/model" placement="below"
       open={() => state.menuOpen}
       onClose={() => { state.menuOpen = false }}>
  <stack id="items" arrange="vertical" spacing={2} width={180} height="content">
    <button id="a" variant="menu" height={24} text="llama3"
            onClick={() => { state.model = "llama3"; state.menuOpen = false }} />
  </stack>
</popup>
```

It is a container, so what is inside is an ordinary layout: a `<stack>` of buttons is a
menu, a `<list>` is a picker, a `<label>` is a tooltip. The element is only the surface,
where it goes, and when it goes away.

**It never writes `open`.** `open` is a binding, and a widget that wrote its own bound
property would be overwritten by the signal on the next frame and flicker. `onClose` is
where the layout closes itself — the same shape `onChange` has everywhere else.

**Size.** As big as what is inside it, plus `padding`. A child that names a fixed `width`
or `height` is taken at its word; anything else is measured. A `width`/`height` on the
popup itself wins over both.

**Where it goes.** Against the named edge of the anchor, and **flipped** to the opposite
edge when it would fall off the one it is growing towards — a menu near the bottom of the
window opens upwards, which is what a reader expects and what sliding it up over its own
button is not. Either way it is kept inside the window. With no `anchor` it is placed by
its own `x`/`y`, like anything else in a `<container>`.

**Who gets the pointer.** While a blocking popup is open, the whole interface behind it
runs with no pointer: nothing hovers, nothing highlights, nothing fires. That is what
makes an open menu behave like an open menu, and it is why the transparent catch-all
button is not needed.

It also decides what a press on the anchor means:

- **Blocking** — the anchor's own `onClick` cannot fire, so a press there is an ordinary
  outside press and closes the popup. Pressing a menu's button a second time shuts it.
- **Not blocking** — that press *does* reach the button, so the popup leaves the anchor
  alone and the button's own handler is what closes it. Otherwise the two would cancel and
  the click would shut and reopen it.

Turn `blocking` off for a tooltip, a hint, anything that floats without taking over.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
