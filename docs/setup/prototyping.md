# Seeing it before there is anything behind it

```bash
rda prototype .
```

A window, with your interface in it, and no backend at all.

The window is `rda-preview`'s: `rda` hands this command, and `rda shot`, to the program of
that name beside it, with the whole line and everything it printed and returned passed
through. It is a program of its own because it is the part of the toolchain that needs Vulkan
and a display; `rda`, which compiles, needs neither. `bin/` has both, and a `rda` copied
somewhere without it will say that it is missing. Typing `rda-preview prototype .` is the
same thing.

An interface is written against `state`, `tables` and `commands` from `res/state.ts`, and
normally a backend supplies all three: it defines the signals, fills the rows, answers the
commands. That is the right arrangement and it is also the thing standing between you and
the screen you are designing — a `<list>` with nothing in it is an empty box, and a detail
page with no signal set is a page of zeroes.

So this defines everything the declaration promises and puts placeholder contents in it.
Editing a `.tsx` rebuilds it in place, the same hot reload an application gets.

## What it does

| | |
| --- | --- |
| compiles the project | the same step `rda build` runs, so you are looking at what you just wrote |
| defines every signal | at the value it was declared with — where the real thing starts |
| defines every table | with its columns, and no rows until you ask for some |
| binds every command | to a line on the terminal saying which one the interface asked for |
| opens the window | on the first declared route, or on the only layout there is |

Nothing computes, validates or persists. It is not a mock framework and it will not
become your backend.

## Putting something in it

Three ways, and they stack — a file first, then generated rows, then single values, so
the last word goes to the shortest thing you typed.

**One value.** Read as the signal's declared type, so nothing has to say which it is:

```bash
rda prototype . --set count=99 --set text_box="a line somebody typed"
```

**Rows, by the yard.** When what you need is to see how the screen behaves with fifty of
something:

```bash
rda prototype . --rows products=50
```

Numbers count up, text takes the column's name and the row's number, booleans alternate.
It is deliberately obvious placeholder data: data that looked real would be data somebody
eventually mistakes for real.

**A screenful, from a file.** Keys are names the declaration already knows — a signal
name sets it, a table name fills it from an array of rows:

```json
{
  "count": 7,
  "productCount": 4,
  "products": [
    { "title": "Brass hinge",  "price": 4,  "inStock": true  },
    { "title": "Cast bracket", "price": 18, "inStock": false }
  ]
}
```

```bash
rda prototype . --data mock.json
```

A key naming neither a signal nor a table is refused rather than skipped. A misspelling
in a mock file is a screen that stays empty for a reason nobody can see, and finding out
at the terminal beats staring at the window.

## Which screen

With routes declared, all of them work — the navigation is the application's own — and
`--layout` picks which to open on:

```bash
rda prototype . --layout catalogue
```

With no routes, `--layout` names the file: `--layout home` shows `res/layouts/home.rdab`.
It is only needed when there is more than one to choose from.

## The rest

| | |
| --- | --- |
| `--no-build` | show what is already compiled; do not compile first |
| pressing a button | prints `rda: commands.save()`, so you can see what it asked for |
| editing a `.tsx` | rebuilds in place, without losing the state you set |

Assets resolve relative to the project, exactly as they will when the application runs —
the tool works from the project directory rather than rewriting paths it does not own.

Nobody at the machine, or two screens to compare? [`rda shot`](seeing-it.md) is this same
command with no window and a `.png` at the end.

## Editing in the window

Mark a container `editable` and the window lets you build inside it with the mouse.

```tsx
<stack id="sidebar" arrange="vertical" spacing={8} editable>
  <label id="title" height="content" text="Settings" />
</stack>
```

`editable` works on `<container>`, `<panel>`, `<stack>` and `<scroll>`. With one in the layout,
the interface gets an outline around it, and a second window opens: the editor's panel, which
stays on top and can go anywhere, off the interface or onto another screen.

| | |
| --- | --- |
| drag a widget from the panel into an outline | adds it. In a stack it goes between the children you let go of it between. In a panel or a container it goes where you let go, with a position and a size |
| or click a widget in the panel, then click in an outline | the same, and Esc puts it back. On Wayland, which does not tell a program where its windows are, this is the way |
| click a widget inside an outline | selects it, and its properties appear in the panel |
| type in a property | saves on Enter, or when you click away. Clearing it takes the property out of the file |
| click a choice | moves to the next word; after the last it goes back to the default |
| **Remove from the file** | takes the selected widget out |
| F2, or closing the panel | hides all of it, so you can try what you built. F2 brings it back |

While the editor is showing the interface takes no clicks: a click selects, and pressing a
button does nothing until F2 has hidden the overlay.

Everything goes straight into the `.tsx`, and hot reload is what puts it on the screen, so there
is nothing to save and nothing to lose by closing the window. An edit changes only what it has
to. Comments, blank lines and the way you broke a long tag stay as they were, so the file can be
open in an editor beside the window, and a save from either side is picked up by the other.

A few things are left to you, in the file:

- A value worked out from state, like `text={() => state.title}`, is shown and cannot be typed
  over, because typing over it would lose the expression. Events are code, and are not shown.
- A widget whose id is worked out by code cannot be found in the file, so it cannot be edited.
- A layout written as [objects](../frontend/objects.md) has no JSX to change, and says so.

`editable` means nothing outside `rda prototype`. An application reads it and ignores it, so it
can stay in a layout that ships.

---

Back to [setup](README.md) · [all documentation](../README.md)
