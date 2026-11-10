## The three kinds of value

```tsx
<label id="t"
       text="Ready"                                    // 1. a constant
       visible={state.count > 0}                       // 2. a binding
/>

<button id="b" text="Add"
        onClick={() => state.count++}                  // 3. a handler
/>
```

- **A constant** is written into the blueprint and never looked at again.
- **A binding** — any value that reads `state` — is parsed at build time into stack operations over signals, and evaluated when one of the signals it reads changes. That is the whole of the reactivity. You may write it with an arrow, `{() => state.count > 0}`, and it means the same; the arrow is added for you when it is not there.
- **A handler** — `onClick`, `onChange` — is the same machinery pointed the other way. It may write signals and call commands, and it runs when the widget says so. A handler always keeps its arrow, because that is what says it runs later.

The arrow is **not a closure**. It is a syntax the compiler recognises so it knows *this expression is to be evaluated later*, and it is compiled from its source text. Nothing from the code around it comes along. What it can name is `state`, `commands`, `tables`, and the top level of its file and its imports:

```tsx
const STEP = 5                     // the file's top level

function Counter() {
  const step = 5                   // a local of the component
  return (
    <stack id="counter">
      {/* error: `step` is not in scope */}
      <button id="a" onClick={() => state.count += step} />
      {/* fine: STEP ships with it */}
      <button id="b" onClick={() => state.count += STEP} />
      {/* fine */}
      <button id="c" onClick={() => state.count += 5} />
    </stack>
  )
}
```

An event that carries a value hands it to the handler, and a two-way widget takes both halves explicitly — `value` says where the truth is, `onChange` says what to do about a change:

```tsx
<checkbox  value={() => state.on}    onChange={(now) => state.on = now} />
<slider    value={() => state.gain}  onChange={(v) => state.gain = v} />
<textfield text={() => state.notes}  onChange={(typed) => state.notes = typed} />
<select    value={() => state.size}  onChange={(chosen) => state.size = chosen} />
<tabs      value={() => state.page}  onChange={(index) => state.page = index} />
```

## What an expression may contain

|  |  |
| --- | --- |
| **Literals** | numbers, `"strings"`, template literals, `true`, `false` |
| **Signals** | `state.name` — read, and in a handler, written |
| **The engine’s own** | `state.rda.width`, `state.rda.height` — read only |
| **Row fields** | `item.column` — only inside a `<list>` row template |
| **Event value** | the handler’s parameter, wherever it is named |
| **Arithmetic** | `+` `-` `*` `/`, unary `-` |
| **Comparison** | `===` `!==` `<` `<=` `>` `>=` |
| **Logic** | `&&` `||` `!` |
| **Conditional** | `cond ? a : b` |
| **Assignment** | `=` `+=` `-=` `*=` `/=` `++` `--` — handlers only |
| **Commands** | `commands.name()` — handlers only |
| **Sequence** | `;` between statements in a handler |

That is the grammar, and a binding written in it runs with no engine behind it. A binding that goes past it — a function you wrote, a method, an array, a loop — is compiled to JavaScript instead and runs as QuickJS bytecode; see [functions](../frontend/functions.md).

Two rules the grammar **enforces** rather than hopes for: a value binding may not call a command — drawing must not have effects — and a handler may write while a binding may not. What reaches outside the interface — a file, the network, a library — belongs in the backend, which is the half of the application that has a language.

## state.rda — what the engine knows about itself

The one place a binding reads two levels deep. `state.rda.width` and `state.rda.height` are the window’s size in layout units, rewritten every frame — so a layout follows a resize without the application being involved:

```tsx
<stack id="root" arrange={() => state.rda.width < 700 ? "vertical" : "horizontal"}>
```

They are read-only, and they cost nothing when nothing moves: a write that changes no value notifies nobody, so a still window is two comparisons a frame and wakes no binding.

## Sizing

| width / height | means |
| --- | --- |
| `200` | exactly 200 units |
| `"content"` | as big as the widget says it needs |
| `"fill"` | share of what is left over |
| `"fill:2"` | twice the share of a plain `fill` beside it |
| `"content-23"`, `"fill-40"`, `"content+8"` | the same, less or plus a number of units |

```tsx
<stack arrange="horizontal" spacing={8}>
  {/* fixed */}
  <panel width={200} />
  {/* the rest */}
  <panel width="fill" />
  {/* as wide as its text */}
  <label width="content" />
</stack>
```

A **unit** is a pixel on an ordinary display and one and a half pixels on one configured at 150%, so `height={30}` looks the same size on every display. Everything a layout says is in units; pixels exist only inside the renderer.

**Saying nothing is not the same as saying zero.** Along the stacking axis, a widget with no declared size is measured — so a row of labels that declared no width is a row of labels, not a pile of them at the same x. Across the axis, saying nothing means *stretch me to the line*, which is what a child of a column almost always wants.

`"content"` and `"fill"` are not values: one is a request to measure, the other a claim on room nobody has divided yet. Both are answered during layout, long after every binding has run — which is why the offset rides along with the word instead of being arithmetic, and why the offset itself may still be computed.

## The twenty-seven elements

| Group | Elements |
| --- | --- |
| **Containers** | `<container>`, `<panel>`, `<stack>`, `<scroll>`, `<splitter>`, `<popup>` |
| **Text and input** | `<label>`, `<button>`, `<checkbox>`, `<slider>`, `<textfield>`, `<number>` |
| **Keys** | `<shortcut>` — a key chord and what it does; a `<button>` or `<checkbox>` takes `shortcut="Ctrl+S"` itself |
| **Choices and pages** | `<select>` and `<option>`, `<tabs>` and `<tab>`, `<image>` |
| **Large data** | `<list>` — rows from a declared table, through a pool of widgets; `<tree>` — the same rows nested by a depth column; `<grid>` and `<column>` — a table as columns under titles, sorted by clicking one |
| **Live pictures** | `<stream>` — a picture the backend replaces, frame after frame |
| **Docking** | `<dockspace>`, `<dock>`, `<header>` |
| **The viewport** | `<viewport>` — a hole the application fills, with Vulkan or with 2D commands |

Each takes its own properties plus the common ones: `id`, `visible`, `animate`, `width` / `height` and their bounds, `hAlign` / `vAlign` and the per-child `hAlignSelf` / `vAlignSelf`, and absolute placement with `x`, `y`, `w`, `h`, `anchor` for the times a stack is the wrong tool.

Every `text` property is UTF-8, from the `.tsx` through the blueprint to the screen, and so is everything a `<textfield>` receives from the keyboard. The font atlas is baked with Latin — including the Romanian comma-below letters — and anything outside it is baked the first time it is drawn, from the first face in `config.font` that has it, or failing that from the system's own. A codepoint no face has draws as a hollow box, which is a thing you can see, rather than as blank space.

## Beyond one layout

- **Screens** — more than one layout, with `route` as ordinary state. Declaring `routes` in `state.ts` adds a `route` signal and `back` / `forward` as commands for free.
- **Opening and closing** — `onOpen` and `onClose` on a layout’s root element run when it opens and closes, the application exiting included, and not when a saved edit reloads it.
- **Without JSX** — a layout may return a plain object instead, `{ type: "label", text: "Hello", children: [...] }`, typed as `Layout`. It compiles to the same thing. See [a layout as an object](../frontend/objects.md).
- **Components** — plain functions expanded at build time, which is how a layout splits across files without anything expanding at run time.
- **Themes and variants** — `variant="..."` on a widget picks an entry from the theme; everything left out inherits from `default`.
- **Motion** — `animate` is inherited down the tree, and `visible` collapses a child of a stack rather than leaving a hole.
