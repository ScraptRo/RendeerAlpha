// A handler runs when the event does, and without its arrow this says it runs now.
export default function Main() {
  return <button id="press" text="Add" onClick={state.count++} />
}
