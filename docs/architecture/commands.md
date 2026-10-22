# Asking the application to do something

A signal carries a value across the boundary. A command carries intent: a button that
saves a file cannot express "save the file" as an expression over state, and should not
have to — the work belongs in C++, and what the layout says is *which* work to ask for.

Commands are declared where state is, for the same reason:

```ts
export const commands = {
  save:   "write the notes out",
  rename: { doc: "rename a note", params: { id: 0, name: "" } },
}
```

That generates both halves. In C++:

```cpp
RDA::State::define();                  // declares the names, as it always did
RDA::State::onSave([] {                // supplies the work
    write(RDA::State::text_box());
});
RDA::State::onRename([](double id, const std::string& name) { rename(id, name); });
```

and in the layout:

```tsx
<button id="save" text="Save" onClick={() => commands.save()} />
<button id="rename" text="Rename" onClick={() => commands.rename(item.id, state.draft)} />
```

`commands.save()` compiles to two instructions -- `call @save (0)`, `halt` -- and the second
handler pushes its two arguments first and says so: `call @rename (2)`. No string is looked
up at run time: the name is resolved to an id when the layout loads, the same way a signal
name is.

## What a command carries

Its arguments: what the event meant at the moment it happened. A command used to carry
nothing, and state was the channel -- write what the backend needs, then ask -- which was two
steps for one intent, a signal declared only to carry a value across, and a race in a list:
two clicks on two rows before the backend looked, and it read the second row twice.

Underneath, one JSON array, because every language a backend is written in reads that
without help. The C++ handler is typed from the declaration's `params`; the C ABI hands the
array to whoever asks with `rda_command_args()`. Signals stay what the interface shows,
kept and observed; an argument is observed by nothing.

## Answered by state.ts

A command with a `run` is an **action**: state.ts answers it, inside the engine. A layout
calls it exactly as it calls any other command and cannot tell the difference, which is what
lets work move between TypeScript and the backend without a layout changing. A backend that
binds one as well watches it, and runs after it with the same arguments. `rda_call` runs any
command and returns what it returned -- an action's result as JSON, `null` for one a backend
answers. See [code that runs](runtime-code.md#actions-commands-statets-answers).

**What it will not let you write**, each refused where it is written:

- `text={() => commands.save()}` — a value binding is evaluated every time the interface
  is drawn, and a command changes something. Handlers only.
- `otherFunction()` in the grammar, when the layout defines no such function -- there is
  no way to write something that looks like backend work and quietly is not. A function the
  layout does define runs as [code](runtime-code.md).

A command the application never declared is a TypeScript error, because `commands` is
generated only from the declaration. Compiled past that — by running `rda` directly — it
warns at load naming the command, and again if pressed.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
