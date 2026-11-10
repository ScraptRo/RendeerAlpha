// `of` is read once, when the list is built, so there is nothing for a binding to drive.
export default function Main() {
  return <list id="l" of={state.which} row={(item) => <label id="x" text="row" />} />
}
