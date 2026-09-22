# Filters over a picture

Applying a compute shader to an image in Vulkan is about two hundred lines that are the
same every time — a descriptor set layout, a pool, a set, a pipeline layout, a pipeline,
two image barriers, a dispatch, a fence — wrapped around about four that are the filter.
None of the two hundred is a decision anybody wants to make twice, and getting a barrier
wrong is a validation error rather than a wrong picture, so it is not even a productive
mistake.

The engine owns all of it. What you write is the filter.

```python
rda.define_effect("grey", """
    void main() {
        vec4 c = texture(src, uv());
        store(vec4(vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722))), c.a));
    }
""")

rda.push_frame("camera", jpeg_bytes)
rda.apply_effect("grey", "camera", "camera.grey")
```

```tsx
<stream id="out" name="camera.grey" width={320} height={240} />
```

Node is `defineEffect` / `applyEffect` / `forgetEffect`; C# is `DefineEffect` /
`ApplyEffect` / `ForgetEffect`; C++ calls `RDA::effects()` directly.

## What you get

| | |
| --- | --- |
| `src` | a `sampler2D` of the first input |
| `tap(i, at)` | the `i`'th input sampled at `at`, `i` in `0..3` |
| `dst` | a `writeonly image2D` of the output |
| `uv()` | this invocation's position, `0..1` |
| `coord()` | the same as integer pixels |
| `size()` | the output's size in pixels |
| `param(i)` | one of the eight floats passed to `apply_effect`, `i` in `0..7` |
| `store(c)` | write the result for this invocation |

One invocation per pixel, in tiles of 8×8. You do not declare a workgroup, a binding or a
layout; if you would rather, GLSL beginning with `#version` is taken as a whole shader and
nothing is prepended.

## More than one picture

Up to four. A filter with one uses `src` and never mentions the rest; one that blends two
reads `tap(0, uv())` and `tap(1, uv())`.

```python
rda.define_effect("blend", "void main() { store(mix(tap(0, uv()), tap(1, uv()), param(0))); }")
rda.apply_effect("blend", ["photo", "grade"], "graded", [0.35])
```

The output is the size of the **first** source, and the rest are sampled in `0..1` — so a
mask or a lookup of a different size is resized rather than refused, which is what you want
when the second picture is a mask and the first is a photograph.

Four because a blend takes two and a masked blend three; past that the thing being
described is a pipeline rather than a filter, and a pipeline is several effects chained,
which already works.

## Colours are linear here

Inside an effect you are in **linear light**, not the sRGB a theme is written in. `src` is
sampled from an sRGB surface, so the hardware has already decoded it, and what `store()`
writes is encoded again on the way to the screen.

That is also the only space image arithmetic is correct in. A blur averaged in sRGB is the
wrong average — it is the reason naively blurred images look darker than they should.

## Where the output goes

`apply_effect(name, source, into)` reads one stream and writes another. The destination is
made if it is not there and re-made when the source changes size, so you never size it.

The two may not be the same stream. A compute shader reading the image it is writing sees
whatever its neighbours got to first, which is a different picture on every run; that is
refused rather than left to surprise you. Chain instead:

```python
rda.apply_effect("grey",  "camera", "camera.grey")
rda.apply_effect("edges", "camera.grey", "camera.edges", [1.6])
```

## When it will not compile

The compiler's own message goes in the log, and it names the line — counted from the first
line **you** wrote, not from the engine's prelude. The call is refused rather than leaving
a filter that quietly does nothing.

## Getting the result back

```python
pixels, width, height = rda.read_frame("camera.edges")
```

RGBA8, rows tightly packed. Node `readFrame` gives `{pixels, width, height}`; C#
`ReadFrame` gives a tuple.

What comes back is **the picture as it looks on screen**. A surface an effect wrote holds
linear light, and it is encoded on the way out — so saving it as a PNG gives a PNG of what
was displayed, rather than something too dark. A pushed frame comes back the way it was
pushed.

It costs a round trip to the GPU and a wait: a staging buffer, a copy, a queue idle. That
is right for saving a frame or handing one to a model, and wrong for doing every frame —
which is what `<stream>` is for.

## What this is not

Up to four inputs, one output, eight floats, and a readback. That covers greyscale,
threshold, blur, sharpen, edges, colour grading, blending and masking — filters over
pictures, which is what it is for.

It is not a general compute framework: no storage buffers, no arbitrary dispatch shapes, no
compute over anything that is not an image. An application that needs those has
`<viewport>`, where it gets the command buffer and the device and can do whatever it
likes.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
