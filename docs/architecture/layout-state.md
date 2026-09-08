# State a layout owns

Not everything belongs in the shared declaration. Whether a panel is expanded, which tab
is selected, what is in a filter box — nothing outside that screen will ever name it, and
putting it in `res/state.ts` is bookkeeping for its own sake.

```tsx
signal("expanded", true)          // declared where it is used

<button text={() => state.expanded ? "Hide" : "Show"}
        onClick={() => state.expanded = !state.expanded} />
```

Read as `state.<name>`, like any other signal. The declaration moved next to the use; the
grammar, the bytecode and the error messages did not change — and a binding still reads
through `state.`, which at the point of use is the thing that says "this is reactive".

**Scoped by default.** `signal("expanded", ...)` in `hello.tsx` becomes `hello/expanded`,
so two layouts may each have an `expanded` without being the same signal. The compiler
rewrites the reads inside that file, so nothing downstream knows it was ever local:

```
bind  visible on root/notes
      load hello/expanded
```

**Global on request.** `signal("theme", "dark", { global: true })` keeps the bare name, and
is then the same signal C++ and every other layout see.

The declarations ride in the blueprint as properties of the root node under a reserved
key, visible in `rda dump`:

```
!signal hello/expanded = 1
```

A section of their own would be tidier and would mean a new field in every blueprint
header. They are defined when the layout loads, before any binding resolves a name, and
`define()` returns the signal that is already there — so a hot reload finds the value the
user left rather than the one the file starts with.

## Where the checking stops

A local is not in the generated `RdaState`, so `state` is typed as
`RdaState & Record<string, any>`: a declared name keeps its type, an undeclared one is
permitted. That means a **misspelled local is not a TypeScript error** — it resolves to a
signal nothing declared and warns when the layout loads.

Closing that would mean the layout compiler validating every `state.<name>` against the
shared declaration plus the file's own — which it could, since it already has both, and
which would turn a load-time warning into a build error. Worth doing; not done.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
