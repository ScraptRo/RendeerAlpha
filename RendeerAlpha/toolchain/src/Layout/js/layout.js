
var __rdaSignals = [];
function signal(name, value, options) {
  if (typeof name !== "string" || name.length === 0) {
    throw new Error("signal() needs a name: signal(\"open\", false)");
  }
  __rdaSignals.push({ name: name, value: value, global: !!(options && options.global) });
}
