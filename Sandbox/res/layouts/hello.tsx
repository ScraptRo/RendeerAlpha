// The first layout that goes all the way through the pipeline.
//
// This file is never read by the application that shows it. `rda layout` runs esbuild
// over it, evaluates the module once, and writes hello.rdab — a flat array of nodes and
// a string table. What ships is that file and a loader that walks it.
//
// The default export is the component. It runs exactly once, at compile time, which is
// what makes everything below free at runtime: the map, the helper function and the
// string concatenation all happen in the compiler and leave only their results behind.

type Props = { title?: string }

// A build-time constant. Nothing about this survives into the blueprint except the
// three rows it produces.
// RdaButtonVariant comes from rda.d.ts, which `rda types` generated from the widget
// schema in C++ and the variants the theme actually defines. Misspell one and the
// editor says so before anything is compiled.
const buttons: Array<[string, RdaButtonVariant]> = [
  ["Primary", "primary"],
  ["Ghost", "ghost"],
  ["Danger", "danger"],
]

// An ordinary function returning markup. Composition like this is the freedom that
// running a real language buys, and it costs nothing at runtime because it is gone by
// then — the compiler sees only the <stack> it returned.
function Row(label: string, variant: RdaButtonVariant) {
  return (
    <stack id={"row-" + variant} vertical={false} spacing={6} height="content">
      <label id="name" text={label} width="fill" height="content" />
      <button id="pick" text={variant} variant={variant} width={110} />
    </stack>
  )
}

export default function Hello(props: Props) {
  return (
    <stack id="root" vertical={true} spacing={8} padding={12}>
      <label id="title" height="content" text={props.title ?? "Compiled from hello.tsx"} />
      <label
        id="subtitle"
        height="content"
        variant="muted"
        text="No parser ran to put this on screen - just a flat node array."
      />

      {buttons.map(([label, variant]) => Row(label, variant))}

      <checkbox id="flag" label="Structure decided at compile time" value={true} height="content" />
      <slider id="amount" variant="warm" min={0} max={1} value={0.35} height="content" />

      <textfield
        id="notes"
        mode="document"
        variant="notes"
        text="Edit hello.tsx, run 'rda layout', restart. The engine never sees TypeScript."
        height="fill"
        minHeight={54}
      />
    </stack>
  )
}
