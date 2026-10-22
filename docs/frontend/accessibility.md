# Screen readers

An interface drawn by this engine is pixels as far as the operating system can see, so a
screen reader would find one blank window. It finds a tree instead: every widget says what
it is, what it is called and what state it is in, and a screen reader's user can press,
toggle, type into and choose from it the way anyone else does with a mouse.

There is almost nothing to write for it. A button is called what it says, a checkbox what
its label says, a field what its placeholder says, a tree row what the first text in it
says. `alt` is for the things that have no words of their own:

```tsx
<slider id="volume" alt="Volume" value={() => state.volume} onChange={(v) => state.volume = v} />
<button id="close" alt="Close" onClick={() => commands.close()}>
  <image id="x" src="res/icons/x.svg" />
</button>
<image id="logo" src="res/logo.png" alt="RendeerAlpha" />
```

| | |
| --- | --- |
| `alt` | what a screen reader calls this. On a container, it makes the container a named group |
| `live` | `polite` or `assertive`: read out when what this says changes — see [Announcements](#announcements) |

## What is in the tree

| Element | Is | Can be |
| --- | --- | --- |
| `<label>` | text | |
| `<button>` | a button, named by its text, or by the text inside it | pressed |
| `<checkbox>` | a checkbox, on or off | toggled |
| `<slider>` | a slider, with its value and range | set to a value |
| `<number>` | an edit, with its value and, when bounded, its range | set to a value |
| `<textfield>` | an edit, or a document when it is more than one line; read-only when it is | set to a text |
| `<select>` | a combo box showing the chosen option, open or shut; while open, an item per option, the chosen one selected and the highlighted one where the reader is | opened, shut, set by an option's text or value; an option chosen |
| `<tabs>` | a tab list, and an item per title, the open one selected | an item chosen |
| `<list>` | a list, and an item per row — those scrolled out of view marked off-screen | scrolled into view |
| `<tree>` | a tree, and an item per row shown, open or shut, chosen or not, with its level | opened, shut, chosen, activated, scrolled into view |
| `<grid>` | a list, and an item per row named by every column with its title — "Name: notes.txt, Size: 120" — the chosen one selected | chosen, activated, scrolled into view |
| `<grid>` or `<tree>` with `multiSelect` | the same, said to be multi-selectable, every chosen row selected | a row added or taken out; every row, or none |
| `<dock>` | a pane, named by its title | |
| `<popup>`, open | a menu | |
| `<image>` | a picture — only with an `alt`; one without is decoration | |
| anything with `alt` | a group of what is inside it | |

A stack, a scroll view, a panel with no `alt` are layout, and are not in the tree:
what is inside them hangs off whatever is around them. A screen on its way out during a
route's cross-fade is not described; the one arriving is.

Whatever is focused is the same thing for the keyboard and the screen reader: Tab moves a
screen reader along with it, and a screen reader moving the focus moves the ring.

Everything a screen reader does goes through the path a hand would take. Pressing a button
from a screen reader runs its `onClick`; ticking a checkbox runs `onChange`; choosing a tab
writes through `onChange` the way clicking its title does. There is nothing separate to
handle and nothing that can behave differently.

## Reading a field

A text field is read the way a screen reader reads one anywhere else: by character, by word
and by line as the caret moves, what was typed as it is typed, and the selection as it is
made. The field tells it where the caret is, where the selection's other end is, where each
line starts, and where each character on screen is drawn — enough for the screen reader to
say the character the caret just passed, the word under it, the line it is on, and to draw
its own highlight around any of them.

The screen reader can move the caret and select, too, and the field moves as though it had
been clicked: Narrator's and Orca's commands for jumping and selecting work in it.

A word is a run of letters and digits with the spaces after it, or a run of punctuation.
Chinese and Japanese put no spaces between words, so there each Han or kana character is a
step of its own — not what a dictionary would call a word, but something a reader can
follow, where a whole sentence read as one word is not. A sentence is read as a line.

## Announcements

Some things change where the reader is not looking: a status line saying the file saved, a
count going up, an error appearing under a form. `live` says to read them out when they do,
without moving the focus there:

```tsx
<label id="status" live="polite" text={() => state.status} />
<label id="error" live="assertive" visible={() => state.failed} text="Upload failed" />
```

`polite` waits until the screen reader has finished what it is saying; `assertive`
interrupts it, which is for what cannot wait. Either is read when the text changes and when
the element appears saying something — a toast being shown — but not when the interface
first opens, which would read the whole screen out.

On a label it is the label's text. On a container it is all the text inside it, in order,
as one sentence, so a status built from two labels — `Saved` and `3 files` — is read as
"Saved 3 files" rather than as two half-thoughts.

Use it sparingly. Everything that changes being read out is as bad as nothing being: put it
on the one line that tells the reader something happened.

## Long lists

A list keeps only as many row widgets as it shows, but a screen reader is told about every
row, so it can say "row 40 of 900" and read the list from top to bottom. A row scrolled out
of view is described from the table alone — named by the first text its template gives
that row, the same name it has when it is on screen — and marked off-screen; bringing it
into view is a screen reader's ordinary "scroll to this" command, and the list scrolls the
way it would for `revealRow`.

Up to two thousand rows of one list are described, in a window around the ones on screen,
which follows the view as it scrolls. A table longer than that is still read as far as the
reader scrolls; it is not counted to the end.

## What it costs

Nothing, until a screen reader asks. The tree is built only once something has asked the
window for it, and from then on only on frames that walked the interface anyway — a still
window describes nothing. What the screen reader is told is a copy made at the end of the
frame, so it reads at its own pace on its own threads and the frame never waits for it.

## Where it works

**Windows**, through UI Automation — which is what Narrator, NVDA and JAWS read, and what
Accessibility Insights and Inspect show.

**Linux**, through AT-SPI — which is what Orca reads, and what Accerciser shows. The engine
speaks it over D-Bus itself, with no ATK and no GLib, and loads libdbus when a window opens
rather than linking it: a machine without it, or without a session bus, builds and runs
exactly as before. It registers with the desktop's accessibility registry only when the
desktop says assistive technology is on, as every toolkit does — `RDA_ACCESSIBILITY=1`
registers anyway, `RDA_ACCESSIBILITY=0` never — and even then builds nothing until a client
other than the registry asks it something. A tree row says its depth as the `level`
attribute, which is what Orca reads out.

Start the screen reader before the application on Linux: whether to register is decided
when a window opens, and an application started while assistive technology was off stays
unregistered until it is restarted.

**Not on macOS.** The engine itself does not run there yet. The tree is built the same way
everywhere, so the layer that would speak it to NSAccessibility is a contained piece of
work for when it does.

## Seeing it

```bash
rda shot . --a11y tree.txt --key Tab --key Tab
```

writes the tree beside the picture, one node a line, with its state, where it is and the
widget it came from:

```
Button 'Save'  @16,42 260x30  #root/form/save
CheckBox 'Bold' off focused  @16,80 260x18  #root/form/bold
Slider 'Volume' 0.25 [0..1]  @16,106 260x20  #root/form/vol
TabList ''  @292,16 260x300  #root/tabs
  TabItem 'Files' selected  @292,16 67x28  #root/tabs/files
  Tree 'Files'  @292,44 260x272  #root/tabs/files/tree
    TreeItem 'src' open  @292,44 250x24  #root/tabs/files/tree#src
```

The `#` part is the widget's path, which is also its UI Automation `AutomationId` — the
handle a UI test uses to find it.

## Not there yet

- **macOS**, above.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
