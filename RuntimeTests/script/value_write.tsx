// For the rda_value_binding_write test: refused, because a value binding runs again when
// what it reads changes, and this one would undo every handler that wrote to count.
export default function ValueWrite() {
  return <label id="total" text={() => state.count = 1} />
}
