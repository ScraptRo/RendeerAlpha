# Where things stand

**Live and load-bearing.** The blueprint format and its compiler; the widget and theme
schemas and the types generated from them; components, expanded at compile time; signals,
bindings and compiled event handlers; routing, with history and restored params; the theme
compiler, including the type scale and transitions; the twenty-seven elements, the overlay
layer and `<popup>` among them; the headless probe; hot reload -- of a layout, and of the
theme and meshes it is drawn with; the display scale, so a unit is a unit on a display at
150%; the CMake build with per-platform scripts.

And the seam out of C++: the C ABI, the Python, Node and C# packages written on it, and
the generators and build rules that give each of them the same declaration the C++ and
the TypeScript come from. Four languages have now driven it, which is a different thing
from one language having designed it.

**Compiled but optional.**

- *The 3D scene path* — `Scene`, `Mesh`, `Material`, `Primitives`, `ForwardPass`,
  `ShadowPass`, `SceneFrame`, `MaterialResources`. Still built into the library. The
  direction of travel is for this to leave the engine and live in whatever application
  wants a 3D view, which is what it was always for — and the
  [`<viewport>`](../backend/viewport.md) is now the door it would leave through: an
  application records its own Vulkan there, so a scene renderer outside the engine is a
  callback rather than a fork.
- *Run-time scripting* — `src/Scripting/`, behind `RDA_ENABLE_SCRIPTING`, **off by
  default**. An embedded QuickJS host with bindings for the 3D scene that predate the
  interface layer. Not to be confused with a layout's functions, which run on their own
  host -- `Layout/ScriptHost` -- and are on unless `RDA_RUNTIME_CODE` is off.

**A hole in the interface.** A `<viewport>` is the one place the application draws
rather than the engine. C++ gets the command buffer inside the render pass, with the
device, the allocator and the render pass to build against; every other language gets a
list of 2D commands -- rectangles, lines and text in the viewport's own coordinates --
which the engine draws with the same rasteriser the widgets use, so a colour there is the
same pixels as that colour in a theme. One offscreen target exists, so one viewport at a
time can be the Vulkan one; any number can be 2D. See
[drawing in a viewport](../backend/viewport.md).

**Still XML.** `RendeerAlpha/res/themes/languages.xml`, the syntax-highlighting
definitions. State moved to TypeScript; this is what is left, and it is engine data rather
than something an application writes. It is loaded on request rather than by default --
`GuiConfig::languagesPath` -- and a field naming a language that was never loaded now says
so instead of drawing plain text in silence.

**Named for the axis, since the vocabulary changed.** Alignment used to be relative to a
container's direction -- `align` across the stacking axis, `spread` along it -- which
stopped being defensible the moment `arrange` could be a binding: a stack that flips
between a row and a column would have had its alignment silently come to mean the other
axis. It is `hAlign` and `vAlign` now, on containers and on text alike, with `spread`
kept for the one thing that only has a meaning along an axis: spreading children apart.
A layout written against the old names is named in the log, property by property, with
what to write instead.

**What 1.0 is promising.** Two things, and they are the reason the number moved.

A colour is the colour you wrote: `#3A6AD0` in a theme comes off the screen as `#3A6AD0`,
for every colour in a theme, a layout, an `<image>` tint and a drawing sent over the C ABI.
This was not true before 1.0 and fixing it is the one change that alters how an existing
application looks — see [themes](../frontend/themes.md).

And the C ABI's `major.minor`: same major in both directions, engine minor at or ahead of
the package's. A package built against 1.0 keeps working against every 1.x engine after it.
That was designed before this release and is now a commitment rather than an intention.

**Screen readers, on Windows and Linux.** Every widget describes itself into a
platform-neutral tree, built only once something asks and published as a snapshot. A UI
Automation provider speaks it on Windows and an AT-SPI one, over a libdbus loaded at run
time, on Linux -- see [screen readers](../frontend/accessibility.md). macOS has the tree and
nothing to speak it, because the engine does not run there. A text field is read caret by
caret on both, through UI Automation's text pattern and AT-SPI's Text, and an open
dropdown's options are items a reader can move through and choose. A list's rows are all
in the tree, those out of view described from the table alone, up to two thousand around
the view.

**Chinese, Japanese and Korean.** Drawn from the system's own faces when the named ones do
not have them, loaded one at a time, the first time a character needs one, in the order the
system's language suggests. Typed through the input method on Windows, composed at the
caret with the method off everywhere but a text field. Linux has the faces and, under X11,
finished text through GLFW; it has no composition at the caret, and Wayland has no input
method at all until GLFW speaks its text input. The atlas holds about two thousand CJK
characters per size and does not grow.

**Known loose ends.** None of them needs something to change shape to be fixed, which is
what makes them loose ends rather than reasons to wait.

- A `<stack>` and a dock pane collapse a child that becomes invisible; a scroll view and
  a list still make it disappear at once, leaving the space it had.
- An animated `.svg` re-draws its whole loop whenever its box changes size. Safe, but
  not cheap: one 260px collapse of a panel beside it re-drew a thirty-frame loop ninety
  times, for about half the frame rate while it lasted. Deferring the re-draw until the
  size settles was tried and bought nothing — a moving edge holds each whole-pixel width
  for several frames, so "has it stopped" is true almost as often as not. Drawing at the
  size the picture is given rather than the size of the box around it is the lever that
  would work.
- A frameless window's frame is the platform's own on Windows (snapping, snap layouts on
  the button marked `maximizeButton`, native resize) and the engine's GLFW approximation
  elsewhere -- moved, resized from every edge, double-clicked to maximise and dragged out
  of maximised, but never snapped. A Linux equivalent would go through the compositor
  (`_NET_WM_MOVERESIZE` on X11, xdg-toplevel's move and resize on Wayland).
- A `tiles` dock space does not scroll. Rows past the bottom of the space are clipped.
  Compaction pulls everything up, so it takes genuinely more tiles than fit -- but a
  dashboard that grows past the window has nowhere to put the rest.
- A tile cannot be pulled out of the grid to float, and two tiles cannot be tabbed
  together. Both are `panes` ideas: a tile is a rectangle rather than a share of its
  neighbour, so neither has anything to mean in a grid.
- A window dragged onto a monitor at a different scale keeps the scale it opened with. The
  font atlas is baked at a scale, and re-baking it mid-run means rebuilding the texture and
  the descriptor set every frame in flight is reading from -- so the interface comes out
  slightly the wrong size rather than blurry, and the log says so once. Reopening the window
  there gets it right.
- A focus ring around a widget drawn by a mesh follows that mesh's shape, and around
  everything else it is still a rectangle, which is the right answer for a rectangle.
- A widget can be clicked while it is collapsing. Brief, and not wrong enough to be worth
  wrapping every collapsing child in `pushInert()` — which is what a route's outgoing
  screen does now.
- A text field scrolls its own contents instantly. The other three scrollers ease what they
  draw with; the field reads `scrollY` in a dozen places while it renders, and giving it a
  drawn value of its own is a bigger change than the jump is worth. Its *bar* animates
  like the rest.
- Scroll bars do not fade out when nothing is scrolling, the way a phone's do. The width is
  reserved either way, so it would cost nothing to draw — but a bar that is not there when
  you look for it is a different decision from an animation, and nobody has asked for it.
- Routing has no deep links or URLs, and history is not persisted -- it starts empty
  every run. A screen is named, not addressed, which is enough for an application
  and not enough for anything that wants to be linked to from outside.
- Text fields are drawn at the base size only. `fontSize` reaches labels, buttons and
  checkboxes; threading it through an editor's caret, selection, gutter and line
  arithmetic is its own job and has not been done.
- No real bold face — `weight` is synthesised, and says so. A second *family* is no longer
  the limit it was: `rda_config_set_font` takes several paths separated by `;`, the first
  is the body font and the rest are fallbacks, and after those the system's own faces are
  tried, one at a time, for anything none of them has. A codepoint no face on the machine
  has still draws as a box, visibly, and the log says which.
- The syntax highlighter tokenises in ASCII, so `café` in a code field is highlighted as
  the identifier `caf` followed by plain text. It renders correctly — a token boundary is
  always at an ASCII byte, so a run never splits a character — it is only coloured
  slightly wrong, and only when a non-ASCII identifier is also a keyword.
- Linux builds from a clone, and on Ubuntu 24.04 with GCC 13 its own tests pass, the
  Python binding test passes with a window on lavapipe, and an application built against
  it runs and exits cleanly under AddressSanitizer. The Node and C# binding tests have
  only been run on Windows so far.
- Public engine headers reach engine-internal ones, so a consumer needs `src/` on its
  include path. Recorded in `RendeerAlpha/CMakeLists.txt` rather than quietly worked
  around.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
