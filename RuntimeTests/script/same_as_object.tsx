// For the rda_layout_forms_agree test: this file and its twin have to compile to the same
// bytes, so the two are kept in step by hand and differ only in how they are written. All
// of it fits the grammar, because a script section records its own file's name.
function Title(props: { text: string }) {
  return { type: "label", id: "title", variant: "heading", height: "content", text: props.text }
}

export default function Main(): Layout {
  return {
    type: "stack", id: "root", arrange: "vertical", spacing: 8,
    onOpen: () => state.count = 1,
    onClose: () => commands.save(),
    children: [
      { type: Title, text: "Hello" },
      {
        type: "stack", id: "row", arrange: "horizontal", height: "content",
        children: [
          { type: "button", id: "add", text: "Add", width: 100, onClick: () => state.count += 5 },
          { type: "label", id: "count", text: () => `count ${state.count}` },
        ],
      },
      false,
      {
        type: "list", id: "items", of: "items",
        row: (item) => ({ type: "label", id: "name", text: () => item.name }),
      },
    ],
  }
}
