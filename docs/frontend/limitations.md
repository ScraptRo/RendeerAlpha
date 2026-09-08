# What the language deliberately does not have

Worth knowing up front, so you do not go looking:

- **No loops or lists of elements.** Repeating a widget over data is what `<list>` and a
  table are for; repeating it over a fixed set is what a component and three call sites
  are for.
- **No function calls in a binding**, other than a command in a handler. A computation
  belongs in the backend, which has a language for it.
- **No CSS-like cascade.** A widget's look comes from its variant, and `animate` is the
  only property that is inherited.
- **No layout that depends on itself.** A wrapped label's height depends on the width it
  was given, and that is as far as it goes: nothing measures what it is going to become.
- **No keyboard focus model beyond text fields.** A dropdown has no arrow keys, and
  nothing is tab-navigable.
- **One font face.** No second family and no real bold — `weight: "bold"` is drawn twice a
  pixel apart.
- **Latin text only.** Every string is UTF-8, and the atlas covers Basic Latin, Latin-1
  Supplement, Latin Extended-A, the Romanian comma-below letters, common punctuation and
  the euro — so `Șase limbi și un preț — Straße, mañana, Łódź, Győr, İstanbul` all draw,
  and can be typed into a `<textfield>`. Greek, Cyrillic, Hebrew, Arabic, CJK and emoji
  draw as a hollow box: they need shaping and bidirectional layout, not a wider range.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
