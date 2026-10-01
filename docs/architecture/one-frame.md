# One frame, and why there is no diffing

## One frame

```mermaid
flowchart TD
    pump["pumpEvents(wait)<br/>OnDemand: blocks up to 50 ms when idle"] --> svc["loopWork().service()<br/>work other threads asked the loop to do"]
    svc --> dirty{"bindings().applyDirty()"}
    dirty -->|"> 0 applied"| req["requestRedraw()"]
    dirty -->|"nothing dirty"| walk
    req --> walk["beginWindowGui() per window<br/>walks the retained widget tree"]
    walk --> upd["config.onUpdate(dt)<br/>the application: reloadIfChanged()"]
    upd --> end2["gui().end()"]
    end2 --> decide{"does this window<br/>deserve a frame?"}
    decide -->|"changed / resized / requested"| draw["submit + present"]
    decide -->|"nothing moved"| skip["skip: no submit, no present"]
```

`applyDirty()` runs **before** the tree is walked, so the walk sees this frame's values
rather than last frame's. On almost every frame the dirty list is empty and the whole step
costs one check.

When it does apply something it also **marks the retained cache dirty**, and that is not
optional. The cache decides whether to walk the tree by looking at input — the pointer
moving, a button, a key, a focused caret. A binding writing a widget property is none of
those, so without it the frame renders the geometry it already had and the change does not
appear until something unrelated moves the mouse. It looked like the interface lagging by
a second or two; it was the interface being right and the screen showing the frame before.

The redraw decision is per window, not per loop. An idle window costs an idle window —
that mattered most back when windows belonged to different processes, where one animating
application dragged every other one through a redraw it had no reason to want.

## One frame at a time

There is a second way into that diagram, and it exists because of one platform fact:
`glfwPollEvents` does not return while a window is being dragged or resized. Everything
the engine does lives after that call, so without help, moving one window freezes every
window. The OS does keep delivering position, size and refresh events from inside that
loop, so the engine hangs a frame off them — `serviceWhileModal`.

**That second door has to be shut while a frame is already running.** The same three
callbacks also fire *synchronously* out of `SetWindowPos`, and `SetWindowPos` is what
moving, resizing, maximising or undecorating a window calls. So this is an ordinary thing
for an application to do:

```tsx
<button onClick={() => state.rda.maximized = true} />
```

and the call lands in the middle of the walk that ran the handler. A frame started there
resets the frame arena and truncates the draw list underneath the walk still using them —
which shows up as anything from nothing at all to the process going away. `runFrame` sets
a flag for as long as it is building a frame, and `serviceWhileModal` does nothing while
it is set except ask for another frame.

Dropping the event is safe because the thing it would have reported — a new size or
position — is read again at the top of the next frame. It is only the *timing* that was
ever urgent, and only in the case where there is no next frame coming.

---

## Why there is no diffing

This is the part worth understanding, because it is what makes the whole thing cheap.

```mermaid
flowchart LR
    handler["a click handler runs<br/>state.count++"] --> setSig["signals().set(count, 1)"]
    setSig --> changed{"is the value<br/>actually different?"}
    changed -->|"no"| stop["nobody is notified"]
    changed -->|"yes"| mark["mark the observers<br/>of THIS signal dirty"]
    mark --> list["a dirty list of binding ids"]
    list --> apply["next frame: evaluate<br/>only those bindings"]
    apply --> widget["write the property<br/>on the target widget"]
```

There is no virtual tree, no reconciliation and no search. A signal knows its observers, a
binding knows its target widget and property, and a write that changes nothing notifies
nobody. React's model without React's machinery — the dependency graph is built once, at
load, from what the compiler already recorded.

A binding whose program fails is switched **off** rather than retried every frame: it will
be just as wrong next frame, and a warning per frame is a warning nobody reads.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
