// A prop that happens to be called `state` is the prop, so this is a constant and not a binding.
function Card({ state }: { state: { title: string } }) {
  return <label id="card" text={state.title} />
}

export default function Main() {
  return <Card state={{ title: "hello" }} />
}
