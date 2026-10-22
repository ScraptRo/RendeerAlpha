// Imported by state.ts, so what its code reaches comes from here too: a function that ships
// as code, and a value a call made, which ships as the value it was.
const SCALE = Math.pow(10, 2)

export function round2(x: number) {
  return Math.round(x * SCALE) / SCALE
}
