// A mesh, for the one test that goes through the whole toolchain.
//
// MeshTests.cpp covers the reader by handing it bytes; this covers everything in front of
// that -- esbuild, the geometry prelude, QuickJS, the writer, and the reader again, since
// `rda mesh` parses back what it wrote. It is not a shape anybody wants. It is every part
// of the prelude a real mesh uses, in one file, so that a change to any of them fails a
// build rather than a project.
//
// The three states share a topology, which is the invariant the whole design rests on:
// 21 vertices from the rrect and 5 from the strip, in every one of them. The corner radii
// differ, and a square corner still emits its arc points -- on top of each other, drawing
// nothing -- which is exactly why that is allowed.
export default mesh({
  params: {
    body: colour("#2A2E38", "the badge itself"),
    edge: colour("#3A6AD0", "the lit strip along its bottom"),
    lift: number(2, "how far the chosen one reaches past its own rect"),
  },

  states: {
    // At rest: rounded, in its own colour, with an unlit strip the widget colours.
    normal: union(
      rrect(inset(1), 6, { fill: "body" }),
      group(rect({ left: [0, 2], right: [1, -2], top: [1, -3], bottom: [1, -1] }), "strip"),
    ),

    // Under the pointer: the strip lights up and thickens. Same vertices, further apart.
    hover: union(
      rrect(inset(1), { tl: 6, tr: 6, br: 4, bl: 4 }, { fill: "body" }),
      group(rect({ left: [0, 2], right: [1, -2], top: [1, -5], bottom: [1, -1] },
                 { fill: "edge" }), "strip"),
    ),

    // Held: every corner square. This state is the regression guard -- `rrect` used to
    // hand an all-square radius to `rect()`, which is the same shape with four triangles
    // instead of twenty, so this state and the two above it stopped sharing a topology and
    // the mesh was refused. A radius of 0 in one state exists *because* another state is
    // rounded, which is the one case that shortcut broke.
    pressed: union(
      rrect(inset(1), 0, { fill: "body" }),
      group(rect({ left: [0, 2], right: [1, -2], top: [1, -4], bottom: [1, -1] },
                 { fill: "edge" }), "strip"),
    ),

    // Chosen: the bottom corners square off and the shape reaches two pixels below its
    // own rect, so it merges into whatever is drawn under it.
    selected: union(
      rrect({ left: [0, 1], right: [1, -1], top: [0, 1], bottom: [1, 2] },
            { tl: 6, tr: 6, br: 0, bl: 0 }, { fill: "edge" }),
      group(rect({ left: [0, 2], right: [1, -2], top: [1, -1], bottom: [1, 2] },
                 { fill: "edge" }), "strip"),
    ),
  },

  regions: [
    hit("body"),
    text("label", { left: [0, 10], right: [1, -10] }),
    clip("inner", inset(4)),
  ],
})
