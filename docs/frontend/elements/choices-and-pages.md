# Choices and pages

## `<select>`
A box that opens a list of `<option>` children.

| | |
| --- | --- |
| `value` | the chosen option's value |
| `placeholder` | shown when the value matches no option |
| `onChange(string)` | with the chosen value |

The open list is drawn after the whole tree has been walked, with the clip back at the
viewport, so it escapes whatever panel it lives in and sits over everything — which is the
one thing a dropdown has to do.

## `<option>`
`text` is what the row says; `value` is what choosing it sets. They are read where they
are, so a binding on an option's text is an ordinary binding on an ordinary widget.

## `<tabs>` and `<tab>`
A row of titles, and the one page whose title is selected.

| | |
| --- | --- |
| `value` | which page, counting from zero |
| `barHeight` | height of the title row |
| `onChange(number)` | with the index clicked |

`<tab>` takes a `title`; its children are the page. The pages that are not showing **still
exist and still hold their state** — a widget is a thing that persists, so leaving a tab
and coming back does not reset what is in it.

## `<image>`

| | |
| --- | --- |
| `src` | path, relative to the running program |
| `fit` | `"contain"` keeps its shape inside the box; `"stretch"` fills the box |

It loads its file the first time it paints and owns the texture, so the lifetime is the
widget's. Two images of one file are two textures — a UI has a handful of them, and a
cache outliving the tree is a cache somebody has to remember to invalidate.

---

Back to [the elements index](README.md) · [all documentation](../../README.md)
