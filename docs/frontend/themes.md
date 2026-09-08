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
