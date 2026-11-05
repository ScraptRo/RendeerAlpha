// For the rda_layout_forms_agree test: this file and its twin have to compile to the same
// bytes, so the two are kept in step by hand and differ only in how they are written. All
// of it fits the grammar, because a script section records its own file's name.
function Title(props: { text: string }) {
  return <label id="title" variant="heading" height="content" text={props.text} />
}

export default function Main() {
  return (
    <stack id="root" arrange="vertical" spacing={8}
           onOpen={() => state.count = 1}
           onClose={() => commands.save()}>
      <Title text="Hello" />
      <stack id="row" arrange="horizontal" height="content">
        <button id="add" text="Add" width={100} onClick={() => state.count += 5} />
        <label id="count" text={() => `count ${state.count}`} />
      </stack>
      {false}
      <list id="items" of="items" row={(item) => <label id="name" text={() => item.name} />} />
    </stack>
  )
}
