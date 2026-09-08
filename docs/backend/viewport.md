# Drawing in a viewport

A `<viewport>` is the one place a backend draws. Everywhere else the seam is the state:
the backend says what is true and the interface decides what that looks like. Inside a
viewport there is nothing for the interface to decide, so the application draws.

What it gets to draw with depends on the language, and the difference is large enough to
be worth saying plainly before either half:

| | |
| --- | --- |
| **C++** | the Vulkan command buffer, inside the render pass, at the viewport's resolution |
| **Python, Node, C#, anything on the C ABI** | a list of 2D commands the engine draws |

## The small half: 2D commands

Build a drawing, send it in one call. It stays on screen until another one replaces it, so
a picture that does not change is sent once and then costs nothing.

Coordinates are the viewport's own: `0, 0` is its top-left corner, and the units are the
same pixels the rest of the interface is laid out in. A drawing never has to know where on
screen the viewport ended up — the same list is correct in a docked pane, in a dialog, or
full width — and anything that overruns is clipped at the edge.

```python
import math, rda

width, height = rda.viewport_size("chart")

d = rda.Drawing()
d.clear("#11141A")
d.line(0, height / 2, width, height / 2, "#2A3140", 1)
for i, value in enumerate(samples):
    x = width * i / (len(samples) - 1)
    y = height / 2 - value * height * 0.35
    d.circle(x, y, 3, "#3A6AD0")
d.text(12, 12, "live", "#DCE0E7", 12)

rda.draw("chart", d)
```

```js
const { width, height } = rda.viewportSize('chart')
const d = new rda.Drawing()
d.clear('#11141A')
samples.forEach((v, i) => d.rect(i * 12, height - v, 8, v, '#3A6AD0', 2))
rda.draw('chart', d)
```

```csharp
var (width, height) = Rda.ViewportSize("chart");
var d = new Drawing();
d.Clear("#11141A");
foreach (var (v, i) in samples.Select((v, i) => (v, i)))
    d.Rect(i * 12, height - v, 8, v, "#3A6AD0", radius: 2);
Rda.Draw("chart", d);
```

### What there is to draw with

| | |
| --- | --- |
| `clear(color)` | fills the whole viewport, whatever size it turned out to be |
| `rect(x, y, w, h, color, radius)` | a rectangle, rounded if you like |
| `circle(x, y, radius, color)` | a rect with equal sides and a radius of half of them |
| `line(x1, y1, x2, y2, color, width)` | at any angle |
| `text(x, y, text, color, size)` | top-left at `x, y`; size 0 is the interface's own |

Colours are what a theme takes — `"#RGB"`, `"#RRGGBB"`, `"#RRGGBBAA"` — and a colour here
is the same pixels as the same colour in a theme. Font sizes that draw crisply are the
ones the atlas was baked with (see [themes](../frontend/themes.md)); another size is drawn
at the nearest baked one.

That is the whole list, and it is short on purpose. Five primitives cover a plot, a
timeline, a Gantt chart, a seating plan, a waveform, a board — the drawings a backend
actually wants — without the ABI growing a graphics API nobody asked for.

### Sending it

**One call, the whole picture.** Every call into the engine crosses to its loop thread and
waits, the same reason a table is filled a column at a time rather than a cell at a time. A
shape per call would be a frame per shape.

**An empty drawing takes it back down**, which is different from never having drawn: a
viewport no backend has touched shows the engine's own 3D scene instead.

**Ask how big it is, and draw again when that changes.** `viewport_size` reports what the
last frame laid out — both zero before the first one. A drawing sized to a stale answer is
the one mistake this is easy to make.

## The large half: Vulkan, from C++

A C++ application gets the command buffer. The render pass has already begun on the
viewport's own target and the viewport and scissor cover it, so the shortest useful
callback is a bind and a draw.

```cpp
#include <GraphicalObjects/Viewports.h>

config.viewportMode = RDA::ViewportMode::Widget;   // a viewport needs a target

config.onStart = [] {
    // Everything needed to build Vulkan objects of your own, including the pass the
    // recording goes into. Available here, so a pipeline is built once rather than
    // lazily on the first frame.
    RDA::ViewportGpu gpu = RDA::viewportGpu();
    buildMyPipeline(gpu.device, gpu.renderPass);

    RDA::viewports().setClearColor("scene", RDA::rgba(12, 14, 20));
    RDA::viewports().onDraw("scene", [](const RDA::ViewportFrame& frame) {
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gPipeline);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0);
    });
};
```

`ViewportGpu` carries the instance, the physical device, the device, the graphics queue
and its family, the VMA allocator, the target's colour and depth formats, the render pass,
and how many frames the engine keeps in flight. There is no wrapper in the way and no
subset to discover: whatever Vulkan can do, a viewport can do.

`ViewportFrame` carries the command buffer, that render pass, the target's extent, the
frame index (`0 .. framesInFlight - 1`, for per-frame resources), and the time — `seconds`
since the engine started and `delta` since the last frame, so a callback that animates does
not need a clock of its own.

Three things worth knowing:

**It runs after the engine's own scene, in the same pass.** An application that only wants
its own drawing simply adds no meshes; one that wants both — a gizmo over a model — gets
the order it expects.

**Recording is not optional work.** The callback happens inside a frame that is already
being built, so it must return. Slow work belongs on another thread, with the result
handed over for the next frame.

**The GPU is idle by the time `onShutdown` runs**, so whatever was built in `onStart` is
destroyed there with nothing still using it.

### Animation and on-demand redraw

The engine's default is `RedrawMode::OnDemand`: it renders when something it can see has
changed. A backend replacing a 2D drawing is such a change and asks for a frame by itself.
A Vulkan callback animating its own contents is **not** — the widget tree is identical and
so is the input — so an application animating inside a viewport either calls
`rendeerRequestRedraw()` while it animates, or sets `RedrawMode::Continuous`.

The upside of that arrangement is worth saying: a viewport animating at sixty frames a
second re-walks the interface zero times. The GUI's geometry is reused and only the
viewport's own pass is re-recorded.

## Which viewport gets the GPU

There is one offscreen target. The first viewport painted that is not drawing 2D commands
holds it; if a second one wants it too, the first keeps it and the log says so once. Any
number of 2D viewports can be on screen at the same time.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
