# Text and input

## `<label>`
A line of text.

| | |
| --- | --- |
| `text` | what it says |
| `wrap` | break to its width instead of running past. Off by default |
| `spans` | runs drawn in their own colour -- see below |
| `hAlign`, `vAlign` | where the text sits in its box |
| `variant` | which label style from the theme |

`wrap` is off by default because a caption that silently became three lines tall would
move everything under it, and most labels are one line on purpose. Wrapped, it breaks to
the width it was *given* rather than the width it asked for, and each line is aligned on
its own.

## A label in more than one colour

`spans` colours runs of the text without splitting it into several labels:

```tsx
<label id="line" text={() => item.code} spans={() => item.colours} />
```

The value is `start:length:colour` triples separated by `;`:

```
0:3:#C678DD;4:5:#61AFEF;20:28:#5C6370
```

`start` and `length` are **bytes**, which is what everything else here indexes text by --
the caret a field reports, the ranges a wrap returns. For ASCII, which is most of what a
highlighter deals in, they are the same as character counts. In Python that means
`text.encode("utf-8")` offsets; in Node, `Buffer.byteLength`.

It is an ordinary bindable string, so a `<list>` row carries its own colouring in a column:
a diff, a search highlight or syntax colour inline needs no new mechanism. Anything no span
covers is drawn in the label's own colour, and a label with no `spans` is exactly what it
was.

A run that crosses a wrap is drawn in its colour on both lines; you do not have to know
where the breaks fell.

**Nothing malformed is fatal.** This arrives from a backend, sometimes a character at a
time while a model is still writing, so a half-written triple, a colour that is not one, or
a zero length is skipped and the rest of the line still colours. A span reaching past the
end is clipped to it, a negative start is pulled to zero, and overlapping spans are
resolved in favour of whichever starts first.

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
| `submitKey` | which keystroke runs `onSubmit` — see below |
| `language` | the syntax grammar to colour with, overriding the variant's |
| `variant` | which textfield style |
| `onChange(string)` | runs on each edit |
| `onSubmit(string)` | the send keystroke, with the contents as they stand |

`onSubmit` is what a search box, a chat composer or a one-line form wants: the reader
finishes by pressing a key rather than by reaching for a button. It carries the text so a
handler does not have to read the signal back, and it does not clear the field — clear it
yourself in the handler if that is what finishing means here.

### Which key sends

Say nothing and a field sends the way its mode implies: **Enter** on a single line, which
has nowhere to put a newline anyway, and **Ctrl+Enter** on a `"document"` or `"code"`
field, where Enter is a line break.

`submitKey` says otherwise.

| | |
| --- | --- |
| `"enter"` | Enter sends. **Shift+Enter** is the line break |
| `"ctrlEnter"` | Ctrl+Enter sends; Enter is a line break |
| `"both"` | either sends |
| `"none"` | neither; `onSubmit` never fires |

```tsx
<textfield id="composer" mode="document" submitKey="enter"
           text={() => state.draft}
           onChange={(v) => state.draft = v}
           onSubmit={() => commands.send()} />
```

That is the combination the modes alone could not express, and it is what a chat composer
is: several lines, pasted text that keeps its newlines, and Enter still meaning send.
**Shift+Enter is the line break wherever Enter sends** — it is the gesture everybody
already has in their fingers, and a field that took it away would be the odd one out.
Sending does not move the keyboard on, so the reader can type the next message straight
away; a single-line field still gives the keyboard back on Enter, the way it always has.

### Which grammar it colours with

`"code"` gives it a gutter and syntax highlighting. The palette comes from the theme
variant and the grammar from `language` on the element, so one `code` variant serves
however many languages an application shows — a variant per grammar was the only way to
do this before, and it meant an application could not colour a language its theme had
never heard of. A variant may still name a `language`; the element's wins.

Either way, asking for a language that was never loaded says so in the log rather than
quietly drawing plain text — see `GuiConfig::languagesPath`.

## Completion, ahead of the caret

A `<textfield>` can show a suggestion the reader has not typed:

```tsx
<textfield id="editor" mode="code"
           text={() => state.code}
           suggestion={() => state.hint}
           onChange={(v) => { state.code = v }}
           onCaret={(at) => { state.caretAt = at }}
           onAccept={() => { state.hint = "" }}
           onDismiss={() => { state.hint = "" }} />
```

| | |
| --- | --- |
| `suggestion` | drawn after the caret in the `suggestion` colour. **Never part of `text`** |
| `onCaret` | where the caret is, in bytes, whenever it moves or the text changes |
| `onAccept` | **Tab** took it — by then it is in `text`, through an ordinary edit |
| `onDismiss` | **Escape** dropped it, and the text is untouched |

The suggestion is not in the value, so it cannot be selected, copied, or entered into the
undo history. Tab and Escape are handled inside the field: an application never sees a
keystroke, it sees that its offer was taken.

**Clear the signal in the handler.** The engine drops its own copy the moment either key
answers it — that is what stops the ghost text flickering for a frame — but a bound
`suggestion` is re-applied from the signal it reads, so a suggestion that is not cleared
comes straight back. `onAccept` and `onDismiss` exist to be where you clear it.

Both keys keep their old meanings when nothing is on offer — Tab indents a `mode="code"`
field, Escape gives the keyboard back.

One line only. A multi-line suggestion would overdraw whatever is below the field, and
reserving height for text that is not in the value is a layout problem rather than a
completion one; a suggestion containing a newline is cut at it.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
