// Imported by functions.tsx: run-time code may live in any file a layout imports, and a
// name used in both is renamed apart by the bundler -- this file's `UNITS` is not that one.
const UNITS = ["B", "KB", "MB", "GB"]

export function formatSize(bytes: number) {
  let i = 0
  while (bytes >= 1024 && i < UNITS.length - 1) { bytes /= 1024; i++ }
  return `${bytes.toFixed(i ? 1 : 0)} ${UNITS[i]}`
}

export const sum = (values: number[]) => values.reduce((a, b) => a + b, 0)
