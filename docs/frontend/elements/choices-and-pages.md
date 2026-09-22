# Choices and pages

## `<select>`

Choices that are known when the layout is written are `<option>` children. Choices the
machine finds are a table:

```tsx
<select id="model" of="models" textColumn="name" valueColumn="id"
        placeholder="Pick a model"
        value={() => state.model} onChange={(v) => { state.model = v }} />
```

| | |
| --- | --- |
| `of` | a declared table to take the choices from, instead of `<option>` children |
| `textColumn` | which column a row shows. Default `text` |
| `valueColumn` | which column a row means. Default `value` |

There is then no maximum to declare and nothing to hide — the models installed, a recent
files list, the tables in a database. With `of` set the `<option>` children are ignored.

A hidden `<option>` is also properly skipped now, so "declare a maximum and bind
`visible`" works where it silently drew blank rows before.
A box that opens a list of `<option>` children.

| | |
| --- | --- |
| `value` | the chosen option's value |
| `placeholder` | shown when the value matches no option |
| `onChange(string)` | with the chosen value |

The open list is drawn after the whole tree has been walked, with the clip back at the
viewport, so it escapes whatever panel it lives in and sits over everything — which is the
one thing a dropdown has to do.

## `<option>`
`text` is what the row says; `value` is what choosing it sets. They are read where they
are, so a binding on an option's text is an ordinary binding on an ordinary widget.

## `<tabs>` and `<tab>`
A row of titles, and the one page whose title is selected.

| | |
| --- | --- |
| `value` | which page, counting from zero |
| `barHeight` | height of the title row |
| `onChange(number)` | with the index clicked |

`<tab>` takes a `title`; its children are the page. The pages that are not showing **still
exist and still hold their state** — a widget is a thing that persists, so leaving a tab
and coming back does not reset what is in it.

## `<image>`

| | |
| --- | --- |
| `src` | a path relative to the running program, an `.svg` to draw, or `"mem:<name>"` for a picture the backend registered |
| `fit` | `"contain"` keeps its shape inside the box; `"stretch"` fills the box |
| `tint` | multiplied into the picture. White leaves it alone; a colour is how one icon becomes any of them |

It loads its file the first time it paints and owns the texture, so the lifetime is the
widget's. Two images of one file are two textures — a UI has a handful of them, and a
cache outliving the tree is a cache somebody has to remember to invalidate.

### Icons that stay sharp

An `.svg` is **drawn**, not loaded. It is rasterised at whatever size the layout turned out
to give the widget — so it is crisp at 16px and at 96px from one file, and on a display at
150% it is crisp at a size nobody exported.

```tsx
<image id="save" src="res/icons/save.svg" width={20} height={20} tint="#DCE0E7" />
```

`tint` multiplies the picture, and it is a vertex colour rather than a second
rasterisation — so one white-drawn icon in six colours costs one texture, and a tint bound
to a signal changes colour for free.

Which is why **`currentColor`, and a `fill` the file never stated, come out white** rather
than the black the SVG specification asks for. An icon set almost always means "whatever
colour the text is", and the specification's answer would draw invisible icons on every
dark theme. White is the one colour a tint can still turn into any other; a file that
states its own colours keeps them, because the default tint is white.

It reads what icons are made of: `<path>` with the whole path grammar including arcs,
`<rect>` with `rx`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<g>`,
transforms, fills with either winding rule, strokes with caps and joins, and per-shape
opacity. It does **not** read text, gradients, patterns, filters, masks, clip paths or CSS
— none of which an icon has, and each of which is a renderer rather than a feature. A file
using them draws the parts it can and says so in the log.

Joins and caps are rounded. On an icon at sixteen pixels a mitre is a pixel of difference
nobody can see, and a disc cannot spike the way a mitre on a sharp angle does.

The size is the widget's, so re-sizing re-draws; the file is read once and the parsed shape
kept. Two `<image>`s of one icon are two textures, the same as for a file — a UI has a
handful, and a cache outliving the tree is a cache somebody has to remember to invalidate.

### Icons that move

An `.svg` carrying SMIL animates, with nothing to say in the layout — it is the same
`<image>`:

```tsx
<image id="busy" src="res/icons/spinner.svg" width={24} height={24} tint="#78AAFF" />
```

```xml
<g>
  <animateTransform attributeName="transform" type="rotate"
                    from="0 12 12" to="360 12 12" dur="1s" repeatCount="indefinite"/>
  <path d="M12 3 A 9 9 0 0 1 21 12"/>
</g>
```

Read: `<animate>` on any attribute, `<animateTransform>` for `rotate`, `translate` and
`scale`, and `<set>` for a step change. Timing: `dur`, `begin`, `repeatCount` (a number or
`indefinite`), `values` with `keyTimes`, `from`/`to`/`by`, `calcMode="discrete"`, and
`fill="freeze"`. Numbers and lists of numbers are interpolated componentwise, colours per
channel, and anything else steps — because there is no half way between two keywords.

`stroke-dasharray` and `stroke-dashoffset` are supported, which is what makes an icon draw
itself: one dash as long as the whole line, with the offset animated to nothing.

**The loop is rasterised once**, at the size the widget was given, into thirty frames a
second — so playing it is picking a texture, and an animated icon costs what a still one
costs. Thirty frames of a 24px icon is about seventy kilobytes. A loop is capped at four
seconds' worth of frames; a longer one plays slower rather than using memory nobody asked
for.

An icon part-way through its loop keeps the window drawing, the same way a colour still
easing does. When it is the only thing moving and it stops, the window goes idle again.

Not read: `<animateMotion>` along a path, and anything CSS-driven. Those are in the format
but not in the icon sets that export SMIL.

### A picture the backend made

A vision model hands back bytes, a plot is rendered into a buffer, a frame arrives from a
camera. Registering it gives it a name:

```python
rda.define_image("answer", png_bytes)          # PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PNM
rda.define_image_pixels("plot", rgba, 320, 240)  # four bytes a pixel, rows packed
```

```tsx
<image id="shown" src="mem:answer" width={320} height={240} />
```

Node is `defineImage` / `defineImagePixels` / `forgetImage`; C# is `DefineImage` /
`DefineImagePixels` / `ForgetImage`; C++ calls `RDA::images().define(...)` directly.

The prefix is explicit on purpose: a bare name that also happened to be a file on disk
would be an ambiguity resolved silently, and this way what a layout means is visible in the
layout.

Unlike a file, a registered picture is owned by the **engine**, not by the widget. Two
`<image>`s naming one share a texture, and registering the same name again replaces what
both of them draw — which is what makes it the right shape for a preview that updates. It
reaches the screen on its own, with no click and no signal write, because the frame cache
is told a picture changed.

`src` is bindable, so one `<image>` can show a file now and a model's answer a moment
later. A name that has not been registered draws nothing and says so once; `forget_image`
puts it back to that.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
