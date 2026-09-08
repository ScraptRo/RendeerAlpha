# Every element

Nineteen of them. Each takes [the common properties](common-properties.md) as well as its
own.

Every `text` property is UTF-8, from the `.tsx` through the blueprint to the screen, and
so is everything a `<textfield>` receives from the keyboard. The atlas covers Latin —
Basic Latin, Latin-1 Supplement, Latin Extended-A, the Romanian comma-below letters, the
usual punctuation and the euro. Anything outside that draws as a hollow box, which is a
thing you can see, rather than as blank space.

| | |
| --- | --- |
| [Containers](containers.md) | `<container>`, `<panel>`, `<stack>`, `<scroll>`, `<splitter>` |
| [Text and input](text-and-input.md) | `<label>`, `<button>`, `<checkbox>`, `<slider>`, `<textfield>` |
| [Choices and pages](choices-and-pages.md) | `<select>` and `<option>`, `<tabs>` and `<tab>`, `<image>` |
| [Large data](list.md) | `<list>` — rows from a declared table, through a pool of widgets |
| [Docking](docking.md) | `<dockspace>`, `<dock>` |
| [The viewport](viewport.md) | `<viewport>` — a hole the application fills, with Vulkan or with 2D commands |
| [The common properties](common-properties.md) | `id`, `visible`, `animate`, sizing, `hAlignSelf` / `vAlignSelf`, placement |

---

Back to [the frontend index](../README.md) · [all documentation](../../README.md)
