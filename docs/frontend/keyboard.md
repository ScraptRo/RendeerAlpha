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
| `<textfield>` | typing, the usual editing keys, and **Escape** to leave the field. **Enter** sends from a single line and **Ctrl+Enter** from a multi-line one, unless `submitKey` says otherwise — and **Shift+Enter** is the line break wherever Enter sends |

Inside a field, the shortcuts are the ones every other field on the machine has:

| | |
| --- | --- |
| **Ctrl+C** / **Ctrl+X** / **Ctrl+V** | copy, cut, paste. Copy works on a read-only field too |
| **Ctrl+A** | select everything |
| **Ctrl+Z** | undo |
| **Ctrl+Y**, **Ctrl+Shift+Z** | redo |

## Taking something back

Undo is per field and needs nothing from the layout or the backend. A run of edits of the
same kind, made without a pause, is **one** step: typing a word and pressing Ctrl+Z once
leaves the word gone rather than its last letter. A pause of about three quarters of a
second, a different kind of edit, or the caret being moved by hand ends the run and starts
a new step.

A step carries the caret as well as the text, so undoing puts the reader back where they
were, not just back to what they had. Anything typed after an undo throws away what was
undone, the way it does everywhere else.

Each field remembers up to a hundred steps or a megabyte of text, whichever comes first,
and then forgets the oldest. A field's history belongs to that field and lives as long as
the interface does; nothing about it crosses to the backend, and a signal written from
outside the field is not something undo can take back — it was not an edit.

A read-only field has no history, because it has nothing to undo.

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
