// Names the widget schema does not know, one of each kind: an element that is not one, a
// property that is a transposition of a real one, a property another toolkit would have taught
// (text on a checkbox, which is its label), and one that is nothing like any of them.
// `rda layout` warns about every line and still compiles it; `--strict` refuses it.
export default function Misspelt() {
  return (
    <stack id="root" arrange="vertical">
      <buton id="go" text="Go" />
      <button id="again" widht={100} text="Again" />
      <checkbox id="remember" text="Remember me" />
      <label id="title" text="Title" zebra="striped" />
    </stack>
  )
}
