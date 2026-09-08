# The keyboard

Everything that does something when you press it can be reached without a pointer. There
is nothing to declare: a widget that takes a click takes the keyboard, and the order is
the order it was drawn.

## Moving

| | |
| --- | --- |
| **Tab** | the next one, in the order they appear |
| **Shift+Tab** | the previous one |
| a click | points the keyboard at what was clicked |
| a click on nothing | takes the keyboard away |

The order is not a property you set. It is the order the widgets were drawn, which is the
order they are on screen, so a tab index cannot disagree with what the reader sees. A
widget scrolled out of view is not drawn, so Tab does not visit it.

What holds the keyboard wears a ring, themed once for the whole interface:

```ts
export const focus: RdaFocusTheme = {
  default: { color: "#6E9CF0", width: 2, inset: -3 },
}
```

`inset` is negative to sit outside the widget's own edge. `width: 0` turns the ring off.

## Doing

| Widget | Keys |
| --- | --- |
| `<button>` | **Space** or **Enter** presses it |
| `<checkbox>` | **Space** or **Enter** toggles it |
| `<slider>` | **Left/Right** or **Up/Down** move it a twentieth of its range |
| `<select>` | **Enter** opens it; then the arrows move the highlight, **Enter** takes it, **Escape** leaves without changing anything |
| `<textfield>` | typing, the usual editing keys, and **Escape** to leave the field |

A dropdown opened from the keyboard starts on whatever is already chosen, so the first
arrow moves from there rather than from the top. The highlight it moves looks exactly like
the one the pointer makes: there is one highlight in a list, however the reader is moving
it.

Two keys mean something else in one place. A `<textfield>` in `mode="code"` indents with
**Tab** instead of moving on, and says so, so the focus does not also move. **Escape** in
any field gives the keyboard back rather than leaving the application — a field swallows
every other key, so it owes the reader a way out that is not the mouse.

## What a backend sees

Nothing new. A keyboard press runs the same handler a click runs: `onClick` fires whether
the button was pressed with the mouse or with Space, and `onChange` reports the same new
value whether a slider was dragged or arrowed. There is no separate keyboard event, which
is the point — an interface that answers the keyboard should not be a second interface.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
