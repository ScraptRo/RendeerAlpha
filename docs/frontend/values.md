# The three kinds of value

Every property is one of three things, and the difference is what the compiler does with
it.

```tsx
<label id="t"
       text="Ready"                                    // 1. a constant
       visible={() => state.count > 0} />              // 2. a binding

<button id="b" text="Add"
        onClick={() => state.count++} />               // 3. a handler
```

**A constant** is written into the blueprint and never looked at again.

**A binding** — `{() => …}` — is parsed at build time into stack operations over signals.
It is evaluated when one of the signals it reads changes, and at no other time. Writing
`state.count` marks exactly the bindings that read it, and the next frame evaluates only
those. That is the whole of the reactivity, and it is why a shipped binary can be reactive
without carrying a JavaScript engine.

**A handler** — `onClick`, `onChange` — is the same machinery pointed the other way. It
may write signals and call commands, and it runs when the widget says so.

The arrow is not a closure. It is a syntax the compiler recognises so it knows *this
expression is to be evaluated later*, and it is compiled from its source text. Nothing from
the code around it comes along -- what it can name is `state`, `commands`, `tables`, and
the top level of its file and its imports:

```tsx
const STEP = 5                                     // the file's top level

function Counter() {
  const step = 5                                   // a local of the component
  return (
    <stack id="c">
      <button id="a" onClick={() => state.count += step} />  // error: `step` is not in scope
      <button id="b" onClick={() => state.count += STEP} />  // fine: STEP ships with it
      <button id="c" onClick={() => state.count += 5} />     // fine
    </stack>
  )
}
```

A binding that goes past the [expression grammar](expressions.md) -- `STEP` above, a call,
a method -- runs as compiled JavaScript; see [functions](functions.md).

An event that carries a value hands it to the handler:

```tsx
<checkbox value={() => state.on} onChange={(now) => state.on = now} />
<slider   value={() => state.gain} onChange={(v) => state.gain = v} />
<textfield text={() => state.notes} onChange={(typed) => state.notes = typed} />
<select   value={() => state.size} onChange={(chosen) => state.size = chosen} />
<tabs     value={() => state.page} onChange={(index) => state.page = index} />
```

Naming the parameter is optional — a handler that does not want the value takes none.

A two-way widget takes both halves explicitly, which is deliberate: `value` says where the
truth is and `onChange` says what to do about a change. A widget that wrote the signal by
itself would be a widget with an opinion about ownership.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
