# Changelog

Newest first.

Two numbers move independently and both are named in every entry. The **version** is the
engine's. The **C ABI** is what a backend in Python, Node, C# or anything else with an FFI
is built against: its major says whether a package built against an older engine still
runs, and a minor means functions were added while nothing already there changed meaning.
See [the C ABI](docs/backend/c-abi.md).

## 1.1.0 — 2026-10-01

C ABI **1.6, unchanged**. Nothing in this version crosses it: this is the layout language,
the theme, the toolchain and what gets drawn, all of which a backend reaches through calls
it already makes. A backend built against 1.0.0 runs against this untouched.

### Before upgrading

Two things that were already being drawn now look different.

- **A click no longer leaves a focus ring.** It is drawn only where the keyboard put the
  focus — Tab draws one, a click does not, and neither does `rda_set_focus`. A press is its
  own feedback, and on a widget that is *also* the chosen one a ring reads as a second kind
  of chosen-ness, which is wrong in the one place it matters most. `show: "always"` on the
  focus theme puts it back everywhere; `width: 0` still removes it entirely.
- **The engine prints warnings and errors only.** It used to print everything it had, which
  on start-up is dozens of lines of somebody else's business in your terminal.
  `config.logLevel = RDA::LogLevel::Info` asks for all of it back and `RDA_LOG=info` does
  it without a rebuild. A Debug build still writes every line into `RDA_DEBUG.txt` whatever
  the console is set to, so nothing is lost — only moved out of the console.

### Meshes: a widget's shape as an asset

The shape of every widget was a literal in a `.cpp` — a button is a rounded rect because
`Gui::button` draws one. Every colour of that was themeable and none of its *shape* was, so
a look not made of rectangles needed an engine change.

A mesh is that shape as an asset. You write it in TypeScript, `rda` compiles it, and a
theme variant names it — so a layout that already says `<button variant="tab">` gets the
new shape without a line changing:

```ts
tab: { mesh: "res/mesh/tab.rdam", text: "#E8EAF0", transitionMs: 160,
       "tab.seam": "#C8503C" },          // the mesh declared this; the theme sets it
```

- **Anchors are the one idea.** The graph runs when you build, and a mesh can see nothing
  at run time except the rectangle the layout gave the widget — so a vertex is not a
  position, it is a position expressed in terms of that rectangle:
  `pos.x = rect.x + ax * rect.w + ox`. Two multiply-adds when it is drawn, and nine-slice
  scaling generalised from nine rectangles to any mesh. Whether a feature stretches with the
  widget or keeps its size is the anchor, which is what makes the anchor the design decision.
- **States blend.** A mesh cannot see whether it is hovered, so hover is not a parameter —
  it is another state, and states are a ladder used in the order they are written: resting,
  under the pointer, held. The engine eases between them over the variant's `transitionMs`,
  so a shape change is an animation and you wrote no interpolation. Every state shares a
  topology, which is exactly what makes them blendable, and why a mesh cannot grow geometry.
- **Regions, rather than input.** `hit`, `text` and `clip` say *where* a press, a string and
  a container's contents go. The widget runs the same hit test it always did, and text stays
  with the text system, which already wraps, falls back and speaks UTF-8.
- **Parameters are declared, and the theme schema grows to fit.** A mesh says what it wants
  from the theme instead of the engine deciding in advance, and those become real fields on
  that element, namespaced by the mesh's file name. They are checked like every other
  field — `"tab.seem"` is refused with a suggestion, and so is a mesh parameter on a
  `<label>`, which no mesh can draw — and they reach the generated `rda.d.ts`, so an editor
  catches it first.
- `rda mesh <mesh.ts>` compiles one, and `rda build` finds `res/mesh/*.ts` by itself. Meshes
  compile **before** themes, because what a mesh declares is part of what a theme may carry.
  A C++ project states the list — `rda_add_meshes(myapp res/mesh/tab.ts)`, declared before
  `rda_add_theme` and `rda_add_types` — and that rule hands `--mesh` to both for you.

[Meshes](docs/frontend/meshes.md) is the whole of it, including what one deliberately
cannot do.

### Chosen, as against pointed at

- **`selected` on `<button>`.** Hover and press are things happening *to* a widget and the
  GUI works them out for itself. Being the chosen one — a tab, a segmented control, the tool
  that is currently the tool — is something only the application knows, so it is a property
  and a binding the button never writes. It draws in the variant's `pressed` colour and
  keeps it while the pointer is elsewhere, ahead of hover on purpose: a chosen tab that went
  back to looking unchosen because the pointer arrived on it would be saying the wrong thing
  in the one place it matters most. Drawn with a mesh, it is the top rung of that ladder.
- **`variant` on `<tabs>`.** Every title wears it and the widget marks the open one
  `selected`, so one variant describes a whole bar: its `normal` is a closed title, its
  `pressed` the open one. The bar is drawn **after** its pages now, which lets a title reach
  a pixel or two past its own rectangle onto the page below — how a mesh-drawn title merges
  into its page and the seam between them disappears. One consequence: a click lands after
  the pages have painted, so the page it chose begins arriving on the next frame.

### New elements

- **`<number>`** — a number you drag sideways to change and click to type into. `label`,
  `value`, `step`, `min`, `max`, `decimals`, and `onChange` while it is dragged as well as
  when a typed value is committed. Shift is the fine adjustment. Clicking turns it into a
  real text field, with selection, the caret and undo, because those already exist and a
  second half of them would be worse at all of it. Neither widget that was already here
  fits: a `<slider>` needs a range and spends its width on a track, and a `<textfield>`
  makes you select, type and commit to nudge a value by one.
- **`<header>`, inside a `<dock>`** — a panel's title bar as a widget tree instead of a
  filled strip with an `x` on the end. Dragging the band still picks the panel up, and a
  button on the bar still wins its own press: the band reads which widget the pointer went
  down on before its contents paint and again after, so a close button closes rather than
  drags. It is drawn wherever the panel is the only one in its pane — floating and tiles
  included — and not when it is one tab among several, because a strip belongs to a pane and
  a header belongs to a panel, and two panels in one group cannot both dress it.

### Right-click menus

`onContext` is on every element and fires when the right button goes down on it; the deepest
widget that asked for it gets it. It carries nothing, because the thing that wants the
position is the menu, and `<popup anchor="pointer">` already knows where the pointer was
when it opened. The middle button is readable too (`GuiInput::mDown` and friends), with no
event of its own yet.

Mouse state is per button now — `lPressed`, `rPressed`, `mPressed` and the rest — rather
than one set of flags for whichever button it happened to be.

### Docking

- **A drag shows you where it would land.** The four window edges, banded, with the one
  under the pointer lit, so docking across a whole side is discoverable at all. The pane
  under the pointer, tinted. And the panel itself as a figure: the rectangle it would take,
  outlined, wearing a bar with its name on it. Over a pane's edge that figure is the half
  the panel would take; in a pane's middle, where the drop joins that pane's tab group, it
  is the **tab it would become**, drawn at the end of that pane's strip. It fades in with
  the drag rather than blinking on the frame a panel is picked up.
- **The corner of a pane splits and joins it.** Dragging the grip in a pane's bottom-right
  inward cuts the pane in two and moves the panel showing into the new half; dragging it out
  over a neighbour makes the two one. A split *moves* the panel rather than copying it,
  which is where this parts company with the gesture it is borrowed from — so a pane holding
  one panel has nothing to split off, and the grip is not drawn when there is neither
  anything to split nor anyone to join.
- **`visible` on a `<dock>` closes the space up.** The pane's sibling takes the whole split,
  the splitter between them goes, and showing the panel again puts it back where it was at
  the size it was. That is what a close button you drew yourself should write; the engine's
  `x` is different, and removes the panel from the tree for good.
- Animation keys are per dock space, so two spaces in one window no longer drive each
  other's guides.

### Seeing a layout with no backend behind it

```bash
rda prototype .
```

A window, with your interface in it, and nothing behind it. It compiles the project,
defines every signal at the value it was declared with, every table with its columns, and
binds every command to a line on the terminal saying which one the interface asked for.
Editing a `.tsx` rebuilds it in place, the same hot reload an application gets.

`--set count=99` reads a value as that signal's declared type, `--rows products=50`
generates deliberately obvious placeholder rows, `--data mock.json` does a screenful at
once, `--layout` says which route — or which file — to open on, and `--no-build` shows what
is already compiled. A key naming neither a signal nor a table is refused rather than
skipped: a misspelling in a mock file is otherwise a screen that stays empty for a reason
nobody can see. Every refusal exits non-zero.

[Prototyping](docs/setup/prototyping.md).

### The log

`LogLevel` is `Off`, `Error`, `Warning` or `Info`. `config.logLevel` sets it and defaults to
`Warning`, and `RDA_LOG=off|error|warning|info` overrides whatever an application asked for,
because an environment variable is somebody standing at the machine and they outrank a value
compiled in months ago.

A **Release** build could previously neither log nor stop. Every macro compiled away, which
did not make a release build quiet so much as deaf: it could not report a theme that would
not load or a swapchain that would not be created, and `RDA_RUNTIME_ERROR` expanded to
nothing, so a fatal failure carried straight on into the next line. Both are fixed — errors
are reported, and fatal ones still throw.

### Fixed

- **Animation snapped at the end, for two separate reasons.** A journey is read from one
  clock and that clock was x's, so a widget moving only downward — every child of a vertical
  stack, which is most widgets that move at all — had an x that had never been anywhere and
  therefore a clock saying nothing had happened. It stood still for the whole duration and
  arrived in a single frame. Either axis having somewhere new to go now restarts both. And
  the step that lands a value *on* its target reported nothing as moving, so an on-demand
  window skipped the one frame that would have shown it there: the end of every journey
  never reached the screen, and sat a frame short until something unrelated forced a walk.
  Three cases cover both, linear on purpose, because with an easing that decelerates the
  leftover is a fraction of a pixel and the same bug is invisible.
- **Toggling window decorations could take the process down.** The GLFW position, size and
  refresh callbacks fire synchronously out of `SetWindowPos`, and `SetWindowPos` is what
  `state.rda.maximized = true` eventually calls — so the frame the engine hangs off those
  callbacks (to keep drawing while a window is dragged, when `glfwPollEvents` will not
  return) started in the middle of the walk that ran the handler, reset the frame arena and
  truncated the draw list underneath it. `runFrame` now says while it is building, and that
  second door only asks for another frame instead. Dropping the event is safe: the new size
  and position are read again at the top of the next frame.
- **`anchor="pointer"` was a type error** in a project's generated `rda.d.ts`. An element
  that shadows a common property now emits `Omit<RdaCommonProps, "anchor">`.
- A scroll thumb follows the left button specifically, so a right-click on its way to a menu
  no longer grabs it.

### Also

- 138 test cases, up from 135.
- Linux: Debug **and** Release both build on GCC 13.3 and pass, from a clone. The log macros
  expanding in Release is what found two includes MSVC had been supplying by accident —
  `<cstring>` for `strrchr` and `<strings.h>` for `strcasecmp`.

## 1.0.0 — 2026-09-22

C ABI **1.6**. 55 functions to 88, with nothing removed and nothing changed in meaning.

One thing was not compatible, and it is why this was 1.0 rather than 0.10: **a colour is
the colour you wrote.** The GUI shader took sRGB bytes into a linear pipeline, so every
value in every theme drew lighter than it was written, and applications had been
compensating for it in their own palettes.

Drawings, from an `.svg` rasterised at the size the layout gives it — and animated, since
`<animate>`, `<animateTransform>` and `<set>` are read, with `stroke-dasharray`, which is
what makes an icon draw itself. Motion with a shape: `route` is an SVG path a widget travels
and `pace` is how fast it travels it. `<stream>`, a picture that keeps arriving, and compute
effects over one to four pictures with no Vulkan in the application. A window of your own —
icon, `decorated`, size bounds, and four signals of `state.rda` that read and write, so a
close button is an assignment. `arrange="tiles"` as a second docking arrangement. `<popup>`,
undo per text field, a label in more than one colour, rows of different heights, `onHover`,
pictures from memory, `tint`, `submitKey`, `language`. And a collapsing container takes its
contents with it rather than slicing them.

## 0.9.0 — 2026-09-08

C ABI **1.1**, and the reason there is one: a backend reaches the interface layer through a
C ABI, so it does not have to be C++. Python, Node and C# each got a package, a generator
that emits the state declaration in that language, a CMake rule and a test that starts a
real engine.

Docking with a customisable chrome, animation as an inherited property, alignment at three
levels, and text that is UTF-8 from the `.tsx` to the screen — the atlas had covered ASCII
32..126 and replaced everything else with a space, which is correctly laid out, silently
wrong, and invisible to a test written in English.

The Vulkan SDK stopped being vendored: `Dep/` was 1.2 GB of a 1.22 GB repository. The first
Linux build, which found 31 headers using a fixed-width integer without including
`<cstdint>` themselves. And the documentation became a folder per subject rather than four
files.

## 0.1.0 — 2026-08-27

The turn that made this an interface engine rather than a renderer. A layout is written in
TypeScript and compiled ahead of time into a flat binary blueprint, so the application that
loads it contains no parser and no JavaScript engine. Then signals and bindings over a state
declaration both sides are generated from, compiled to stack bytecode so that writing a
signal marks exactly the bindings that read it; hot reload that survives the swap; and
themes in TypeScript, compiled the same way.

Added here and removed again before the end: a run-time JavaScript host, a global runtime
process with its own wire protocol and compositor, and an in-repo sandbox. They were routes
to the same goal, and the compiled blueprint replaced all three.

## Before that — unversioned

A Vulkan renderer: instance and device selection against a real surface, swapchain and
render pass, framebuffers, shaders compiled at run time and cached, a memory allocator over
VMA, window and input handling on GLFW. Then an immediate-mode GUI over it, with a baked
font atlas carrying a reserved white block, so that solid rectangles and text of every size
sample one texture and a whole frame batches into a single draw.

---

Entries before 1.1.0 were written after the fact. The commit messages are the longer record,
and the one written at the time — `git log` has them in full.
