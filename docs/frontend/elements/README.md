# Every element

Twenty-seven of them. Each takes [the common properties](common-properties.md) as well as
its own.

Every `text` property is UTF-8, from the `.tsx` through the blueprint to the screen, and
so is everything a `<textfield>` receives from the keyboard. The atlas is baked with Latin
— Basic Latin, Latin-1 Supplement, Latin Extended-A, the Romanian comma-below letters, the
usual punctuation and the euro — and anything outside that is baked the first time it is
drawn, from the first face in `config.font` that has it, or failing that from the system's
own, which is how Chinese, Japanese and Korean draw with nothing named. A codepoint no face
has draws as a hollow box, which is a thing you can see, rather than as blank space. See
[themes](../themes.md) for naming fallback faces.

| | |
| --- | --- |
| [Containers](containers.md) | `<container>`, `<panel>`, `<stack>`, `<scroll>`, `<splitter>`, `<popup>` |
| [Text and input](text-and-input.md) | `<label>`, `<button>`, `<checkbox>`, `<slider>`, `<textfield>`, `<number>` |
| [Keys](../keyboard.md#shortcuts-of-your-own) | `<shortcut>` — a key chord and what it does, with nothing on screen |
| [Choices and pages](choices-and-pages.md) | `<select>` and `<option>`, `<tabs>` and `<tab>`, `<image>` |
| [Large data](list.md) | `<list>` — rows from a declared table, through a pool of widgets; `<tree>` — the same, nested by a depth column; `<grid>` and `<column>` — a table as columns under titles, sorted by clicking one |
| [Live pictures](stream.md) | `<stream>` — a picture the backend replaces, frame after frame |
| [Docking](docking.md) | `<dockspace>`, `<dock>`, `<header>` |
| [The viewport](viewport.md) | `<viewport>` — a hole the application fills, with Vulkan or with 2D commands |
| [The common properties](common-properties.md) | `id`, `visible`, `animate`, sizing, `hAlignSelf` / `vAlignSelf`, placement |

---

Back to [the frontend index](../README.md) · [all documentation](../../README.md)
