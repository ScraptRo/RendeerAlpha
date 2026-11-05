// What the interface and the backend agree on. Everything the backend needs to read or
// be told is declared here, once; the build generates each language's side from it.
export const state = {
  count: { value: 0, doc: "how many times the button has been pressed" },
}

export const commands = {
  report: "tell the backend what the count is",
}
