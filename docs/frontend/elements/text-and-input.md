# Text and input

## `<label>`
A line of text.

| | |
| --- | --- |
| `text` | what it says |
| `wrap` | break to its width instead of running past. Off by default |
| `hAlign`, `vAlign` | where the text sits in its box |
| `variant` | which label style from the theme |

`wrap` is off by default because a caption that silently became three lines tall would
move everything under it, and most labels are one line on purpose. Wrapped, it breaks to
the width it was *given* rather than the width it asked for, and each line is aligned on
its own.

## `<button>`

| | |
| --- | --- |
| `text` | the label on it |
| `hAlign` | `start` / `center` (default) / `end` |
| `padding` | inset around its children, when it has any. 6 by default |
| `variant` | which button style |
| `onClick` | runs on each completed click |

### A button around something other than a string

A `<button>` may hold children, and then they are what it says:

```tsx
<button id="save" variant="primary" width={220} height={64} onClick={() => commands.save()}>
  <stack id="c" arrange="vertical" spacing={2} anchor="fill" vAlign="center">
    <label id="t" height="content" hAlign="center" variant="strong" text="Save" />
    <label id="s" height="content" hAlign="center" variant="caption" text="to the current folder" />
  </stack>
</button>
```

The children are laid out inside it the way a `<panel>`'s are, with `padding` between them
and its edge, and clipped to it. A button does not arrange them itself: put a `<stack>`
inside one and it arranges them, which is why an icon beside a label is a row and two
lines of text is a column.

`text` is not drawn while it has children — they replaced it.

**The children draw; the button takes the input.** A widget inside a button that would
otherwise be clickable is not, because the press belongs to the button. That is the same
answer HTML gives, and the only one that makes "the whole thing is one control" true.

## `<checkbox>`

| | |
| --- | --- |
| `label` | the text beside the box |
| `value` | ticked when true |
| `onChange(boolean)` | with the new state |

## `<slider>`

| | |
| --- | --- |
| `value`, `min`, `max` | where the knob is, and its range |
| `onChange(number)` | runs while it is dragged |

## `<textfield>`

| | |
| --- | --- |
| `text` | the contents |
| `mode` | `"line"` (default), `"document"`, or `"code"` |
| `variant` | which textfield style |
| `onChange(string)` | runs on each edit |

`"code"` gives it a gutter and syntax highlighting, from the language named in its theme
variant. A variant asking for a language that was never loaded says so in the log rather
than quietly drawing plain text — see `GuiConfig::languagesPath`.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
