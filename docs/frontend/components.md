# Components and imports

## Components

A function that returns one element.

```tsx
function Field(props: { label: string; value: RdaBound<string> }) {
  return (
    <stack arrange="horizontal" spacing={6} height="content">
      <label variant="muted" height="content" text={props.label} />
      <label height="content" text={props.value} />
    </stack>
  )
}

<Field id="name" label="Name"  value={() => state.name} />
<Field id="mail" label="Email" value={() => state.email} />
```

It is called **while the layout is compiled**, and what it returns takes its place. What
ships is the stack and the two labels, twice over, with nothing at run time knowing a
component was written. Reuse costs nothing at run time because by run time it has already
happened.

Two things follow from that:

- **A prop may be a binding, and it stays one.** `value` above is handed straight to a
  label; if the caller passed a thunk it is compiled *there*, against that label. So a
  component can wrap something reactive without knowing what it reads.
- **A binding written inside a component still closes over nothing** — `props` included.
  `text={props.label}` works because it is substituted before there is an expression to
  parse. `text={() => props.label}` does not, for exactly the reason `() => step` does not.

An `id` given at the call site wins over the one the component gave its own root, which is
what makes two of them two widgets rather than one name used twice.

## Splitting a layout across files

The transform bundles, so a layout or a theme may import.

```tsx
import Nav from "./nav"
import { hint, columns } from "./strings"
```

Anything imported is evaluated at build time along with everything else — constants,
components, shared tables of text. It leaves no trace in the blueprint.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
