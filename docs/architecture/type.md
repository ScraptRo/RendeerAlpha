# Type

Size and weight are theme fields, so a layout names a role and the theme decides what that
role looks like:

```ts
export const label = {
  title:   { color: "#F0F3F8", fontSize: 28, weight: "bold" },
  caption: { color: "#8A94A6", fontSize: 12 },
}
```

```tsx
<label variant="title" height="content" text="Type has a scale now" />
```

`fontSize` and `weight` work on `label`, `button` and `checkbox`. A style that says nothing
about either is drawn at the interface's base size, which is what everything was before
these existed.

## The sizes are baked, not scaled

A bitmap glyph drawn at a size it was not baked at is a blurry glyph, and text is most of
what a GUI draws. So every size an application uses is baked at startup — into **one**
atlas texture, stacked as bands — and text of every size still batches into a single draw.
The cost is atlas area, which is cheap; the cost it avoids is a draw call per size, which
is not.

Which sizes get baked is the application's to declare:

```cpp
config.gui.fontHeight = 18.0f;                       // the base
config.gui.fontSizes  = { 12, 15, 22, 28, 36 };      // the rest of the scale
```

A theme naming a size that was not baked is drawn at the nearest one that was, and says so
once in the log, naming both numbers. That is a real mistake — it silently draws at the
wrong size — so it is worth a line, and worth only one line rather than one per frame.

## Bold is synthesised

`weight: "bold"` draws each glyph twice, a pixel apart, scaled to the size. It is not a
real bold face and does not claim to be. What it buys: no second font file, so it works
with whatever font an application ships; and **the metrics do not change** — the second
pass bleeds a pixel to the right and that is all — which is what keeps a bold word in the
same column as a regular one in a monospaced font. A real second face can be added later
without `weight` having lied about what it meant.

## A character is not a byte

Text is UTF-8 everywhere, and the atlas is looked up by codepoint.

That sentence is unremarkable and the engine did not do it for most of its life. The atlas
baked ASCII 32..126, one glyph per byte, and anything else was replaced with a space. So
`é` — two bytes, both out of range — drew as two blank spaces. Correctly laid out. No
warning. Invisible to every test, because every test was written in English.

The C ABI had been carrying UTF-8 faithfully the whole time, and the binding tests asserted
it: `"éàü — non-ascii"` round-trips through Python, Node and C# byte for byte. That made
the gap *harder* to see rather than easier. The bytes arrived intact and the screen was
still wrong, because the transport and the renderer were fixed by different work at
different times and nothing joined them up.

**What is baked**, in `GraphicalSrc/GlyphRanges.h`: Basic Latin, Latin-1 Supplement, Latin
Extended-A, the two Romanian comma-below letter pairs that live outside it, a block of
General Punctuation (dashes, curly quotes, the ellipsis) and the euro. 349 codepoints, at
every baked size, in one 1024×1024 atlas — the same single draw as before.

Contiguous blocks rather than a set, because a TrueType packer wants ranges anyway and
because it makes "where is this glyph" a walk over seven entries rather than a hash lookup,
on a path that runs once per glyph per frame.

**What is not baked**: Greek, Cyrillic, Hebrew, Arabic, CJK, emoji. Those are not a wider
range — they are shaping, bidirectional layout, colour, and an atlas that cannot be baked
once at startup. The line is drawn at Latin and it is drawn on purpose.

**And what happens at that line is the whole point.** Anything undrawable renders as a
hollow box, not as blank space. The box is stroked into the atlas by the engine at every
size rather than taken from the font's U+FFFD, because plenty of fonts do not have one —
Cascadia Mono, which ships here, is one of them. A fallback that can itself be missing is
not a fallback; it is the original bug wearing a hat. The first few undrawable codepoints
are also named in the log, distinguishing "outside GlyphRanges.h", which is a limit of this
engine, from "not in this font", which is a limit of the font and can be fixed by changing
one.

**A caret is still a byte offset.** Everything in the text field — selection, clipboard,
line starts, box selection — goes on indexing bytes, because that is what a `std::string`
is sliced with. Only two things changed: anything that computes a *width* measures a
character range instead of summing bytes, and anything that *moves* the caret steps whole
codepoints. Everything else passes through one snap to a boundary, in one place, after the
clamp. So Backspace over `ă` removes the letter rather than half of it, and no other code
in that file had to learn what UTF-8 is.

`Core/Utf8.h` is deliberately in Core and not beside the font: the atlas needs decoding,
the text field needs boundaries, and neither should depend on the other or on there being
a device. Nothing in it can loop forever — every function that walks bytes advances by at
least one, whatever it is handed — because text arrives from clipboards, files and a C ABI,
where malformed is an ordinary case and not an assertion.

## A theme field that does not exist

A theme could say `borderWith: 1` and get no border and no message. The reader looks for
the fields it knows and never sees the rest — and that is the worst kind of mistake to
leave to a person: a missing border reads as a design decision, and a wrong colour reads
as the colour somebody chose.

So the field names are declared once, in `ThemeSchema.h`, the same shape as the widget
schema. The theme compiler refuses a name it does not know:

```
`button.ghost` has no field called `borderWith`. Did you mean `borderWidth`?
`button.primary` has no field called `radius6`. Did you mean `radius`?
nothing is styled by `labels`. Did you mean `label`?
`textfield.ide` `syntax` has no field called `keywrd`. Did you mean `keyword`?
```

The suggestion is worth the trouble because the mistake is nearly always a letter, and a
field name is not something you can work out from first principles. It is offered only
when the name is close — within a third of its length — because a wrong guess sends
someone to fix the wrong line. `elevation` gets no suggestion; it is not a misspelling of
anything here.

A nested `syntax` object is checked against the token kinds, not the element's fields: a
`keyword` colour written beside `background` is still a mistake, which a single flat list
of names would let through.

## A language that does not exist

The same mistake, one layer down, and it survived longer because the schema cannot catch
it: `language` on a text field is a *value*, not a field name, and the set of valid values
is not known until the program runs.

`"python"` is built into the engine. Everything else — `cpp`, `glsl`, `javascript`, `lua` —
lives in `res/themes/languages.xml`, which is loaded only if an application names it:

```cpp
config.gui.languagesPath = "res/themes/languages.xml";  // shipped beside the binary
```

An application that did not set it, with a theme that says `language: "cpp"`, got a text
field drawn as plain text and no message. That is the border again: unhighlighted code
reads as *this language has no keywords*, not as *that language is not here*, and the two
are identical on screen.

So the lookup a field makes is not the same one everything else makes. `find()` stays
quiet, because a miss is an ordinary answer to it — `base=` on a language that has not
loaded yet is allowed to miss. `forField()` is the one a field about to draw calls, and it
says so:

```
A text field asks to be highlighted as 'cpp', which no language here is called; it will
draw as plain text. "python" is built in, and the rest load from the file named by
GuiConfig::languagesPath (res/themes/languages.xml ships with the engine).
```

Once per name, not once per frame — a field redraws constantly, and a warning per frame is
a log nobody reads. The same shape `FontAtlas` uses for a text size it did not bake.

The languages that ship are now read by `ctest` rather than only by an editor: the test
loads the real file, checks all four languages arrive including the two derived with
`base=`, and tokenizes a line of C++ to assert `int` is a type, `return` a keyword and
`// done` a comment. Data can be wrong in ways a compiler cannot see.

## The same table types the file

`rda_add_types` writes an interface per element from that same declaration, so annotating
a theme export makes an editor underline the typo before the build gets a chance to:

```ts
export const button: RdaButtonTheme = {
  primary: { normal: "#3A6AD0", borderWith: 1 },
//                              ~~~~~~~~~~
//  'borderWith' does not exist in type 'RdaButtonStyle'. Did you mean 'borderWidth'?
}
```

Every field is optional, because a variant overrides what it cares about and inherits the
rest — that is what `base` is for. A **shared** piece needs the annotation most: without
it, `mode: "code"` widens to `string` and every variant spreading that object is refused.

Two checks rather than one, because they catch at different moments: the editor is where
you want to hear about it, and the compiler is what actually gates the build whether or
not anyone ran `tsc`.

## Wrapping

`wrap` breaks a label to the width it is given, at spaces where it can and mid-word where
it must:

```tsx
<label variant="body" wrap={true} height="content" text="a long paragraph..." />
```

A word too long for a line of its own is broken rather than allowed to overflow: a path or
a hash has no space in it, and running off the edge is worse than an ugly break.

Wrapping is the one case where measuring has to look at the space a widget was *offered*
rather than only at its own content — it turns a width into a height, and `height="content"`
then means "as tall as the lines this broke into". Everything below it moves by the right
amount when the window is resized, because the measure and the paint break against the
same width.

It is off by default. A caption that silently became three lines tall would move
everything under it, and most labels are one line on purpose.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
