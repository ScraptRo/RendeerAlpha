# `res/themes/app.ts` — the look

A theme is a set of exported objects. Each export names a **widget type**; each key
inside it is a **variant**, chosen by a widget with `variant="..."`.

```ts
export const button: RdaButtonTheme = {
  default: { radius: 5, transitionMs: 130, easing: "out" },
  primary: { normal: "#3A6AD0", hovered: "#4C7CE6", pressed: "#2A54B4", text: "#FFFFFF" },
  quiet:   { normal: "#222834", hovered: "#2C3442", text: "#DCE0E7" },
}

export const label: RdaLabelTheme = {
  default: { color: "#DCE0E7" },
  heading: { color: "#FFFFFF", fontSize: 22 },
  caption: { color: "#89929F", fontSize: 12 },
}
```

```tsx
<button id="go" variant="primary" text="Save" />
<label id="t" variant="heading" text="Files" />
```

**Exactly nine exports are read**, and a misspelling is a build error with a
suggestion (`nothing is styled by 'buton'. Did you mean 'button'?`):

`button` `checkbox` `slider` `panel` `label` `textfield` `dock` `focus` `background`

Anything else in the file — a palette object, a helper — is ignored by the compiler and
perfectly fine to write.

## Which export styles which element

| Export | Elements that take its variants |
| --- | --- |
| `button` | `<button>`, `<tabs>`, `<select>` |
| `label` | `<label>` |
| `panel` | `<panel>`, and the body of a dropdown |
| `textfield` | `<textfield>`, `<scroll>`, `<list>` |
| `checkbox` | `<checkbox>` |
| `slider` | `<slider>` |
| `dock` | `<dockspace>`, `<dock>`, `<splitter>` |
| `focus` | the ring around whatever holds the keyboard, on every widget |
| `background` | what is behind the whole interface — the window itself |

## Inheriting

A field left out inherits from that widget's `default` variant, and every default is the
engine's. **Write only the decisions you are making.** `base: "primary"` starts a variant
from another one instead.

## Fields, by export

Colours are `"#RGB"`, `"#RRGGBB"` or `"#RRGGBBAA"`.

**`button`** — `normal` `hovered` `pressed` `text` `border` `borderWidth` `radius`
`fontSize` `weight` `transitionMs` `easing` `base`

**`label`** — `color` `fontSize` `weight` `base`

**`panel`** — `body` `accent` `accentHeight` `border` `borderWidth` `radius`
`transitionMs` `easing` `base`

**`background`** — `color`. What the window starts each frame at, behind every widget.
It is a theme export rather than a startup setting because it is a look like any other:
one line in this file, reloaded with the rest, and the same for a backend in any language.

```ts
export const background: RdaBackgroundTheme = {
  default: { color: "#0E1016" },
}
```

**`focus`** — `color` `width` `inset`. One style for every widget: it answers a question
about the keyboard rather than about the widget, so the same mark appears each time.
`inset` is negative to sit outside the widget's own edge.

**`checkbox`** — `box` `boxHover` `check` `label` `border` `borderWidth` `radius`
`checkInset` `fontSize` `weight` `transitionMs` `easing` `base`

**`slider`** — `track` `fill` `knob` `knobActive` `knobWidth` `radius` `transitionMs`
`easing` `base`

**`textfield`** — `mode` `background` `text` `caret` `selection` `gutter` `lineNumber`
`currentLine` `border` `scrollTrack` `scrollThumb` `scrollThumbHover` `padding`
`borderWidth` `radius` `caretWidth` `readOnly` `multiline` `showLineNumbers`
`highlightCurrentLine` `language` `syntax` `transitionMs` `easing` `base`

**`dock`** — `pane` `tabStrip` `tab` `tabActive` `tabText` `titleBar` `titleBarActive`
`close` `closeHover` `grip` `splitter` `splitterHover` `dropBand` `dropBandHot`
`dropPane` `dropPreview` `tabHeight` `tabPadding` `closeWidth` `radius` `transitionMs`
`easing` `base`

**`syntax`**, inside a `textfield` variant — `plain` `keyword` `type` `string` `number`
`comment` `operator` `function` `preprocessor`

A misspelled field is a build error naming the variant:
``` `button.primary` has no field called `borderWith`. Did you mean `borderWidth`? ```

## Motion

`transitionMs` on a variant is how long its colours take to follow hover and press; 0 is
instant. `easing` is `out` (the default), `in`, `inOut` or `linear`. Putting
`transitionMs` on the `default` variant is what makes every button of that kind feel
answered rather than switched.

**`radius` travels too.** A widget whose variant changes -- and a variant may be a
binding -- becomes a different shape as well as a different colour, and both take the
same `transitionMs` to get there. A panel has motion for exactly this reason: it reacts
to nothing on its own, but `variant={() => state.selected ? "on" : "off"}` is an
ordinary thing to write.

Everything else is the layout's `animate={ms}`, which moves widgets rather than
recolours them, and is inherited by children.

## Font sizes

`fontSize` must be **one of the sizes baked into the atlas at startup**: by default 12,
15, 18, 22, 28, 36. A size that was not baked is drawn at the nearest one that was, and
the log says so, naming both. An application can change the list in its startup config.

`weight: "bold"` is synthesised and does not change metrics. There is one font face.

## Colour helpers

Available in a theme without importing anything. They run while the theme is compiled;
what ships is the string one of them returned.

| | |
| --- | --- |
| `rgb(r, g, b)` `rgba(r, g, b, a)` | components 0–255, alpha 0–1 |
| `hsl(h, s, l)` | hue in degrees, the rest 0–1 |
| `fade(colour, alpha)` | the same colour at a different opacity |
| `mix(a, b, t)` | blend; `t` of 0 is `a` |
| `lighten(c, amount)` `darken(c, amount)` | toward white, toward black |
| `alphaOf(colour)` | its alpha, 0–1 |

```ts
const brand = "#3A6AD0"

export const button: RdaButtonTheme = {
  primary: { normal: brand, hovered: lighten(brand, 0.12), pressed: darken(brand, 0.15) },
}
```

## Changing it at run time

`rda.set_theme(path)` (Node `setTheme`, C# `SetTheme`, C++
`gui().theme().replaceWithFile`). It **replaces**: a variant the new theme does not name
goes back to the engine's default, not to the old theme's. Colours and radii ease to their
new values over the new theme's `transitionMs`, so a swap is a transition rather than a
jump; give `background` a `transitionMs` too, or the ground changes in one frame while
everything on it is still moving. A file that will not load changes nothing and says why
in the log.

## Loading it

The compiled `.rdth` sits beside its source. The backend names it at startup:
`config.theme = "res/themes/app.rdth"`. Leave it empty for the built-in look.
