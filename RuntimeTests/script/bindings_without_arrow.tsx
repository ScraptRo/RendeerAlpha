// Every way of writing a value that reads state, with its arrow. bindings_without_arrow.tsx is the
// same file without them, and the two have to compile to the same bytes.
function Card(props: { title: unknown }) {
  return <label id="card" text={props.title as () => string} />
}

export default function Main() {
  return (
    <stack id="root" arrange={state.rda.width < 700 ? "vertical" : "horizontal"}
           spacing={state.gap}>
      <label id="plain" text={state.title} />
      <label id="template" text={`Count: ${state.count}`} width={`fill-${state.gap}`} />
      <label id="sum" visible={state.count > 0} height={state.rda.height / 2} />
      <label id="choice" text={state.count === 1 ? "one" : "many"} />
      <label id="constant" text="left alone" width={120} visible={true} />
      <Card title={state.title} />
      <list id="items" of="items" row={(item) => <label id="name" text={item.name} />} />
      <button id="press" text={state.title} onClick={() => state.count++} />
    </stack>
  )
}
