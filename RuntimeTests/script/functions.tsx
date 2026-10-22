// A layout written to use every way code reaches run time, for the rda_runtime_code test.
// Compiling it is the test: the bundle, acorn, the reach from each binding, the snapshot
// of what a top-level call made, and the bytecode. It is never drawn.
import { formatSize, sum } from "./helpers"

signal("open", false)

// Built while the layout is compiled, by a call: ships as the value it made.
function makeUnits() { return { names: ["B", "KB", "MB"], when: new Date(0), seen: new Set([1, 2]) } }
const UNITS = makeUnits()

// No call in it, so it ships as code and is evaluated again when the module starts.
const LIMITS = [1, 10, 100]

class Counter {
  n = 0
  bump() { this.n += 1; return this.n }
}
const counter = new Counter()   // built here and not data: only reachable as code would fail

function label(n: number) { return `${n} ${UNITS.names[0]} of ${LIMITS.length}` }

// Calls a colour helper at run time, which brings the helpers along.
function tint(on: boolean) { return on ? mix("#ff0000", "#0000ff", 0.5) : "#000000" }

// The engine's helpers: `format` at build time, where it makes a value that ships, and at
// run time, where naming it brings it along; and a timer, from a handler, which brings the
// timers along.
const BUILT = format.bytes(2048)
let hide: number | undefined
function flash() {
  clearTimeout(hide)
  state.open = true
  hide = setTimeout(() => { state.open = false }, 400)
}

// A component: called while the layout is built, never reached at run time.
function Row(props: { text: string }) {
  return <label id="row" height="content" text={props.text} />
}

export default function Functions() {
  return (
    <stack id="root" arrange="vertical">
      <Row text="static" />
      <label id="a" text={() => label(state.count)} />
      <label id="b" text={() => formatSize(state.size)} />
      <label id="c" text={() => state.tags.split(",").filter((t) => t.length > 0).join(" / ")} />
      <label id="d" text={() => `${sum(tables.files.map((f) => f.size))} bytes`} />
      <label id="e" text={() => tint(state.open)} />
      <button id="f" text="go" onClick={() => { const before = state.count; state.count = before + 1; state.open = !state.open }} />
      <list id="g" of="files" row={(file) => <label id="n" text={() => file.name.toUpperCase()} />} />
      <label id="h" text={() => `${format.number(state.size, { decimals: 1 })} of ${BUILT}, ${format.relative(Date.now())}`} />
      <button id="i" text="flash" onClick={() => flash()} />
    </stack>
  )
}
