# Themes and variants

Every widget that draws takes `variant="name"`, resolved against the compiled theme.

```tsx
<button variant="primary" text="Save" />
<label  variant="caption" text="a quieter line" />
```

A theme is TypeScript too, compiled to a `.rdth`:

```ts
export const button: RdaButtonTheme = {
  default: { radius: 5, transitionMs: 130, easing: "out" },
  primary: { normal: "#3A6AD0", hovered: "#4C7CE6", text: "#FFFFFF" },
  danger:  { ...primary, normal: "#B23A3A" },
}
```

Every field left out inherits from that widget's `default` variant, so an entry is only
the decisions it actually makes. `transitionMs` and `easing` say how long a widget's
appearance takes to follow what is happening to it, which is the difference between a
state change and a response.

They cover the **colours and the corner radius**. A variant may be a binding, so a widget
that becomes the selected one can be a different shape as well as a different colour, and
both arrive over the same time. `<panel>` has them for that reason alone: it reacts to
nothing on its own, but `variant={() => state.selected ? "on" : "off"}` is ordinary.

Two exports are not widgets. `focus` styles the ring drawn around whatever holds the
keyboard — `color`, `width`, and an `inset` that is negative to sit outside the widget's
own edge. One style for all of them, because it answers a question about the keyboard
rather than about the widget.

`background` is the other, and it is one field:

```ts
export const background: RdaBackgroundTheme = {
  default: { color: "#0E1016" },
}
```

That is the colour the window starts each frame at, behind every widget — what shows
wherever the interface has not drawn anything. It lives here rather than in a startup
config because it is a look like any other: one line, reloaded with the rest of the theme,
and the same for a backend in any of the four languages without a call of its own.

## A colour is the colour you wrote

`#3A6AD0` in a theme comes off the screen as `#3A6AD0`. Sample a screenshot and you get
back the byte you typed, for every colour in a theme, a layout, an `<image>` tint and a
drawing sent over the C ABI.

That is worth stating because it was not true before. The engine wrote a theme's colour
into an sRGB surface without decoding it first, so the hardware encoded something that was
already encoded: `#808080` reached the screen as `#BCBCBC`, `#1A1F29` as `#5A6270`, and
every dark theme came out washed. `#000` and `#FFF` were the only two that survived, which
is exactly why it went unnoticed.

**If you built a theme by eye against the old behaviour, it will now look darker** — it is
being shown as written for the first time. Pick the colours again against what you see, and
delete any helper that pre-compensated for the old encode; applying one now makes the same
mistake twice, in the other direction.

Alpha is unaffected: it is coverage, not light, and was never gamma-encoded. Semi-
transparent panels do change, though — blending now happens in linear light, which is what
blending physically is.

## Fonts, and what is drawn when one runs out

`config.font` names the body font. It may name **several, separated by `;`**:

```python
config.font = "res/fonts/CascadiaMono.ttf;C:/Windows/Fonts/seguisym.ttf"
```

The first is the body and decides the line metrics. The rest are asked, in order, only
about a codepoint the ones before them do not have — so a monospace interface keeps its
alphabet and still shows an emoji. A fallback that is not on this machine is skipped with
a line in the log; a body font that is missing is fatal, because there would be nothing
to draw with.

**Latin is baked at startup; everything else is baked the first time it is written.** The
preloaded set is the seven blocks in `GlyphRanges.h` — 349 codepoints — and a character
outside it is rasterised on demand into spare room in the atlas, at the size it is needed,
from the first face that has it. Nothing declares what an application will show, which
matters when the text comes from somewhere the application does not control.

What it cannot do: **colour emoji**. Faces like `seguiemj.ttf` are COLR/CPAL and would
rasterise as their base layer only, so the atlas is monochrome coverage and `seguisym.ttf`
is the face to point at. ZWJ sequences and skin-tone modifiers draw as their parts. When
no face has a codepoint at all it draws as a hollow box and the log names it once.

## Changing it while the window is open

```python
rda.set_theme("res/themes/warm.rdth")     # Node: rda.setTheme(...)  C#: Rda.SetTheme(...)
```

C++ says `getMainWindow()->gui().theme().replaceWithFile(path)`.

**It replaces rather than layers.** A variant the new theme does not mention goes back to
the engine's built-in look, not to whatever the last theme said. That is what makes a
light theme a light theme instead of a hybrid of the two files — and it means a theme
meant to be switched to should say everything it cares about.

**And it travels.** Every colour and every radius the new theme moves eases there over the
`transitionMs` the *new* theme gives it, because a widget's colour was already an animated
value and this only changes where it is heading. A theme whose variants say 0 swaps in one
frame. The `background` moves on its own `transitionMs`, so give it one or the ground under
a fading interface will change instantly while everything on it is still travelling.

If the file will not load, nothing changes — the interface keeps the theme it had rather
than falling back to the built-in look — and the reason is in the log.

The variant names in your `rda.d.ts` are generated from your theme file, so
`variant="primry"` is an error in the editor. `<panel>` is the exception — its variants
are typed as `string`, because a panel style may be defined from code as well.

Colour helpers — `rgb`, `rgba`, `hsl`, `fade`, `mix`, `lighten`, `darken`, `alphaOf` — are
available in a theme without importing anything. They run while the theme is compiled and
are gone afterwards; what reaches the blueprint is the string one of them returned.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
