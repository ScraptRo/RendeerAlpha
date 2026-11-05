// For the rda_state_compute_error_line test: a computed value that throws when it is run
// once while state.ts is built. The engine catches that and says which value it was, and
// under that has to say where in this file: line 12, inside the compute.
type Row = { price: number }

const rows: Row[] = []

export const state = {
  count: 0,
  first: {
    compute: () =>
      rows[0].price * state.count,
  },
}
