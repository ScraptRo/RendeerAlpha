# Pictures, pages and choices

Three elements that were missing rather than interesting. An application cannot show a
logo, group a settings screen, or ask which of five things without them, and "build it out
of buttons" is not an answer a toolkit gets to give.

## `<image>`

```tsx
<image id="logo" src="res/images/swatch.png" fit="contain" width={160} />
```

`fit="contain"` keeps the picture's shape inside the box and centres it; `fit="stretch"`
fills the box and ignores the shape. It loads the file the first time it paints and **owns
the texture**, so the lifetime is the widget's and a hot reload reloads it. Two images of
one file are two textures: a UI has a handful of them, and a cache that outlives the tree
is a cache somebody has to remember to invalidate.

`width="content"` asks for the picture's own pixel size, clamped to the room available and
keeping its shape — otherwise a photograph asks for four thousand pixels.

## A texture cannot be destroyed by the widget that owns it

A frame runs in this order:

```
beginWindowGui()   Gui::begin() walks the tree; the draw list records a const Texture*
onUpdate()         the application's update -- a screen swapped, a layout reloaded
Gui::end()         then record, then submit, from that draw list
```

The draw list is built **before** the application's update and recorded **after** it. So a
widget torn down in an update leaves this frame's commands pointing at whatever it owned.
Nothing owned anything until `<image>`, which is why nothing had hit it; with a picture on
the screen, navigating away produced a destroyed sampler still referenced by a submitted
command buffer, and then a descriptor written from an image view of
`0xdddddddddddddddd` -- the fill pattern of a freed heap block.

So a texture outlives the frame that drew it. `rendeerRetireTexture()` takes ownership,
and the engine destroys it at the top of a frame, once the GPU is idle and the GUI has
released the descriptor set it cached by that address. Taking a `unique_ptr` is what keeps
the address stable: a texture moved into a queue would move out from under the very
pointers this exists to protect.

There are two more moments the queue has to be emptied, and both were found by getting
them wrong first:

- **After `onShutdown`**, for whatever the application released itself.
- **After the windows close**, because a widget tree is owned by its window, not by the
  layout host that filled it -- so the last images are retired *after* the renderer is
  gone and *before* the device is. Left past that, VMA asserts that allocations outlived
  the block they came from, which is exactly what had happened.

The GUI backend had already written down what the rule was: *"whoever destroyed the view
should have called forgetTexture() first, so this is a bug upstream rather than a case to
handle quietly."* It was right, and the upstream was `<image>`.

## `<tabs>`

```tsx
<tabs id="pages" value={() => state.tab} onChange={(i) => state.tab = i}>
  <tab id="picture" title="Picture"> ... </tab>
  <tab id="about"   title="About">   ... </tab>
</tabs>
```

A `<tab>` is a container with a title; the title is what the bar says. `value` is an index,
bound to a signal, so it is two-way exactly as a checkbox is — the bar writes it, and
anything else that writes it moves the tabs.

The pages that are not showing **are still there**, holding their state. A Widget is a
thing that persists, so leaving a tab and coming back leaves everything in it where it
was; only the selected page is painted and measured.

## `<select>`

```tsx
<select id="size" value={() => state.size} onChange={(v) => state.size = v}>
  <option id="s" text="Small"  value="small" />
  <option id="m" text="Medium" value="medium" />
</select>
```

Its choices are `<option>` children, read where they are rather than copied into a list of
the widget's own — so a binding on an option's text is an ordinary binding on an ordinary
widget, and there are not two copies to keep in step. `onChange` hands over the chosen
option's `value`, a string, so it binds to a text signal the way a text field does.

Choosing happens on release, not press: the press is what opened the list, and acting on
it would pick whatever happened to be under the pointer at that moment.

## A dropdown unrolling

`transitionMs` on `button`, which is where a `<select>` already takes its colours from.

The list is drawn to a fraction of its height and faded by the same fraction, so it comes
out of the control rather than appearing beside it. Its rows are laid out where they will
finally sit and the box revealing them grows past them — the alternative, easing each row
into place, would have them slide relative to one another, which is a list being sorted
rather than a list being shown. Opening and closing, measured:

```
open  0.0000 → 0.3213 → 0.6295 → 0.8685 → 0.9904 → 1.0000
shut  1.0000 → 0.7361 → 0.4309 → 0.2226 → 0.0953 → 0.0002
```

**It is the widget that stays alive, not a copy of it.** A closing dock panel needs a
ghost, because closing destroys the container; a `<select>` is a widget and a widget
persists, so the only thing needed here was to stop asking `mOpen` whether to draw the list
and start asking how far open it is. `drawAbove()` is called while that is above zero
rather than while the flag is set.

**A list on its way shut takes no input**, inside `pushInert()`. It is still on the screen,
and without it the press that closed the list would also pick whatever row it happened to
be over.

**The rows fade under the pointer** rather than switching on and off, so a pointer running
down the list leaves each one on its way out. They are drawn always and faded to nothing
when cold — only the alpha moves, since both ends are the same colour.

**And the box itself now follows what it is doing.** A `<select>` drew its own fill
directly rather than going through `Gui::button()`, so it was the one control on the screen
that still snapped between normal, hovered and pressed. It uses the same animated value the
others do now.

The chevron cross-fades between `v` and `^` in place. There is no transform to turn one
over with — every quad this GUI draws is axis-aligned, which is the same limit that keeps
`addQuad` to eight floats — and over a tenth of a second two glyphs swapping reads as the
one becoming the other.

## Drawing above everything

An open dropdown belongs to a widget somewhere down a panel, and it has to draw **over**
everything and **outside** whatever clips its owner. Nothing in the GUI could do that, so:

```cpp
void Gui::drawAbove(Widget* widget, glm::vec2 origin);   // from inside a paint
virtual void Widget::paintAbove(Gui&, glm::vec2 origin); // called after the walk
```

Widgets that ask during the walk are painted after it, with the clip back at the whole
viewport, in the order they asked — so the last one opened is on top. Hover is
last-writer-wins, so a popup painted last also wins the pointer, which is what makes
clicking a row work rather than clicking whatever is under it.

It is a general mechanism, not a dropdown mechanism: a tooltip or a context menu is the
same shape and needs no further engine change.

One detail worth stating because it looked like a bug: the list is drawn **opaque**,
whatever the panel style says. A panel is translucent on purpose — it sits over content
and is meant to. A dropdown is a list of words to read, and the label showing through it
makes both unreadable.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
