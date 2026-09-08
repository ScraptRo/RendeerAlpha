# What an expression may contain

The compiler's expression grammar, in full.

| | |
| --- | --- |
| **Literals** | numbers, `"strings"`, `` `templates ${…}` ``, `true`, `false` |
| **Signals** | `state.name` — read, and in a handler, written |
| **The engine's own** | `state.rda.width`, `state.rda.height` — read only |
| **Row fields** | `item.column` — only inside a `<list>` row template |
| **Event value** | the handler's parameter, wherever it is named |
| **Arithmetic** | `+` `-` `*` `/`, unary `-` |
| **Comparison** | `===` `!==` `<` `<=` `>` `>=` |
| **Logic** | `&&` `\|\|` `!` |
| **Conditional** | `cond ? a : b` |
| **Concatenation** | `+` on text, and template literals |
| **Assignment** | `=` `+=` `-=` `*=` `/=` `++` `--` — handlers only |
| **Commands** | `commands.name()` — handlers only |
| **Sequence** | `;` between statements in a handler |

That is the list. There is no function call other than a command, no property access other
than `state.` and a row's, no array, no object, no loop. Each of those would need
something at run time to interpret it, and there is nothing at run time to interpret it
with.

## `state.rda` — what the engine knows about itself

The one place a binding reads two levels deep. `state.rda.width` and `state.rda.height`
are the window's size in pixels, the same ones a widget is laid out in, rewritten from the
window every frame — so a layout follows a resize without the application being involved:

```tsx
<stack id="root" arrange={() => state.rda.width < 700 ? "vertical" : "horizontal"}>
```

They are **read-only**, and writing one is refused where it is written. They cost nothing
when nothing moves: a write that changes no value notifies nobody, so a still window is
two comparisons a frame and wakes no binding.

`rda` is a group rather than a signal, and the only one. The name is reserved: the signal
behind it is literally called `rda.width`, and an identifier cannot contain a dot, so
nothing an application declares can collide with it. Everything else under `state` is
flat, and `state.a.b` is still an error.

Two rules the grammar enforces rather than hopes for:

- **A value binding may not call a command.** A binding is evaluated whenever the
  interface is drawn, and a command changes something. Drawing must not have effects, so
  this is refused at compile time rather than left as a thing to be careful about.
- **A handler may write; a binding may not.** Same reason.

Everything else — a loop, a computation, a call into a library — belongs in the backend,
which is the half of the application that has a language.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
