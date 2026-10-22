# Dragging and dropping

Two things arrive by being let go of: something from elsewhere in the interface, and files
from outside the application. Both are properties every element has, and both end in a
handler that is handed a string — so what was dropped reaches the backend the way
everything else does, as a write to state.

## Inside the interface

`drag` makes an element something you can pick up, and says what it carries. `onDrop`
makes an element somewhere it can land:

```tsx
<button id="a" text="Write the report" drag="task-17" dragKind="task" />

<panel id="done" accepts="task" onDrop={(id) => { state.moving = id; commands.finish() }}>
  …
</panel>
```

What travels is a **string** — an id, a path, whatever the backend can look the thing up
by. Not the widget, and not a row: the handler at the other end is handed a value, and a
value is what a backend can act on. In a `<list>` it is a binding like anything else, so
each row carries its own:

```tsx
<list id="tasks" of="tasks" row={(task) => (
  <panel id="row" drag={() => task.id} dragKind="task"> … </panel>
)} />
```

`dragKind` says what sort of thing it is, and `accepts` on a target lists the sorts it
takes, separated by `|`. A target with no `accepts` takes everything; one with a list takes
only what names a kind on it, so something with no `dragKind` lands only on targets that
take anything. A target that does not accept what is being carried is not a target for
that drag: it does not light up and nothing lands on it.

| | |
| --- | --- |
| `drag` | what this carries; empty is not draggable |
| `dragKind` | what sort of thing that is |
| `accepts` | as a target, the kinds it takes, `"task\|note"`; empty takes any |
| `onDrop(string)` | something it accepts was let go of over it, with what that carries |
| `onDragOver(boolean)` | a drag it would take arrived over it, or left |

### What happens while it travels

Nothing moves until the pointer has gone a few units with the button held. A press that
stays put is a click, and a click on a draggable card still opens it. Once it moves:

- **a figure follows the pointer**, the size of what was picked up, with its text on it —
  a button's text, or the first text inside a row — held where it was taken hold of;
- **the target under the pointer is outlined**, in the focus ring's colour, which is the
  colour every theme already has for "this one";
- **the click is cancelled.** A card that was carried somewhere does not also open;
- **Escape puts it back.** Nothing is dropped and nothing is told it was.

Let go of over nothing that takes it and nothing happens. Let go of over a target and its
`onDrop` runs once.

The deepest target under the pointer is the one — a card inside a column, both taking
tasks, takes the drop itself. The same goes for picking up: a card inside a draggable
column picks up the card. What is behind an open blocking popup cannot be dropped on, and
neither can anything in a screen on its way out.

A press that something else takes as a drag of its own stays that something's. A slider
inside a draggable card moves the slider; a text field inside one selects text.

### Showing where things may go

The outline is the engine's. Anything more is the layout's, from two things it can read:

```tsx
<panel id="done" accepts="task" onDrop={...}
       variant={() => state.rda.dragKind === "task" ? "dropzone" : "default"}
       onDragOver={(over) => state.overDone = over}>
```

`state.rda.dragging` is true while anything is being carried and `state.rda.dragKind` is
its kind, so every place a task could go can change before the pointer gets there.
`onDragOver` is the one under it, reported on the change, the way `onHover` is.

## Files from outside

```tsx
<panel id="attach" onFiles={(paths) => { state.attaching = paths; commands.attach() }}>
```

Files dragged in from the desktop or a file manager and let go of over an element. Their
paths arrive as one string, **one path per line**, in the order the system gave them. The
deepest element under the drop that asked hears it, so a drop zone inside a window that
also takes files gets the files dropped on it.

There is no hover for this half: the window system says nothing until the files are let
go of.

A backend still gets every drop through its own channel — `rda_poll_dropped_file` and
each package's equivalent — whether or not any element asked. `onFiles` is for an
interface where *where* they were dropped means something.

## Testing it

The probe has `drop("from", "onto")`, which hands onto's `onDrop` what from carries, and
refuses for the reasons a real drag would land nowhere. `rda shot` has `--drag X1,Y1,X2,Y2`
to carry something across, `--hold` to take the picture before it is let go of, and
`--files "a.txt;b.png"` for a drop from outside — see [seeing it](../setup/seeing-it.md).

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
