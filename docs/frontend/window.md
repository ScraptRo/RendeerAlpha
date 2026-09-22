# The window

Most of a window is the backend's business — it exists before any layout does, so what it
looks like is stated where the engine is started. What it *does* afterwards is state, and
a layout reads and writes it like any other.

## How it opens

Every backend takes the same set. Python names them in snake_case, Node in camelCase, C#
in PascalCase; the engine's C++ config calls the whole group `window`.

```python
config = rda.StartupConfig()
config.name    = "Ledger"
config.width   = 1100
config.height  = 700
config.icon    = "res/icons/app.svg"
config.min_width, config.min_height = 640, 400
```

| | |
| --- | --- |
| `icon` | the picture the OS shows for the window |
| `decorated` | the OS frame: title bar, border, the three buttons. Default true |
| `resizable` | whether the reader may resize it. Default true |
| `maximized` | opens filling the work area |
| `fullscreen` | opens covering the monitor |
| `always_on_top` | stays above other windows |
| `transparent` | a framebuffer with a real alpha channel |
| `opacity` | the whole window, frame included. 0..1 |
| `min_width`, `min_height`, `max_width`, `max_height` | bounds the reader cannot drag past; 0 is no bound |
| `x`, `y` | where the top-left corner opens; unset leaves it to the platform |

These are read when the window is created, which is why they are on the config rather than
callable later. `transparent` is the only one that cannot be changed afterwards at all —
the alpha channel is part of the surface, and the surface is made once.

## The icon

`icon` takes a `.png`, or an `.svg` — and an SVG is the better answer. An OS asks for the
icon at several sizes (16 for the title bar, 32 for alt-tab, 256 for a window list that
draws thumbnails), and a drawing is rasterised at every one of them, so the small sizes
are *drawn* small rather than being a large one squeezed down. Give it a PNG and it is
handed over at the single size it was saved at, and the OS scales.

It can also be changed while the program runs — `rda.set_icon(path)`, `setIcon`,
`Rda.SetIcon` — with `""` putting the platform's default back.

**This is not the executable's icon.** The picture Explorer shows for the `.exe`, and the
one a pinned shortcut keeps, is a resource compiled into the binary; it is put there when
the program is built and nothing at run time can change it.

## A window with no frame

`decorated = false` gives a window with no title bar, no border and no buttons — the
layout draws all of it. Two things make that work.

**`dragWindow`** on any widget makes dragging it move the window. It is a property rather
than a `<titlebar>` element because the bar is whatever the designer made it — a panel, a
stack, a label — and only one of its jobs is being a handle. A button sitting on the bar
still takes its own press: the bar declares itself before its children draw, and the one
under the pointer last is the one that gets it.

**`state.rda`** is the rest. Four of the engine's own values may be written as well as
read, which is how the buttons do something:

```tsx
<panel id="bar" height={34} dragWindow={true}>
  <stack id="row" arrange="horizontal" padding={6} spacing={6} vAlign="center">
    <label id="title" width="fill" height="content" text="Ledger" />
    <button id="min"   text="_"  width={30} height={22}
            onClick={() => state.rda.minimized = true} />
    <button id="max"   text="[]" width={30} height={22}
            onClick={() => state.rda.maximized = !state.rda.maximized} />
    <button id="close" text="x"  width={30} height={22}
            onClick={() => state.rda.open = false} />
  </stack>
</panel>
```

A maximised or fullscreen window ignores `dragWindow`: dragging one somewhere would leave
it maximised and in the wrong place.

**A frameless window cannot be resized by dragging its edges.** There is no non-client
area to drag and the engine does not invent one, so `resizable` has nothing to act on
once `decorated` is false. Maximising still works, and a corner grip that writes a size
is a layout's to write.

## `state.rda`

| | |
| --- | --- |
| `width`, `height` | the window's size in pixels, the same ones a widget is sized in |
| `focused` | whether it has the keyboard |
| `maximized` | **writable** — setting it maximises or restores |
| `minimized` | **writable** — setting it iconifies or restores |
| `fullscreen` | **writable** |
| `open` | **writable** — writing false closes the window |

The four writable ones are a conversation rather than a report. The OS changes them when
the reader presses a button or alt-tabs; the interface changes them when a title bar it
drew is pressed. Whichever side moved last frame is the one that meant it.

Making them state rather than calls is the same answer this engine gives everywhere else:
`state.rda.maximized = true` is a window maximising, in a handler that is otherwise just
an assignment — and a label bound to `state.rda.maximized` follows it without being told
that windows can be maximised. A backend reads and writes the same four as ordinary
signals, under those names.

`width`, `height` and `focused` are the engine reporting, and writing one is refused where
it is written rather than allowed and quietly undone.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
