// For the rda_state_error_line test: a declaration that throws while it is read. What
// QuickJS evaluates is esbuild's IIFE, where the comments and the type are gone and the
// whole is wrapped in a function, and the error has to name this file's line: 10.
type Limits = { max: number }

const limits: Limits | undefined = undefined

export const state = {
  count: 0,
  max: limits!.max,
}
