# Units and pixels

`<button height={30}>` is thirty units tall. On an ordinary display that is thirty pixels.
On one configured at 150% it is forty-five, and it still *looks* thirty — which is the whole
of what a display scale is for, and the point most interface code gets wrong by one layer.

The rule is one sentence: **everything an application says is in layout units, and pixels
exist only inside the renderer.** A window's size, a padding, a font size, a mesh's corner
radius, `state.rda.width`, the rectangle a `<viewport>` occupies — units. The draw list the
GUI produces — units. The framebuffer, the scissor, the font atlas — pixels.

## Where the conversion happens

One number carries it: physical pixels per layout unit, read from the display when the
window opens (`glfwGetWindowContentScale`), or set by hand with `GuiConfig::uiScale`.

| | |
| --- | --- |
| the window's size | `GLFW_SCALE_TO_MONITOR`, so a window asked for at 1280 units opens 1600 pixels wide at 125% |
| every vertex | one push constant in the GUI pipeline — `2 * k / framebufferWidth` instead of `2 / framebufferWidth` |
| every clip rectangle | multiplied where the scissor is set, because a scissor is pixels |
| the pointer | divided once, where input is gathered, so hit testing happens in units |
| the viewport a layout sees | the framebuffer divided, so a layout gets the room it was written against |
| text | **not** scaled — see below |
| an `.svg` | rasterised at `box * k` and drawn into a box of `box`, so an icon has the pixels the display can show |
| a `<viewport>`'s offscreen target | sized in pixels, or the one 3D view on the screen would be the one soft thing on it |

Measured on a 125% display: a window asked for at 1280x800 opens as 1600x1000 pixels, and
the interface inside it is handed 1280x800 units.

## Why text is baked rather than scaled

A glyph is a picture. A picture drawn at one size and shown at another is soft, and text is
most of what an interface draws — so the one thing that must not go through the matrix above
is the one thing that would suffer most from it.

So the atlas bakes at the scaled size while reporting metrics in units. A theme asking for
15-unit text on a 125% display gets a 19-pixel bitmap, an advance width of
`19px / 1.25 = 15.2 units`, and a quad 15.2 units wide. The GUI lays out in units and never
learns what a pixel is; the glyph it draws has a quarter more pixels than it would have had.

Two details that only matter because of this:

- **A glyph is snapped to a whole physical pixel**, not to a whole unit. Rounding a position
  to the unit grid at 125% puts the bitmap on a quarter-pixel boundary, which is sampled
  between texels and comes out blurred — undoing the reason for baking it larger.
- **The sizes in the log are the baked ones.** `Font atlas baked: ... at 23, 15, 19, 28, 35,
  45px (125% of the sizes asked for)` is a theme asking for 18, 12, 15, 22, 28 and 36.

## What this costs

Nothing per frame: the scale is a constant in a push constant that was already there, and
the font conversion happens once at bake time. The only run-time cost is that an `.svg` and
a `<viewport>` target are rasterised larger, which is the cost of them being sharp.

## The one thing it does not do

**A window dragged to a monitor at a different scale keeps the scale it opened with.** The
atlas is baked at a scale, and re-baking it mid-run would mean rebuilding the texture and
the descriptor set that every frame in flight is reading from. So the interface stays
correct and comes out slightly the wrong size, rather than correct and blurry, and the log
says so once. Reopening the window on that monitor gets it right.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
