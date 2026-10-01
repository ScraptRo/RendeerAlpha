# Seeing it before there is anything behind it

```bash
rda prototype .
```

A window, with your interface in it, and no backend at all.

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

---

Back to [setup](README.md) · [all documentation](../README.md)
