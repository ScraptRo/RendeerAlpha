# The backend

The half of an application that is not the interface, and how each language reaches it.

## What a backend is here

An interface is a compiled blueprint: the layout is written in TSX, checked against a
generated schema, styled by a theme, bound to signals and animated by the engine. None of
that is a backend's job. A backend does four things:

| | |
| --- | --- |
| **Declares** the names the interface is written against | signals, commands, tables, screens |
| **Writes** state | `count = 3` |
| **Answers** commands | what `commands.save()` actually does |
| **Fills** tables | the rows a `<list>` shows |
| **Draws**, in a `<viewport>` and nowhere else | a plot, a scene, a board |

And that is the whole seam. A backend lays nothing out, owns no widget and never sees
one. That is not a limitation of the bindings — it is the design. A backend reacts to
events, and a frame is not an event.

Drawing is the one exception, and it is drawn on the map rather than left in the margin: a
[`<viewport>`](viewport.md) is a hole the application fills, because inside one there is
nothing for the interface to decide. C++ gets the Vulkan command buffer there; every other
language gets a list of 2D commands the engine draws.

The consequence worth stating plainly: **a layout cannot tell which language is behind
it**. The same `home.rdab` loads under C++, Python, Node and C#, and nothing in it mentions
any of them.

## The pages

| | |
| --- | --- |
| [The declaration is the source of truth](declaration.md) | one `state.ts`, and every side generated from it |
| [C++](cpp.md) | the native case: `RdaState.h`, commands, tables, screens, threading |
| [Python](python.md) | ctypes over the C ABI; properties, decorators, `fill()`, `init()` returning |
| [Node](node.md) | koffi over the C ABI, and the one thing that could not be done the same way |
| [C#](csharp.md) | P/Invoke; the binding where the compiler checks the most |
| [Drawing in a viewport](viewport.md) | the one place a backend draws: Vulkan from C++, 2D commands from everywhere else |
| [Any language with an FFI](c-abi.md) | the C ABI itself: the threading contract, errors, the surface, and a backend in C |
| [Writing a binding for a new language](new-binding.md) | the six steps the three existing ones took |
| [What is given up, compared with a separate process](process-isolation.md) | one trade, made deliberately |

---

Back to [all documentation](../README.md)
