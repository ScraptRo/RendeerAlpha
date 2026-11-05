// For the rda_layout_error_line test: a component that throws while the layout is built.
// What QuickJS evaluates is esbuild's bundle, where no line is where it is here -- the
// comments and the types are gone, the helper is pasted in above, and the compiler wraps
// the lot in a function -- and the error has to say where it is here: throws-helper.ts
// line 4, called from line 13 of this file.
import { titleOf } from "./throws-helper"

type Row = { title: string }

const none: Row | null = null

function Title(props: { row: Row | null }) {
  const text = titleOf(props.row)
  return <label id="title" text={text} />
}

export default function Throws() {
  return <Title row={none} />
}
