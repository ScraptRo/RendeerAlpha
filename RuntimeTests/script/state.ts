// A state declaration with code in it: computed values, a command with arguments, and an
// action -- what the generated C++ carries and loads. Compiled by the build into the header
// StateCodeTests.cpp includes, and by the rda_state_code test on its own.
import { round2 } from "./state-helpers"

export const state = defineState({
  price: { value: 2.5, doc: "one item" },
  qty: 2,
  total: { compute: () => round2(state.price * state.qty), doc: "what it costs" },
  summary: { compute: () => `${state.qty} at ${state.price}` },
  lineCount: { compute: () => tables.lines.length },
})

export const tables = defineTables({
  lines: { title: "", amount: 0 },
})

export const commands = defineCommands({
  save: "write it out",
  rename: { doc: "a command with arguments", params: { id: 0, name: "", keep: false } },
  add: {
    doc: "an action",
    run: (n: number) => {
      state.qty += n
      return { qty: state.qty, total: state.total }
    },
  },
})
