// For the rda_shadowed_local test: refused, because the binding would read the top-level
// `step` while the code around it means its own.
const step = 1

function Counter() {
  const step = 5
  return <button id="add" text="Add" onClick={() => { state.count += step }} />
}

export default function Shadowed() {
  return <Counter />
}
