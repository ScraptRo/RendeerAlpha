# The layers

The engine is built in layers, and the build enforces them. Each layer is a directory under
`RendeerAlpha/` with an `include/` of public headers and a `src/` of everything else, and
each is compiled as its own object library that sees its own headers and the public headers
of the layers below it -- nothing above. An `#include` that points upward does not compile.

```
 toolchain   the compilers and code generators: layouts, themes, meshes, state.ts
 runtime     the running engine: bring-up, the frame, input, bindings, scripts, routes
 blueprint   blueprints turned into widgets: the registry of kinds, the instantiator
 ui          the widget toolkit: Gui, widgets, docking, text editing, themes
 renderer    what is drawn: the renderer, its passes, the font atlas, streams, effects
 engine      Vulkan made usable: buffers, textures, pipelines, shaders, meshes
 vulkan  |  platform      the device and the swapchain | the OS: windows' hosts, input, dialogs
 formats     what is written down: blueprints, schemas, expression programs, the a11y tree
 state       signals, commands and tables
 sync        what threads and frames hand each other: wake-ups, loop work, mailboxes, retire queues
 foundation  logging and plain utilities: UTF-8, JSON, PNG, SVG, colour, geometry
```

`vulkan` and `platform` sit side by side and may not reach each other: the engine above is
what puts a device and a window together. `api` -- the C ABI, `rendeer_c` -- sits outside,
on top of everything.

The include paths did not change. A header is still `<GraphicalObjects/Widget.h>` or
`<Layout/Bindings.h>`; what changed is which layer's `include/` holds it. So an application
written against an earlier version includes what it always did, and links `rendeer`, which
is still one static library made of every layer's objects.

## Deciding where something goes

Ask what it may not know. A file belongs in the lowest layer whose headers are enough to
write it, and a layer may not know anything about the ones above. Some answers that are not
obvious from a file's old directory:

- **The accessibility tree** (`GraphicalObjects/Accessible.h`) is in `formats`: it is what
  the widgets write and what the platform hosts read to a screen reader, and both sides are
  above it. What changed between two trees (`GraphicalObjects/AccessibleDiff.h`) is there too.
  What to say about a change is in `platform`, once per screen-reader API, because they do not
  have the same events.
- **The draw list** (`GraphicalObjects/DrawList.h`) is in `renderer`: it is what the
  renderer consumes, and the GUI above produces it.
- **`Rect`** (`Core/Rect.h`) is in `foundation`, because nearly every layer has one and
  none of them should include the GUI's types to say so.
- **The data widgets** -- `<list>`, `<tree>`, `<grid>` -- are in `ui`. They decide *when* a
  row needs its bindings applied; the runtime decides *how*, through `ListView::RowBinder`.
- **`Window`** is in `runtime`, and is what puts a window together: an `OsWindow`, a
  `PresentationSurface`, a GUI, input and the platform hosts. The renderer draws into it as a
  `RenderSurface`, an interface the renderer declares, and knows nothing else about it.
- **`Gui`** is in `ui`, and is a facade. `Canvas`, `Interaction`, `Focus`, `Shortcuts`,
  `DragDrop`, `A11yCollector` and `WindowChrome` own what it is made of. Their headers are in
  `GraphicalObjects/Gui/` because `Gui.h` holds each by value, and what one needs from another
  is passed in, so a method's arguments say what it depends on.
- **`OsWindow`** is in `platform`: the GLFW window and everything done to it, from its title to
  dragging a window with no frame. GLFW itself is private to `platform` and `vulkan`, the one
  layer needing it to make a surface, so a GLFW include anywhere above them does not compile.
  What the runtime wants from the window system it asks of `Platform/OsWindow.h` and
  `Platform/WindowSystem.h`, and key codes are `Platform/KeyCodes.h`.
- **The layout loader** is two things. Turning a blueprint into widgets is
  `blueprint/` (`Layout/Instantiator.h`); attaching bindings, handlers and a list's rows to
  them is `runtime/` (`Layout/LayoutLoader.h`), beside the binding engine and the script host
  those need.

## The seams

Where a lower layer has to reach something above it, it does so through a slot the upper
layer fills when the engine starts, never through an include:

| Seam | Lower side | Filled by |
| --- | --- | --- |
| `Core/Wake.h` | anything may ask for a frame or wake the loop | the frame loop, with GLFW's empty event |
| `Theme::setFileReader` | the style registry loads a theme file | the blueprint reader (`Layout/ThemeLoader.h`) |
| `Layout/Rebuilder.h` | hot reload recompiles a source | the toolchain, in a build with hot reload |
| `RenderSurface` | the renderer draws into a surface | `Window` |
| `ListView::RowBinder` | a list asks for a row to be brought up to date | the layout runtime |

## Adding a file

Put it in the layer it belongs to, under `include/` if another layer will include it and
`src/` if not, and add it to the source list in `RendeerAlpha/CMakeLists.txt`. The layer is
read from the path, so there is nothing else to register. If it does not compile because a
header is not found, that is the layering saying no -- the fix is to move what is needed
down, or to turn the call around into a seam, not to add an include path.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
