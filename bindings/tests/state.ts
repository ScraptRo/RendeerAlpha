// The declaration the binding tests are written against.
//
// Deliberately small, and deliberately one of everything: a signal of each type, a table
// with a column of each type, two screens (one with a param), a command that is bound and
// a command that never is. If the C ABI can carry this, it can carry an application.

export const state = {
  count: { value: 0,       doc: "a number" },
  flag:  { value: false,   doc: "a boolean" },
  text:  { value: "hello", doc: "some text" },
  // Starts empty and is written with more than the 256 bytes a binding guesses at, so the
  // two-call read path -- ask the length, then ask again -- is exercised rather than
  // assumed. Both bindings guess; neither had ever been made to guess wrong.
  note:  { value: "",      doc: "long text, for the read that has to be retried" },
}

export const routes = {
  first:  { layout: "first",  doc: "where it opens" },
  second: { layout: "second", doc: "and where it goes", params: ["count"] },
}

export const commands = {
  bump:   "add one to count",
  unused: "declared and never bound, so invoking it is refused rather than ignored",
}

export const tables = {
  // Not called `rows`: the generated table object has a `rows` accessor of its own, and a
  // column by that name would quietly win. Worth knowing, and worth not tripping over
  // inside the test that would have caught it.
  items: {
    label: { value: "", doc: "text" },
    value: { value: 0,  doc: "a number" },
    on:    false,
  },
}
