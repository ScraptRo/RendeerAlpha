# The viewport

## `<viewport>`

A hole in the interface for the application to fill. The layout decides where it is and
how big; what appears inside it is not the engine's business.

```tsx
<viewport id="scene" name="scene" width="fill" height="fill" />
```

| | |
| --- | --- |
| `name` | how a backend addresses this surface. Defaults to `main` |
| `visible` | draws it; hidden, it takes no space of its own |

It sizes and moves like any other element — `width="fill"`, a `<splitter>` beside it, a
`<dock>` around it — and a backend never has to know where it ended up. What it is told is
how big it is, which is the only thing a drawing actually needs.

Two viewports with the same `name` are the same surface drawn twice.

## What fills it

Three answers, and which one you get depends on what the application does.

**Nothing, and there is a 3D scene.** The engine's own renderer draws into it. This is
all this element used to be, and an application that adds meshes to the scene and puts a
`<viewport>` in its layout still works exactly as it did.

**A C++ program recording Vulkan.** It gets the command buffer, inside the render pass, at
the viewport's own resolution — its own pipeline, its own shaders, its own everything. See
[the backend's page on viewports](../../backend/viewport.md).

**Any other language sending 2D commands.** Rectangles, lines and text, in the viewport's
own coordinates, drawn by the engine. A plot, a timeline, a map, a board.

The split is deliberate rather than unfinished. A Vulkan command buffer across a C ABI
would mean a Python or Node program owning GPU memory, obeying a frame's timing, and being
on the render thread at the right microsecond — none of which it is in a position to do. A
list of drawings is something any language can produce.

## The one that has the GPU

There is one offscreen target, so one viewport at a time can be the Vulkan one: the first
one painted that is not drawing 2D commands. Any number of 2D viewports can be on screen
at once — they cost what their shapes cost and nothing else.

If two viewports both want it, the first painted keeps it and the log says so once, naming
both.

A C++ recording also needs somewhere to record into, which means `ViewportMode::Widget` in
the application's config. In `Fullscreen` mode the scene goes straight to the window and a
viewport has no target of its own; the log says that too, once. The 2D half needs no
such setting.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
