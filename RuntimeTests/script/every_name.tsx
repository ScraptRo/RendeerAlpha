// Names that are all in the schema and so must never be warned about: the structural keys the
// compiler reads itself (id, row on a list), an event, a property of the common set, a
// component's own props, and the same element written as an object. A warning here is a false
// positive, which is the one way this check can do harm.
const Card = (props: { title: string; id?: string }) => (
  <panel id={props.id} variant="card">
    <label id="heading" text={props.title} hAlign="center" />
  </panel>
)

export default function EveryName() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12} onOpen={() => state.count = 1}>
      <Card id="first" title="One" />
      <Card id="second" title="Two" />
      <checkbox id="remember" label="Remember me" value={true} />
      <button id="add" text="Add" width={100} height="content" onClick={() => state.count++} />
      {{ type: "label", text: "from an object", hAlign: "start" }}
    </stack>
  )
}
