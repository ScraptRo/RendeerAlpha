# Pictures that keep arriving

## `<stream>`
A picture the backend replaces — a camera, a decoded video, a model's output as it is
produced, the result of a filter that runs every frame.

| | |
| --- | --- |
| `name` | which stream to show, by the name the backend pushes to |
| `fit` | `"contain"` keeps the frame's shape inside the box; `"stretch"` fills the box |

```tsx
<stream id="preview" name="camera" width={320} height={240} />
```

```python
rda.push_frame("camera", jpeg_bytes)            # PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PNM
rda.push_frame_pixels("camera", rgba, 320, 240)  # four bytes a pixel, rows packed
```

Node is `pushFrame` / `pushFramePixels`; C# is `PushFrame` / `PushFramePixels`; C++ calls
`RDA::streams().push(...)`.

## Why not `<image src="mem:...">`

That works, and for a still it is the right thing. The difference is what happens on the
second frame.

Registering an image **builds a texture**. A camera at thirty frames a second would build
and retire thirty textures a second, each one waiting on the frames still in flight before
it can be freed. A stream keeps the surface and writes into it: the size is settled by the
first frame, and only a change of size builds a new one.

So: `<image>` for a picture you set occasionally, `<stream>` for one that keeps coming.

## Asking whether anybody is looking

A feed on a screen the reader navigated away from should not be decoded. The engine knows
whether a `<stream>` painted lately, and a producer can ask:

```python
while running:
    if rda.stream_wanted("camera"):
        rda.push_frame("camera", camera.read())
    time.sleep(1 / 30)
```

Node `streamWanted`, C# `StreamWanted`. This is the useful half of "pull". The other half
— the engine calling back to ask for a frame — is something a JavaScript binding cannot
take, which is why what crosses is a question the producer asks rather than a callback the
engine makes. It is the same split `<viewport>` has.

It answers **true** for a name nothing has drawn yet, so a producer that asks before the
interface has painted once is not told to stop before it starts. If frames keep arriving
and no `<stream>` ever shows them, it goes false and the log says so, naming the stream —
which is almost always a name spelt two ways.

## What is being wasted

```python
pushed, shown = rda.stream_counts("camera")
```

`pushed` is frames sent; `shown` is frames that were still current when a widget drew them.
Pushing sixty a second into a window drawing thirty means half were replaced before
anything sampled them, and the gap says so.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
