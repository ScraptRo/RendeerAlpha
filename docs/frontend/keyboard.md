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

What holds the keyboard wears a ring — **but only when the keyboard is what put it
there.** Tab draws one; a click does not, and neither does a backend calling
`rda_set_focus`.

A press is its own feedback: you are looking at the thing you just clicked, and a box
around it answers a question you had already answered. On a widget that is *also* the
chosen one — a tab, a segmented control — it reads as a second kind of chosen-ness, which
is worse than nothing. Somebody navigating by Tab has none of that feedback, which is why
this is a rule about how the focus arrived rather than a switch that turns rings off.

It is themed once for the whole interface:

```ts
export const focus: RdaFocusTheme = {
  default: { color: "#6E9CF0", width: 2, inset: -3, show: "keyboard" },
}
```

| | |
| --- | --- |
| `show: "keyboard"` | only where Tab put the focus. The default |
| `show: "always"` | wherever the focus is, including after a click |
| `width: 0` | no ring at all |

`inset` is negative to sit outside the widget's own edge.

## Doing

| Widget | Keys |
| --- | --- |
| `<button>` | **Space** or **Enter** presses it |
| `<checkbox>` | **Space** or **Enter** toggles it |
| `<slider>` | **Left/Right** or **Up/Down** move it a twentieth of its range |
| `<select>` | **Enter** opens it; then the arrows move the highlight, **Enter** takes it, **Escape** leaves without changing anything. **Typing** the first letters of a choice opens it there — see below |
| `<tree>` | **Up/Down** move the choice, **Home/End** go to the ends, **Right** opens a node and then steps in, **Left** shuts it and then steps out, **Space** opens or shuts, **Enter** activates |
| `<grid>` | **Up/Down** move the choice, **Page Up/Page Down** a screen at a time, **Home/End** to the ends, **Enter** activates |
| `<grid>` or `<tree>` with `multiSelect` | **Shift** with any of those reaches from the last row clicked, **Ctrl** with them moves without choosing, **Ctrl+Space** chooses or unchooses, **Ctrl+A** chooses every row — see [Choosing several](elements/list.md#choosing-several) |
| `<textfield>` | typing, the usual editing keys, and **Escape** to leave the field. **Enter** sends from a single line and **Ctrl+Enter** from a multi-line one, unless `submitKey` says otherwise — and **Shift+Enter** is the line break wherever Enter sends |

Inside a field, the shortcuts are the ones every other field on the machine has:

| | |
| --- | --- |
| **Ctrl+C** / **Ctrl+X** / **Ctrl+V** | copy, cut, paste. Copy works on a read-only field too |
| **Ctrl+A** | select everything |
| **Ctrl+Z** | undo |
| **Ctrl+Y**, **Ctrl+Shift+Z** | redo |

## Chinese, Japanese and Korean

Typed through the system's input method, the way they are in any other application: letters
go in, candidates come up, a choice goes into the field. While a word is being composed it
is drawn at the caret, underlined, with the method's own cursor inside it — it is not in
the field's text yet and nothing hears about it until it is chosen. The method's list of
candidates opens under the caret.

The method is on only while a text field has the keyboard. Over a button or a list it is
off, so a shortcut's letter is a shortcut rather than the start of a word, and the keys a
composition is using — Backspace, Enter, the arrows — are the method's and never reach the
interface while it is composing.

That is Windows. On Linux under X11 the input method works through GLFW and its own window:
finished text arrives, and the composition is shown by the method rather than at the caret.
Under Wayland GLFW does not take input-method text at all yet.

`rda shot --compose にほんご` pictures a field part way through a word, for checking what
that looks like without a keyboard set up for it.

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
arrow moves from there rather than from the top.

Typing finds a choice by how it begins, open or shut. `bud` goes to Budapest; the same
letter again and again — `b`, `b`, `b` — steps through everything beginning with B, which is
how a long list of names is walked by its initial. Shut, it opens on the match rather than
choosing it, so nothing changes until Enter, and Escape still changes nothing. A pause of
about a second starts a new search. Letters are matched without regard to case, for the
unaccented Latin ones. The highlight it moves looks exactly like
the one the pointer makes: there is one highlight in a list, however the reader is moving
it.

Two keys mean something else in one place. A `<textfield>` in `mode="code"` indents with
**Tab** instead of moving on, and says so, so the focus does not also move. **Escape** in
any field gives the keyboard back rather than leaving the application — a field swallows
every other key, so it owes the reader a way out that is not the mouse.

## Shortcuts of your own

A button can be pressed with a chord as well as with a click:

```tsx
<button id="save" text="Save" shortcut="Ctrl+S" onClick={() => commands.save()} />
```

Ctrl+S runs the same `onClick` a click runs, which is the same one Space runs when the
button has the keyboard. A checkbox takes one too, and the chord toggles it. Nothing is
declared twice: the menu entry and its shortcut are one widget, so they cannot drift apart.

For a chord that belongs to no button, `<shortcut>` is a chord and a handler with nothing
on screen:

```tsx
<shortcut id="palette" keys="Ctrl+K" onPress={() => state.palette = true} />
<shortcut id="refresh" keys="F5"     onPress={() => commands.refresh()} />
```

It takes no room. Where it is written is where it applies: one inside a screen works while
that screen is showing, and one inside a `<tab>` while that page is.

### How a chord is written

Modifiers, then a key, joined by `+`: `"Ctrl+S"`, `"Ctrl+Shift+P"`, `"Alt+Left"`, `"F5"`,
`"Delete"`. The modifiers are `Ctrl`, `Alt`, `Shift` and `Super`; `Mod` means Ctrl, which is
the word to use for "the key this platform's shortcuts are made with". The key is a capital
letter, a digit, `F1` to `F24`, a named key — `Enter`, `Escape`, `Space`, `Backspace`,
`Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right` — or
punctuation written as itself, with `Plus` for `+`, since `+` is what joins the parts.

The generated `rda.d.ts` knows all of that, so `"Ctrl+Shfit+S"` is a red underline that
asks whether you meant `"Ctrl+Shift+S"`. `Tab` and `Shift+Tab` on their own are refused:
they are how the keyboard reaches every widget there is. `Ctrl+Tab` is fine.

A chord may be a binding, which is how a keymap the reader can change works — keep it in
state, and the button reads it:

```tsx
<button id="save" text="Save" shortcut={() => state.saveKey} onClick={...} />
```

A bound chord is a string nothing can check at build time; one that does not parse is
named in the log, with the widget it was on, and leaves that widget with no shortcut.

Keys are named by where they are on a US keyboard, which is what the window system reports.
On a French AZERTY keyboard `"Ctrl+Z"` is the key with W printed on it.

### When it is live

A shortcut works while its widget could be reached:

- **drawn**, or **scrolled out of view** — Ctrl+S does not depend on the scroll bar;
- **inside a `<popup>` that is shut** — a menu entry's shortcut has to work with the menu
  closed, which is most of what it is for.

And not when it is:

- **hidden** with `visible={false}`, or inside something that is;
- **on a `<tab>` that is not showing** — it would act on something nobody can see;
- **behind a blocking popup that is open**. The interface behind a menu or a dialog is
  behind it for the keyboard as well as the pointer, so Delete in a dialog does not also
  delete the row the dialog is about. What is *in* the popup still works;
- **in a screen on its way out** during a route's cross-fade;
- **in a `<list>` row.** A row is a pooled copy showing whichever row scrolled into it.

If two live widgets have the same chord, the one drawn last takes it — an open menu over
the interface behind it — and the log says so once, naming both.

### Who hears a key first

Whatever has the keyboard. A focused text field keeps every key with no modifier, because
those are characters, arrows, Delete and Escape, and it keeps the Ctrl chords it edits
with: Ctrl+C, X, V, A, Z, Y, Ctrl+Shift+Z, Ctrl with an arrow, Home, End, Backspace or
Delete, and Ctrl+Enter. Everything else — Ctrl+S, Ctrl+K, F5, Alt+anything — goes past the
field to the shortcut. So a `<shortcut keys="Delete">` deletes the selected row, and stops
while the reader is typing in the search box.

Anything else with the keyboard — a button, a slider, an open dropdown — keeps only the
plain keys it acts on: Space, Enter, the arrows and Escape. A `<grid>` or a `<tree>` also
keeps Home, End, Page Up and Page Down, which move through it, and with `multiSelect` those
and the arrows with Shift or Ctrl held, Ctrl+Space and Ctrl+A.

## What a backend sees

Nothing new. A keyboard press runs the same handler a click runs: `onClick` fires whether
the button was pressed with the mouse or with Space, and `onChange` reports the same new
value whether a slider was dragged or arrowed, and a shortcut runs it too. There is no
separate keyboard event, which is the point — an interface that answers the keyboard should not be a second interface.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
