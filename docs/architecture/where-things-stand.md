# Where things stand

**Live and load-bearing.** The blueprint format and its compiler; the widget and theme
schemas and the types generated from them; components, expanded at compile time; signals,
bindings and compiled event handlers; routing, with history and restored params; the theme
compiler, including the type scale and transitions; the nineteen elements, popups
included; the headless probe; hot reload; the CMake build with per-platform scripts.

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
  default**. An embedded QuickJS host: a sandbox with no file, process or network access,
  and bindings for the 3D scene that predate the interface layer. It was compiled into
  every build and called by nothing. Off because *a shipped binary evaluates no
  JavaScript* is a promise this engine makes, and this is the switch that qualifies it.

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

**Known loose ends.**

- Only a `<stack>` collapses a child that becomes invisible. Elsewhere — inside a scroll
  view, a list, a dock panel — it still disappears at once.
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
- Floating panels are drawn, and take input, in the order they were created. Clicking one
  does not bring it to the front, so two that overlap keep whichever order they had.
- A dropdown has no keyboard: no arrow keys, no type-to-find, no escape to close. It is
  a pointer widget, and the focus model it would need is the same one the rest of the
  interface does not have yet.
- Routing has no deep links or URLs, and history is not persisted -- it starts empty
  every run. A screen is named, not addressed, which is enough for an application
  and not enough for anything that wants to be linked to from outside.
- Text fields are drawn at the base size only. `fontSize` reaches labels, buttons and
  checkboxes; threading it through an editor's caret, selection, gutter and line
  arithmetic is its own job and has not been done.
- One font face. No second family and no real bold face — `weight` is synthesised, and
  says so. Text itself is UTF-8 and covers Latin: see *A character is not a byte*. Greek,
  Cyrillic and CJK draw as a box, on purpose and visibly.
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
