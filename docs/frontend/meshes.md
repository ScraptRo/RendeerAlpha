# Meshes

A widget's shape is a literal in a `.cpp`: a button is a rounded rect because
`Gui::button` draws one. Every colour of that is themeable and none of its *shape* is, so
a look that is not made of rectangles needs an engine change.

A mesh is that shape as an asset. You write it in TypeScript, `rda` compiles it, and a
theme variant names it — so a layout that already says `<button variant="tab">` gets the
new shape without a line changing.

```ts
// res/mesh/tab.ts
export default mesh({
  params: {
    seam: colour("#3A6AD0", "the tab, and the panel edge it merges into"),
    idle: colour("#2A2E38", "a tab nobody has chosen"),
  },
  states: {
    normal:   rrect({ left: [0, 1], right: [1, -1], top: [0, 2], bottom: [1, -2] },
                    5, { fill: "idle" }),
    selected: rrect({ left: [0, 1], right: [1, -1], top: [0, 0], bottom: [1, 2] },
                    { tl: 6, tr: 6, br: 0, bl: 0 }, { fill: "seam" }),
  },
  regions: [ hit("body"), text("label", { left: [0, 10], right: [1, -10] }) ],
})
```

```ts
// res/themes/app.ts
export const button: RdaButtonTheme = {
  tab: { mesh: "res/mesh/tab.rdam", text: "#E8EAF0", transitionMs: 160,
         "tab.seam": "#C8503C" },          // the mesh declared this; the theme sets it
}
```

That is the whole of it, and it produces a tab that opens downward into the panel beneath
it when the pointer arrives.

## Anchors: the one idea

The graph runs **when you build**, and a mesh can see nothing at run time except the
rectangle the layout gave the widget. So a vertex is not a position — it is a position
*expressed in terms of* that rectangle:

```
pos.x = rect.x + ax * rect.w + ox
pos.y = rect.y + ay * rect.h + oy
```

An anchor fraction and a pixel offset, per axis. Two multiply-adds when it is drawn.

**The anchor is the design decision.** An edge written `[0, 8]` is eight pixels in from
the left at any width; the same edge written `0.05` is a twentieth of the width and
stretches. Corners should almost always be pinned in pixels — a radius authored as a
fraction goes elliptical the moment the widget is wide.

Every edge of a box is `[anchor, pixels]`, and a bare number is an anchor with no offset:

| | |
| --- | --- |
| `{}` | the whole rect |
| `{ left: [0, 8] }` | eight pixels in from the left |
| `{ left: 0.5 }` | the middle |
| `inset(4)` | four pixels in on every side |

Offsets may fall outside `[0, 1]`. A mesh is allowed to draw past its own rect, and that
is what lets a tab reach into the panel under it — see *Merging* below.

## States

A mesh cannot see whether it is hovered, because it cannot see anything but the rect. So
hover is not a parameter — it is **another state**, and the engine blends between them.

States are a **ladder**, used in the order they are written, from least engaged to most:
the first is resting, the second is under the pointer, the third is held. The blend eases
over the variant's `transitionMs`, with the same curve everything else uses, so a shape
change is an animation and you wrote no interpolation.

**`selected` on the widget is the top of that ladder.** Hover and press are things
happening *to* a widget and the GUI works them out for itself; being the chosen one is
something only the application knows, so it is a property:

```tsx
<button id="m1" text="States" variant="tab"
        selected={() => state.sheet === 1} onClick={() => state.sheet = 1} />
```

A mesh with three states therefore draws a chosen widget the way it draws a held one,
which is usually what a tab wants. Write a fourth state and they differ, with a press
travelling through `held` on its way to it.

`<tabs>` does this for you: every title wears the bar's variant and the widget marks the
open one `selected`, so a mesh on that variant is a tab bar. Its bar is drawn after its
pages, which is what lets a title reach onto the page below it.

This is the difference between *the pointer is here* and *this is the one*, and without it
a tab strip has neither: every tab looks the same until you point at one, and the one you
chose closes again the moment you move away.

**Every state must share a topology.** They are the same vertices in different places —
which is exactly what makes them blendable, and is why a mesh cannot grow geometry at run
time. `rrect` emits the same vertices whatever the radius (a square corner emits its arc
points on top of each other, and the triangles between them draw nothing), so changing
only the corners between two states is fine. A state with a different number of triangles
is a different mesh, and the build says so.

## Shapes

| | |
| --- | --- |
| `rect(box, opts)` | an anchored rectangle |
| `rrect(box, radius, opts)` | rounded; radius in **pixels**, uniform or `{ tl, tr, br, bl }` |
| `union(a, b, …)` | one shape from several — the way to add geometry |
| `group(shape, "name")` | tag every vertex, so something can address this part later |
| `weld(shape, radius)` | join coincident vertices; they genuinely go away |

`opts.fill` names one of the mesh's parameters. Omit it and that geometry takes the colour
the widget would have used anyway, which is how a mesh with no parameters at all works.

## Regions

A mesh does not own input. It says **where input goes**, and the widget runs the same hit
test it always did:

| | |
| --- | --- |
| `hit(name, box)` | where the widget is pressable |
| `text(name, box)` | where a string is drawn |
| `clip(name, box)` | where a container's contents live |

Text stays out of the mesh deliberately: it is glyph quads from the atlas, re-laid-out
every frame, wrapped, UTF-8, with a fallback face. A text region says where the string
goes and the text system does the rest.

## Merging two widgets

The motivating case, and it needs nothing special:

- the selected state drops its bottom edge and reaches a pixel or two past its own rect
- the panel draws first, the tab after — children already paint after their container
- the tab's fill covers the panel's border where they overlap

No cross-widget knowledge. **But a mesh cannot draw outside its *container's* clip**: a
`<panel>` pushes one, so a tab can reach down into the panel below it only if both are
inside the same clip. That decides which direction a merge can go, and it is the first
thing to check when a flare does not appear.

## Building one

`rda build <project>` finds `res/mesh/*.ts` on its own, compiles each into a `.rdam`
beside its source, and does it **before** the themes -- so nothing below is needed for a
Python, Node or C# project.

A C++ project is built by CMake, which gives every part its own process, so the list has
to be stated:

```cmake
rda_add_meshes(myapp res/mesh/tab.ts)   # before rda_add_theme and rda_add_types
```

That rule compiles each mesh, and it also hands `--mesh <file>` to the theme and type
rules for you. Order matters and CMake will not tell you: declared *after* them, the
theme is checked against the engine's table alone and `"tab.seam"` is refused as an
unknown field -- which is the same message a genuine misspelling gets.

The same flag is there by hand, for a build that runs the tool itself:

```bash
rda theme res/themes/app.ts --mesh res/mesh/tab.ts
rda types res/layouts/rda.d.ts res/themes/app.ts res/state.ts --mesh res/mesh/tab.ts
```

## Parameters

A mesh declares what it wants from the theme, rather than the engine deciding in advance:

```ts
params: {
  seam:  colour("#3A6AD0", "where the tab meets the panel"),
  flare: number(6, "how far a selected tab spreads"),
}
```

A theme then sets them, namespaced by the mesh's own file name:

```ts
tab: { mesh: "res/mesh/tab.rdam", "tab.seam": "#C8503C", "tab.idle": "#402A26" },
```

**The theme schema is the engine's table merged with what your meshes ask for.** They are
checked like any other field and they reach the generated types, so `"tab.seem"` is an
error at build time with a suggestion, and your editor underlines it before that. A
parameter on an element no mesh can draw — a `<label>` — is refused too: the merge only
reaches elements that have a `mesh` field of their own.

Every parameter carries a default, so a theme that says nothing about one still draws, and
a mesh with no parameters at all is drawn in the colour the widget would have used anyway.

This is why meshes compile **before** themes: what a mesh asks for is part of what a theme
may carry, so a theme cannot be checked until every mesh has spoken. `rda build` recompiles
a theme whenever any mesh has changed, and it reads every mesh's declaration even when the
`.rdam` is already current — that is the one part of the build that cannot be skipped by a
timestamp, and it is also the cheapest.

Names resolve to indices the first time a style draws a mesh, and are kept. Nothing is
looked up by name per frame, which is the point of declaring them at all.

## What a mesh cannot do

- **Grow with content.** No mesh per list row. This is what buys state blending.
- **Text.** Above.
- **Reach past its container's clip.** Above.
- **Meet another widget at a computed seam.** Only overlap — a mesh sees one rectangle.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
