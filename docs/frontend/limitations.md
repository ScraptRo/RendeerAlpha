# What the language deliberately does not have

Worth knowing up front, so you do not go looking:

- **No loops or lists of elements.** Repeating a widget over data is what `<list>` and a
  table are for; repeating it over a fixed set is what a component and three call sites
  are for.
- **Code that runs reads; it does not reach.** A function a binding calls reads and writes
  signals and reads tables, and that is all of the world it has -- no files, no network,
  no `Intl` (`format` stands in for it, and timers run on the frame clock). See
  [functions](functions.md).
- **No CSS-like cascade.** A widget's look comes from its variant, and `animate` is the
  only property that is inherited.
- **No layout that depends on itself.** A wrapped label's height depends on the width it
  was given, and that is as far as it goes: nothing measures what it is going to become.
- **One body face, no real bold.** `config.font` names a body font and any number of
  fallbacks after a `;`, which the engine reaches for one codepoint at a time — but every
  label is drawn in the body face, and `weight: "bold"` is drawn twice a pixel apart
  rather than from a bold face. See [themes](themes.md).
- **No shaping and no right-to-left.** Every string is UTF-8. The atlas is baked with Basic
  Latin, Latin-1 Supplement, Latin Extended-A, the Romanian comma-below letters, common
  punctuation and the euro — so `Șase limbi și un preț — Straße, mañana, Łódź, Győr,
  İstanbul` all draw, and can be typed into a `<textfield>`. Anything outside those
  ranges is baked the first time something asks to draw it, so symbols, monochrome emoji
  and scripts drawn one glyph per codepoint — Greek, Cyrillic, CJK — draw as long as a
  named face or one of the system's holds them. A line with no space in it wraps between
  characters, which is how Chinese and Japanese wrap anyway.

  Hebrew and Arabic are the ones that do not draw correctly: they need shaping and
  bidirectional layout, which is a different job from a wider range of glyphs.
- **Input methods on Windows only.** Chinese, Japanese and Korean are typed through the
  system's input method on Windows, composed at the caret — see
  [the keyboard](keyboard.md#chinese-japanese-and-korean). On Linux under X11 the method
  works through its own window and hands over finished text; under Wayland it does not
  reach the window at all, because GLFW does not yet speak Wayland's text input.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
