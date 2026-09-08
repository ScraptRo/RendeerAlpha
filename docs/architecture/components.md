# Components, imports and colour helpers

## Components

A component is a function that returns one element.

```tsx
function Field(props: { label: string; value: RdaBound<string> }) {
  return (
    <stack arrange="horizontal" spacing={6} height="content">
      <label variant="muted" height="content" text={props.label} />
      <label height="content" text={props.value} />
    </stack>
  )
}

<Field id="live" label="and it is still" value={() => `${state.count} clicks in`} />
```

It is **called while the layout is compiled**, and what it returns takes its place. By the
time there is a blueprint the component has already happened: nothing in the running
program knows one was written, and reuse costs nothing at run time because it is not a
run-time idea. This is the same bargain as the rest of the compiler — the author writes
what reads like a component tree, and what ships is nodes.

Three things follow from that, and they are what make this different from React:

**A prop may be a binding, and it stays one.** `value` above is handed straight to a
label, and the thunk is compiled *there*, against that label. So a component can wrap
something reactive without knowing what it reads. That is what makes components worth
having here rather than merely possible.

**A binding written inside a component still closes over nothing** — `props` included.
`text={props.label}` works, because it is substituted before there is an expression to
parse. `text={() => props.label}` does not, for exactly the reason `() => state.count +
step` does not: a binding is compiled from its own source text, and nothing from the
surrounding scope comes along.

**An id at the call site wins** over the one the component gave its own root. Two `<Field>`
siblings have to be two distinguishable widgets — hover and focus are keyed on that path —
and without this rule they would be one name and a numbered duplicate, which renumbers
when the file is reordered. `id` is accepted on every component without being declared:
the compiler consumes it, and the component never sees it.

A component returns exactly **one** element. There are no fragments: a node that
contributed nothing to the tree would have to splice its children into its parent, and a
blueprint's children are contiguous by construction. Returning nothing, an array, or
`<>...</>` is a build error naming the component. So is a component that returns itself,
after 64 levels — a hung build is worse than a failed one.

---

## Splitting a module across files

A theme or a layout may import another file:

```ts
import { ink, accent } from "./palette"

export const dock = { default: { pane: ink.bg, tabActive: accent.base } }
```

esbuild is invoked with `--bundle`, so the import is resolved when the file is compiled.
Without it an `import` survived into the JavaScript QuickJS evaluates with nothing to
resolve it, which is why every helper used to be defined in the file that needed it.

It resolves `node_modules` too. That is neither encouraged nor supported: a package
written for a browser or for node will evaluate here and fail in ways that have nothing to
do with this engine. Import your own files.

**Hot reload follows the imports.** Watching only the file that was named would miss an
edit to any of the others, so `LayoutHost` watches the entry and everything it pulls in,
transitively. The list is read out of the source rather than asked of esbuild —
`--metafile` refuses to run without an output path, so getting it from the bundler would
mean writing the bundle to a temporary file and parsing JSON to learn what the import
lines already say. Static relative imports only, which is all the compiler accepts.

---

## Colour helpers

Every theme and layout is compiled with a small prelude of colour functions already in
scope:

```ts
tool: {
  tab:       rgb(27, 33, 43),
  tabActive: darken("#6B7796", 0.25),
  tabText:   fade("#FFFFFF", 0.68),
}
```

`rgb`, `rgba`, `hsl`, `fade`, `mix`, `lighten`, `darken` and `alphaOf`. They are ambient
rather than imported for the same reason `h` and `state` are: the transform step does not
bundle, so an `import` would survive into the evaluated JavaScript with nothing to resolve
it.

They run while the theme is compiled and are gone afterwards — the blueprint holds the
string one of them returned, so `darken("#6B7796", 0.25)` reaches the engine as
`"#505971FF"`. A value that is not a colour fails the build naming it, rather than
resolving to black at load.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
