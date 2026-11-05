// For the rda_mesh_error_line test: a mesh the geometry helpers refuse. What they throw is
// theirs, and the frame under theirs has to be this file's line -- 11, the edge written as
// a word -- rather than a line of the IIFE esbuild makes of it.
export default mesh({
  params: {
    body: colour("#2A2E38", "the shape itself"),
  },

  states: {
    normal: union(
      rect({ left: "half" }, { fill: "body" }),
    ),
  },
})
