# Seeing it without watching it

```bash
rda shot .
```

A `.png` of your interface, and no window.

`rda prototype` opens one and waits for you. This runs exactly the same thing -- the same
compile, the same placeholder state, the same route or layout -- builds every frame the same
way, and declines to put any of them on screen. What comes back is a picture.

That is for the two cases where watching is the wrong shape. A machine with nobody at it:
a build that captures each screen and fails when one of them changed. And anything that has
to be *compared* -- a hover against a rest, a theme against the one before it, this week's
screen against last week's.

## A still

```bash
rda shot . --out home.png --size 1100x700 --scale 1
```

| | |
| --- | --- |
| `--out` | where it goes. `shot.png` by default |
| `--size WxH` | the window, in [layout units](../frontend/sizing.md#what-a-number-means) |
| `--scale` | units to pixels: `1` is a plain display, `1.5` is one at 150%. The display's own, unsaid |
| `--settle <ms>` | how long to let it run first. 250 by default |
| `--scroll <n>` | turn the wheel, negative downward. Held until the picture, so a big number means "to the end" |
| `--key <chord>` | press a key chord first, `Ctrl+S`, `Tab`. Repeatable, sent in order, one a frame |
| `--drag X1,Y1,X2,Y2` | press at the first point, carry across, let go at the second. `--hold` takes the picture before letting go |
| `--click [Ctrl+][Shift+]X,Y` | a click with those held through it, which `--key` cannot do: `--click 100,92 --click Shift+100,212` chooses a run of rows. Repeatable, in order with `--key` |
| a widget's id | `--click`, `--hover` and `--press` take an id as well as `X,Y`: `--click add`, or `--click root/buttons/add` when the end of the path is not enough. The pointer goes to the centre of that widget once the layout is built. An id that is nothing, or the end of several, is refused before a window opens, with the ids there are. Only widgets a screen reader can reach have a centre to aim at |
| `--files <a;b>` | let files go where the pointer is, as though dragged in from outside. Put the pointer there with `--hover` |
| `--a11y <file>` | write the accessibility tree of the same frame — see [screen readers](../frontend/accessibility.md#seeing-it) |
| `--compose <text>` | an input method part way through a word, drawn at the focused field's caret |
| `--type <text>` | type text first, into whatever has the keyboard. In order with `--key`, so `--key Tab --type bud` reaches a dropdown and finds Budapest |

A still waits for the interface to arrive before it takes the picture: transitions ease over
a theme's `transitionMs`, and a window nobody presents runs the loop as fast as the machine
can, so without waiting you get a colour one step from where it started. `--moving` turns
that off; `--still` turns it on for a sequence.

`--size` and `--scale` together are how one machine produces what another would: 1100x700 at
`--scale 1` is the same 1100x700 picture on every display, where the same run without
`--scale` is 1375x875 on a laptop at 125%.

## Under the pointer

```bash
rda shot . --hover 208,27 --out hovered.png
rda shot . --press 208,27 --out pressed.png
```

Hover and press are worked out from where the pointer is, so a picture of a hovered widget
needs a pointer to be somewhere. The coordinates are in layout units -- the ones the layout
is written in -- which is why a shot of the whole screen is the thing to read them off.

With nothing said about the pointer it is parked far outside the window, so a shot of a
screen nobody is pointing at is the same picture every time -- the window is hidden, not
absent, and the real mouse still has a position inside it. `--scroll` on its own puts the
pointer in the middle instead, because a wheel only reaches what it is over.

`--press` holds the button down rather than clicking: what it captures is the pressed look.
A completed click is down on one frame and up on the next, which is a thing to drive from a
backend rather than from a picture.

## Motion

```bash
rda shot . --frames 8 --every 35 --settle 0 --hover 208,27 --out ease.png
```

Eight pictures, 35 milliseconds apart, from the moment the pointer arrives: `ease-0.png`
through `ease-7.png`. Spaced in real time rather than by frames, because a window nobody
presents is not paced by anything -- a thousand frames can be a single millisecond of
animation.

A sequence does not wait for things to settle, since settling is usually the thing being
looked at.

## What is not in it

A `<viewport>`'s own drawing. A capture renders the interface, which is the GUI passes; a
viewport's contents are the scene passes, with their own frames in flight, and an interface
is what this is for. The widget is there, with its background; what the application draws
inside it is not.

## From an application

The same thing, without the tool:

```cpp
config.offscreen = true;        // built every frame, never shown

config.onFrameEnd = [] {
    std::vector<unsigned char> rgba;
    unsigned width = 0, height = 0;
    if (rendeerCaptureFrame(rgba, width, height)) {
        RDA::Png::write("home.png", rgba.data(), width, height);
    }
};
```

Call it from `onFrameEnd`, not `onUpdate`. A frame is not finished when `onUpdate` runs:
the walk is over but `Gui::end()` has not added what it adds afterwards, the focus ring
among it -- so a capture taken there is the frame minus those things.
`rendeerSetSyntheticPointer(x, y, down)` puts the pointer somewhere for the frames that
follow, `rendeerSetSyntheticModifiers(ctrl, shift)` holds those down until it is told
otherwise, and `rendeerClearSyntheticPointer()` gives the mouse its place back and lets go
of both.

`config.offscreen` is not `hiddenMainWindow`: that one says the window is a device anchor and
runs no interface in it at all.

---

Back to [setup](README.md) · [all documentation](../README.md)
