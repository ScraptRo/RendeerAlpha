// A value is read over and over, so it may not write, with an arrow or without one.
export default function Main() {
  return <label id="n" text={state.count++} />
}
