# The frontend language

Everything a `.tsx` layout can say, and what each thing was meant for.

A layout is TypeScript with JSX in it, and it is **not shipped**. The build runs esbuild
over it, evaluates it once, and writes a `.rdab` — a flat array of nodes plus compiled
bytecode for every expression. What the application loads is that file and a loader, and
nothing in it is parsed: anything the language lets you write has to survive being turned
into data at build time. A function you define is no exception -- it is compiled to QuickJS
bytecode then, and runs only because a binding calls it.

Every element and property is declared once, in
`RendeerAlpha/src/Layout/WidgetSchema.cpp`, and the `rda.d.ts` in your project is
generated from that same table. If something is in your editor's autocomplete it exists at
run time, and if it is not, it does not.

## The language

| | |
| --- | --- |
| [The three kinds of value](values.md) | constants, bindings, handlers — and why the arrow is not a closure |
| [What an expression may contain](expressions.md) | the grammar, in full, and the two rules it enforces |
| [The four names a layout starts with](names.md) | `state`, `signal()`, `commands`, `row` |
| [Components and imports](components.md) | functions expanded at build time; splitting a layout across files |
| [Functions: code that runs](functions.md) | what a binding may call, what runs when, and what it costs |

## Layout

| | |
| --- | --- |
| [Sizing](sizing.md) | `width`, `height`, `content`, `fill`, and what saying nothing means |
| [Alignment](alignment.md) | `hAlign` and `vAlign` everywhere, `spread` for spreading, `hAlignSelf` / `vAlignSelf` on a child |
| [Absolute placement](placement.md) | `x`, `y`, `w`, `h`, `anchor` — for the times a stack is the wrong tool |
| [Motion](motion.md) | `animate`, inherited; `visible`, and what collapses |
| [Screen readers](accessibility.md) | what a screen reader is told, `alt` for what has no words, and seeing the tree |
| [Dragging and dropping](drag-and-drop.md) | `drag` and `onDrop` on anything; files from outside with `onFiles` |
| [The keyboard](keyboard.md) | Tab, Space, the arrows, Escape — and the ring that shows where they go |
| [The window](window.md) | how it opens, its icon, a window with no frame, and `state.rda` |
| [Meshes](meshes.md) | a widget's shape as an asset: anchors, states, regions |

## The elements

[Every element](elements/README.md) — twenty-seven of them, in eight groups, plus
[the properties they all share](elements/common-properties.md).

## Beyond one layout

| | |
| --- | --- |
| [Screens](screens.md) | more than one layout, with `route` as ordinary state |
| [Themes and variants](themes.md) | `variant="..."`, and the TypeScript a theme is written in |
| [What the language deliberately does not have](limitations.md) | worth reading before you go looking |

---

Back to [all documentation](../README.md)
