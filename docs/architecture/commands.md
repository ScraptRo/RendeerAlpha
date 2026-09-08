# Asking the application to do something

A signal carries a value across the boundary. A command carries intent: a button that
saves a file cannot express "save the file" as an expression over state, and should not
have to — the work belongs in C++, and what the layout says is *which* work to ask for.

Commands are declared where state is, for the same reason:

```xml
<state>
  <number  name="count" value="0"/>
  <command name="save"  doc="write the notes out"/>
</state>
```

That generates both halves. In C++:

```cpp
RDA::State::define();                  // declares the names, as it always did
RDA::State::onSave([] {                // supplies the work
    write(RDA::State::text_box());
});
```

and in the layout:

```tsx
<button id="save" text="Save" onClick={() => commands.save()} />
```

The handler compiles to two instructions — `call @save`, `halt`. No string is looked up at
run time: the name is resolved to an id when the layout loads, the same way a signal name
is.

**Three things it will not let you write**, each refused where it is written:

- `text={() => commands.save()}` — a value binding is evaluated every time the interface
  is drawn, and a command changes something. Handlers only.
- `commands.save(state.count)` — a command takes no arguments. State is the channel for
  data: write what it needs first, and let it read that. Marshalling a second kind of
  value across a boundary that already carries values would be two answers to one
  question.
- `otherFunction()` — every other call is still refused. There is no way to write
  something that looks like backend work and quietly is not.

A command the application never declared is a TypeScript error, because `commands` is
generated only from the declaration. Compiled past that — by running `rda` directly — it
warns at load naming the command, and again if pressed.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
