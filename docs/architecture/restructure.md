# Restructuring the engine into layers

**Status: proposal.** Nothing here is built yet. It comes from six read-only reviews of the
whole engine, run side by side on 2026-10-22, plus an include graph taken from the tree.
The findings that could be checked cheaply were checked against the code. Each one says
where it is.

The short version: most of the tangle is about **who owns what and which way the
dependencies point**. Very little is about how an algorithm is written. So the fix is mostly
moving code and cutting edges, and almost nothing has to be rewritten.

## What the review found

### 1. The directories depend on each other in both directions

`src/` is grouped by topic (`GraphicalSrc`, `GraphicalObjects`, `Layout`, `Core`), and the
topics include each other in both directions:

| Edge | Count | What crosses |
| --- | --- | --- |
| GraphicalSrc → GraphicalObjects | 19 includes | Texture, Mesh, Material, Scene, Window, Gui, Viewports. GPU resources live among the widgets, and the renderer reads the window and its GUI |
| GraphicalObjects → GraphicalSrc | 14 | the other half of the same cycle |
| Core → GraphicalObjects | 2 | `Colour.cpp` and `Svg.cpp` include `GuiTypes.h` for `parseColor`, which is declared there and defined in Core |
| GraphicalObjects → Layout | 2 | `GuiTheme.h` includes `Layout/Blueprint.h`, so the widget base header depends on the layout file format |
| Platform ↔ GraphicalObjects | 3 / 6 | Window owns the platform hosts; the hosts include widget headers |
| Gui ↔ Docking | – | `Gui` holds a `DockSpace` by value; `Docking.cpp` includes `Gui.h` |
| lower layers → `RendeerAlpha.h` | 11 files | Widget, Streams, Primitives, all four Platform files, LayoutLoader, LayoutHost, Router. They use `rendeerRequestRedraw`, `rendeerInterfaceChanged`, `rendeerRetireTexture` and `createMesh` from the public facade |
| Core headers → the loop | – | `Core.h` and `BackendConnector.h` declare `appInstance`, `windowList` and `InitGlfw`, defined in `RendeerAlpha.cpp` and `Window.cpp`; `getScene()`, `viewportGpu()` and `enabledValidationLayers()` are declared low and defined in the loop file |

On top of that, `Core/Framework.h` includes `<vulkan/vulkan.hpp>`, and through `Logger.h`
that reaches **58 files**, from widgets to the layout compiler, even though no file uses
`vk::`. The compiler parses Vulkan to compile a mesh.

### 2. A handful of files each do ten jobs

| File | Lines | What is in it |
| --- | --- | --- |
| `RendeerAlpha.cpp` | 1517 | about 17 jobs, including: <br>• Vulkan instance and validation<br>• bring-up and teardown<br>• event pump and modal pump<br>• GLFW → GuiInput translation<br>• frame orchestration<br>• window-state signals<br>• resource pools<br>• the deferred-destruction queue<br>• synthetic test input<br>• the public API |
| `Gui.h/.cpp` + `GuiWidgets.cpp` | 3450 | about 19 concerns in one class, including: <br>• draw-list builder<br>• ids and hot/active<br>• focus<br>• shortcuts<br>• drag and drop<br>• accessibility collection<br>• window chrome<br>• text metrics<br>• the immediate widgets<br><br>`Gui::textField` alone is about 880 lines |
| `Widget.h/.cpp` | 3983 | the base class plus 20 widget types. Each type's code is scattered across the file by concern. `Widget::placement()` does layout plus nine side effects |
| `Window` | 873 | GLFW, presentation (surface, swapchain, framebuffer), the GUI, three platform hosts, frameless dragging, icon decoding |
| `Renderer` | 1080 | frame sync, the window registry, pass order, offscreen targets, GUI capture, pixel conversion. It reads `Window`, `Gui`, `getScene()` and `viewports()` in the middle of a frame |
| `Docking.cpp` | 1434 | four classes; `DockSpace::update` is about 470 lines |
| `LayoutLoader` | 1069 | about 10 jobs; the node walk is written twice (whole layouts and pooled rows), and the copies have drifted |
| `ScriptHost` | 1240 | QuickJS runtime, natives, module loader, state.ts computed values and actions, timers |
| `FontAtlas` | 977 | system-font discovery, packing, rasterising, text metrics. Its `const` methods write through 10 `mutable` members, and the GPU renderer owns it, so text cannot be measured without a GPU |
| `StateSchema.cpp` | 1843 | the state.ts reader, an XML reader and four code generators |
| `Cli/rda.cpp` | 1480 | every subcommand, the incremental build, and the prototype/shot harness (`show()` is about 600 lines) |

### 3. Every widget type is described in seven places

A widget's properties are written out by hand in:

- `WidgetSchema`
- the loader's constructors
- the loader's static-prop chain
- the loader's event chain
- `applyBoundValue` in Bindings
- `Probe`
- the compiler's and type emitter's notion of row templates

The last two are hard-coded lists. Most of the rest are `dynamic_cast` ladders: **43 in
LayoutLoader, 19 in Bindings, 15 in Probe**. Adding a widget touches about six files, and
the copies have already drifted:

- **Some properties type-check as bindable and are then dropped at run time.** `rda.d.ts`
  types every value prop as bindable, but `applyBoundValue` has no branch for many of them.
  - Whole widgets: Scroll, Splitter, Viewport, the dock widgets.
  - Single props: `Label.hAlign`/`vAlign` (checked), `Image.fit`, `Tabs.barHeight`,
    `ListView.rowHeight`.
  - Such a binding compiles, then is killed at run time with a warning.
- **The parsers disagree.** `alignFrom` accepts `"auto"` in the loader and not in Bindings,
  and `arrange` is parsed differently by each.
- **Subclasses depend on order.** A new subclass silently takes its parent's branch.
  TreeView only works because it is nested inside ListView's branch.

### 4. Synchronization is hand-rolled in more than ten places

Some of these are written more than once:

- `LoopWork`
- the command poll queue and the file-drop queue (the same bounded drop-oldest mailbox,
  written twice)
- `ViewportEventQueue` (a third copy of that pattern, as its own comment says)
- `AccessibleBridge`
- the wake flags, split between the loop and `Window`
- the start-up latch in the C ABI
- the deferred texture-destruction list in the loop file
- per-frame fences in `Renderer`
- present semaphores in `Swapchain`
- the stream "is anyone watching" registry
- the widget walk guards
- the dock panel's pending spawns and removal

None of them has a home, and four have bugs or hazards:

- **`Streams::push` holds its lock across a whole GPU upload,** including `vkQueueWaitIdle`.
  A producer calling `wanted()` from another thread blocks for that long.
- **`Logger::write` has no lock,** though the loop runs on its own thread.
- **The platform hosts wake every window.** They call the global `rendeerRequestRedraw()`,
  so an IME keystroke in one window redraws all of them.
- **Two closes in one frame lose one.** DockSpace defers removal through a single slot.

### 5. Globals have no owner

There are about 65 file-level globals in `src/`. There are also 13 process-wide registries:

- `signals()`, `commands()`, `tables()`
- `bindings()`, `scriptHost()`, `styleWatch()`
- `widgetMeshes()`, `viewports()`, `images()`, `streams()`, `effects()`
- `loopWork()`, `defaultShaderCompiler()`

Most are deliberately leaked, and nothing resets them between runs. So a second `rda_init`
keeps commands bound to the first backend's function pointers. Teardown order is
hand-sequenced in `engineMain`, with comments explaining what VMA asserts if the order is
wrong.

### 6. Build-time code lives inside the run-time library

- **The compilers are in the runtime library.** They sit in `rendeer`, in the runtime's
  directory and namespace.
- **The runtime calls the compiler.** `LayoutHost` calls `importedFiles` unconditionally,
  which pulls the esbuild runner and prelude strings into every application and into
  `rendeer_c`. `StyleWatch` calls the theme and mesh compilers. `Prototype.h` includes
  `StateSchema.h`.
- **The compiler calls the runtime.** It parses `.rdam` meshes through the run-time reader.
  That is because the mesh format is defined twice, and reading the output back is how it
  checks the two copies still agree.
- **About 1,370 lines of JavaScript are C++ raw strings.** They are spread over three files
  in three conventions, one of them split into three literals to fit MSVC's limit. acorn
  already shows the better way: a `.js` file embedded by CMake.

### 7. Duplicated code

| Duplicated | Copies |
| --- | --- |
| QuickJS exception → text, with different behaviour each time | 6, plus one in Scripting |
| QuickJS runtime/context setup | 7 |
| One-shot GPU submit | 2: `immediateSubmit` and MemoryBuffer's own |
| GPU readback plus sRGB encode | 2: Renderer and Streams |
| Scrollbar thumb drag | 5: Scroll ×2, TextField, ListView, GridView |
| The hot/active block | 6 immediate widgets |
| Accessibility snapshot diff, hit test, caret clamp | Windows and Linux backends; they already differ on single-select deselection |
| File-timestamp polling | 4 |
| "Which interface is showing" (host or router, update, teardown) | the C ABI, `rda`, and every C++ application |
| Language escaping in the generators | 4. Python, Node and C# do not escape newlines (checked for Python), so a text default containing one generates a broken literal |
| Window set-up | 2: the main window and `rendeerCreateWindow`. Only the main window gets clipboard handlers (checked: `RendeerAlpha.cpp:589`), so text fields in a secondary window cannot copy or paste |

### Bugs found on the way

These were checked in the code:

- **Secondary windows cannot copy or paste.** Only the main window gets clipboard handlers.
- **A newline in a text default breaks the Python module.** Python state modules do not
  escape it; the reviewer reports the same for Node and C#.
- **Bound props that type-check do nothing.** Examples are `Label.hAlign` and the others in
  section 3; each is dropped at run time with a warning.
- **`rda_set_title` goes around `Window::setTitle`.** It calls GLFW directly, so the name a
  screen reader is given stays the old one.

These were reported, and look right, but were not checked:

- **A second close in one frame is lost.** DockSpace's pending removal is a single slot.
- **Back and forward can call into a dead Router.** `Router` binds them with lambdas that
  capture `this`, and nothing unbinds them.
- **A window-less teardown may leak.** The renderer is torn down only `if (mainWindow)`, so a
  window-less run may destroy the allocator with images still alive.
- **`ShadowPass` declares the set-0 GLSL itself** instead of using `kSceneSet0Glsl`. That is
  exactly the drift `SceneBindings.h` exists to prevent.
- **`GuiRenderer` has a fixed 256-set descriptor pool,** a ceiling the descriptor allocator
  removed everywhere else.
- **Synthetic presses only reach the first window.** A function-local `wasDown` is shared by
  every window.
- **Stale comments:**
  - the root CMake says the C ABI is 1.6 (it is 1.8)
  - the Studio/App build tiers are still mentioned in several places
- **Dead vendored code:** `shaderc-main` (215 tracked files), `nlohmann`, `OBJ-Loader`.

## The layers

The six layers proposed are the right cut. One point decides how they stack: **the order
they were listed in is not the order they depend on each other.** The synchronization
engines in particular sit near the bottom. Streams, viewport events, the GPU retire queue
and the accessibility bridge all live below the widgets and use them, so a sync layer above
them would make every one of those edges point upward.

Bottom to top, each layer may use only the ones below it:

```
 runtime/static  runtime/dynamic      6  Runtime Environment Handler
 blueprint/                           4  Widget Blueprint Handler
 ui/                                     the widget toolkit (widgets, Gui, text, theme)
 renderer/                            3  Renderer
 engine/        platform/             2  Base Engine Functionalities (+ the OS)
 vulkan/                              1  Core Vulkan handling
 state/   formats/                       the shared model and file formats
 sync/                                5  Synchronization engines
 foundation/                             logging and plain utilities
```

Five pieces are added to the six. Each one exists because some code fits none of the six
without pulling a layer the wrong way:

| Added | Why |
| --- | --- |
| **foundation** | The logger, Utf8, Json, Png, Svg, colour and the container library are used by every layer and know nothing about any of them |
| **state** | Signals, commands and tables are read by widgets (`Select` reads tables) *and* by the blueprint runtime. So the types sit below both, while the instances belong to the runtime |
| **formats** | The blueprint, expression-program and mesh file formats, the widget schema and the script ABI. The runtime and the toolchain both need them. In a layer of their own, neither has to link the other |
| **ui** | The widget toolkit. Your list has no slot for it: the Blueprint Handler builds widgets but is not the widgets |
| **toolchain** | The compilers and code generators, in a library of their own. The engine stops carrying esbuild and the generators |

### What goes where

**foundation/ (`rda_foundation`)**
- Log: Vulkan removed, a lock added, moved into `RDA::`.
- Utf8, Json, Png, Svg.
- `Colour.h`, with `parseColor` out of `GuiTypes.h`.
- `ColorSpace.h`: one sRGB table, today in three places.
- The `RDA_Library` containers, moved out of `vendor/`.

**sync/ (`rda_sync`)** — the synchronization engines. One pattern each, written once. They
are owned by `EngineContext`, never by function-local statics.

| Engine | Replaces |
| --- | --- |
| `LoopMarshal` | `LoopWork` |
| `WakeSignal` | `gRunning`/`gRedrawRequested`/`gWakeRequested`, per-window redraw, `rendeerRequestRedraw`, `rendeerInterfaceChanged` |
| `Mailbox<T>` | the command poll queue and the file-drop queue |
| `Coalescer` | `ViewportEventQueue` |
| `SnapshotBridge<T,R>` | `AccessibleBridge` |
| `Latch` | the C ABI's start-up handshake |
| `RetireQueue<T>` | `gRetiredTextures`, the waitIdle-and-forget pairs, `releaseMaterial`'s rule |
| `DeferredMutations` | `WidgetTree` deferred edits, the walk guards, DockSpace's single slot and pending spawns |
| `FileWatcher` | four copies of timestamp polling |
| `StatePump` | the fixed per-frame order — assets, then timers and scripts, then bindings — today copied in the loop and in `Probe` |

**state/ (`rda_state`)** — Signals (with its observer graph and eager queue), Commands
(with `CommandArgs`), and Tables.

**formats/ (`rda_formats`)**
- The Blueprint reader and writer.
- `ExpressionProgram`: `Op`, `Program`, the slot marks `@ # $ % %r` defined once, and
  `disassemble`. The VM moves to the runtime.
- `MeshFormat`: one definition, replacing two copies.
- `WidgetSchema`, and ThemeSchema's tables (without its global registry).
- `ScriptAbi`: native names, the module-wrapper signature, a version.
- `ImportScan`.

**vulkan/ (`rda_vulkan`)** — layer 1.
- `Instance`: from the loop file and Logger; it owns the `VkInstance`, so the `extern` goes.
- `Device`: GPU pick, queues, VMA. No GLFW, and `getGPU()` becomes const.
- `Surface`: the `WindownSurface` rename.
- `Swapchain`.
- `OneShotSubmit`: one copy.
- `FrameSync`: per-window fences and semaphores, from Renderer and Swapchain.

**platform/ (`rda_platform`)**
- An `OsWindow` interface with one GLFW implementation; `getGLFW()` disappears from callers.
- `Win32/`, `Linux/` and `Null/` implementations of:
  - the accessibility host
  - text input
  - the native frame
- A `WndProcChain`, so three subclassers stop depending on attach order.
- File dialogs, locations and system-font discovery.
- The implementation is picked by CMake, not by `#if` inside every file.

**engine/ (`rda_engine`)** — layer 2, the abstraction over Vulkan.
- `GpuBuffer`, `GpuImage` (today's `Texture`, moved down) and `ImageLoader` (stb, moved up
  out of `Texture`).
- `RenderTarget`, `GraphicsPipeline`, and a new `ComputePipeline`, taken from Effects'
  hand-built one.
- `Descriptors`, with single-set freeing, so `GuiRenderer`'s raw pool goes.
- `Shader` and `ShaderCompiler`, with the cache directory taken from config.
- `GpuReadback`: one copy.
- `Mesh`, `Material`, `VertexLayout`.
- `PresentationSurface`: surface, swapchain and framebuffer, out of `Window`.
- `ResourcePools`: mesh and texture pools and factories, out of the loop file.

**renderer/ (`rda_renderer`)** — layer 3.
- `Renderer` keeps pass order only. It takes a `FrameInputs`, which the runtime builds:
  - the draw list and its version
  - the clear colour
  - the viewport rectangle
  - the scene and the scene-recorder callback

  It no longer reads `Window`, `Gui`, `getScene()` or `viewports()`.
- `OffscreenTargets` and `GuiCapture`.
- `Gui/GuiRenderer`, which includes the draw-list header instead of all of `Gui.h`.
- `GlyphAtlasTexture`: the GPU half of the font atlas.
- `Scene3D/`, as one removable unit (the CMake already says the 3D path is meant to leave):
  - Scene, ForwardPass, ShadowPass, SceneFrame, SceneBindings, MaterialResources
  - Primitives
- `ComputeFilters` (Effects), `StreamSurfaces` (the GPU half of Streams), `ViewportCanvas`
  (the Vulkan half of Viewports).

**ui/ (`rda_ui`)** — the widget toolkit. It contains no Vulkan.
- `Text/`: `FontFaces`, `GlyphCache` and `TextMetrics`.
  - Text can be measured without a GPU.
  - Glyph baking becomes an explicit call instead of a `mutable` side effect.
- `Style/`: the Theme registry (no Blueprint include), the style structs, Syntax.
- `Canvas/`: `DrawList` (the renderer contract) and `Canvas2D` (the draw-list builder, out
  of `Gui`).
- `Context/`: `Gui` becomes a facade over:
  - `Interaction` (ids, hot/active as one helper)
  - `Focus`, `Shortcuts`, `DragDrop`, `A11yCollector`
  - `WindowChrome`, `Motion`
- `Immediate/`:
  - Button, Checkbox, Slider and Splitter, one file each.
  - `ScrollBar`: one implementation, today five.
  - `TextEdit/`: `textField`'s 880 lines split into state, commands, layout, render and
    accessibility.
- `Widgets/`: the `Widget` base, with its props grouped, and one file per widget:
  - `Containers/`, `Controls/`, `Media/`
  - `Data/`: ListView, GridView, TreeView, moved out of the layout runtime
  - `Docking/`: `Docking.cpp` split into widgets, space, panes, tiles, and a shared chrome
- Accessibility model:
  - the `Accessible` tree
  - `AccessibleDiff` and `AccessibleQuery`, shared by the UIA and AT-SPI backends, which then
    only translate

**blueprint/ (`rda_blueprint`)** — layer 4, the Widget Blueprint Handler.
- **`WidgetRegistry`:** one record per element, which each widget family registers once.
  - Each record holds `make`, `applyConst`, `applyValue`, `attachEvent` and `readText`.
  - It replaces the three cast ladders and Probe's.
  - A unit test then checks that every bindable prop in the schema has a setter, which closes
    the drift in section 3.
- **`Instantiator`:** one `buildSubtree`, used for both layouts and pooled rows.
- **`NameResolver`:** defining a missing signal becomes an explicit policy.
- **`ThemeReader` and `WidgetMeshReader`.**

**runtime/ (`rda_runtime`)** — layer 6, the Runtime Environment Handler.

| Part | What it holds |
| --- | --- |
| **static** — set up once, read-only after | <br>• `EngineContext`: owns the instance, device, renderer, registries and sync engines; teardown is member order<br>• `Startup` / `Shutdown`<br>• `WindowSetup`: one path for every window, which fixes the clipboard bug<br>• `DisplayScale`: replaces seven `> 0 ? s : 1` guards<br>• `WindowRegistry`<br>• the stores of what is loaded: blueprints, theme, meshes, compiled script modules |
| **dynamic** — changes every frame or event | <br>• `FrameLoop`, `EventPump`, `ModalPump`<br>• `InputGather`: GLFW → GuiInput<br>• `AppWindow`: OsWindow + PresentationSurface + Input + Gui + the platform hosts, with one `frameFinished(hints)` instead of five hooks<br>• `WindowStateSignals`<br>• `BindingEngine`, with a `RowBindingSet` taking over ListView's private evaluator<br>• the expression VM<br>• `Script/`: `QuickJsRuntime`, `Natives`, `ModuleLoader`, `StateCode`, `Timers`<br>• `LayoutInstance`, `Router`<br>• `InterfaceSession`: host or router, shared by the C ABI, the preview tool and applications<br>• `AssetReload`, behind a `Rebuilder` seam<br>• dock-layout persistence |

**Around them**
- `rendeer`: an INTERFACE umbrella, so `add_subdirectory` consumers do not change.
- `RendeerAlpha.h`: split into App, Windows and Resources, still included by one name.
- `rendeer_c`: split into files over a `CMarshal.h`, with no GLFW.
- `rda_automation`: synthetic input and capture, linked only by the preview tool.

**toolchain/ (`rendeer_toolchain`, namespace `RDA::Build`)**
- `Js/`:
  - `JsContext`: one runtime/context and one exception formatter, today seven.
  - `Esbuild`.
  - Every prelude and the analyzer as real `.js` files, embedded by one CMake function the
    way acorn is.
- `Layout/`: LayoutCompiler, Components, ExpressionParser, ScriptPlanner.
- `State/`: reader and routes.
- `Codegen/`:
  - `CodeWriter`: one escaper per language, which fixes the newline bug once.
  - One generator per language: C++, Python, Node, C# and TypeScript.
- `Theme/`, `Mesh/`.
- `Project/ProjectBuild`: loads state once and compiles the first theme once. Today one
  `rda build --python` runs state.ts through esbuild three times.

`Cli/` splits into two executables:
- `rda`: the toolchain only, no Vulkan, one file per subcommand.
- `rda-preview`: prototype and shot, with a typed input script instead of control-byte
  strings.

### How the build enforces it

Each layer is its own static library with its own `include/` root. `target_link_libraries`
lists exactly the layers below it. An upward include then fails to *compile*, because the
header is not on that target's path — the rule does not depend on review.

- `src/` comes off the public include path.
- `rendeer_tests` links the device-free layers (foundation, sync, state, formats, blueprint)
  instead of re-listing 30 source files.
- Hot reload links the toolchain only in Debug, through the `Rebuilder` seam, so the
  runtime never includes a compiler header.

### Static and dynamic, said once

A thing is **static** if it is decided before the first frame and only read after:

- the instance, device and renderer
- loaded blueprints, compiled programs, the theme and meshes
- the widget schema and registry

A thing is **dynamic** if a frame or an event changes it:

- signals, tables, bindings, scripts and timers
- live widgets, windows and input

The runtime's two halves follow that line. Hot reload is the one place where something
static is replaced, and it goes through `AssetReload`, so it is the only place that has to
know.

## How to get there

Every step leaves the engine building and every test passing, on Windows and Linux. Nothing
changes behaviour except the bug fixes named in that step. The steps can be committed one at
a time.

**Before step 1:**
- Commit what is in the tree now to the dev branch.
- Merge the worktree of the build-error task.

Step 2 moves almost every file. Doing it over uncommitted work would make the history
unreadable, and would make that merge a conflict in every file.

1. **Cut the wrong-way edges in place, with no files moving.**
   - `Framework.h` stops including `vulkan.hpp`.
   - `Colour.h` gets its own header.
   - `WakeSignal` and `RetireQueue` replace the `RendeerAlpha.h` includes.
   - `GuiRenderer` includes only the draw list.
   - The theme loader leaves `GuiTheme`.
   - `Core.h` and `BackendConnector.h` go.
   - The logger gets its lock.
   - The `Rebuilder` seam replaces the runtime's compiler includes.

   Small, and the largest win per line changed.
2. **The directories and targets above.** Mechanical moves with scripted include rewrites,
   and a per-layer CMake. Tests link the libraries.
3. **`WidgetRegistry`, and one file per widget.** Fixes the bindable-prop drift and the
   parser disagreements.
4. **Split the large files:**
   - `RendeerAlpha.cpp` → runtime/
   - `Window` → AppWindow, OsWindow, PresentationSurface
   - `Renderer` → `FrameInputs`
   - `Gui` → Context pieces, and `textField` split
   - `LayoutLoader` → Instantiator
   - `ScriptHost`, `FontAtlas` and `Docking` → their pieces

   Fixes the clipboard, title and window-less teardown bugs.
5. **Consolidate the sync engines.**
   - `Mailbox`, `RetireQueue`, `FrameSync`, `StatePump`, `FileWatcher`.
   - The stream lock no longer spans a GPU upload.
   - The shared accessibility diff.
   - The dock removal queue.
6. **Restructure the toolchain.**
   - `JsContext`, the embedded `.js` files, the generators and `CodeWriter`.
   - `rda` and `rda-preview`.

   Fixes the escaping bug.

The four checked bugs are small enough to fix immediately, ahead of all of this, if
preferred.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
