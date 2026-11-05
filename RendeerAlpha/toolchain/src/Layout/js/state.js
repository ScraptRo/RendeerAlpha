
function defineState(declaration) { return declaration; }
function defineTables(declaration) { return declaration; }
function defineCommands(declaration) { return declaration; }
function __rdaEmptyTable() {
  var none = [];
  var t = {
    length: 0,
    at: function () { return undefined; },
    toArray: function () { return []; },
    forEach: function () {},
    map: function () { return []; },
    filter: function () { return []; },
    find: function () { return undefined; },
    findIndex: function () { return -1; },
    some: function () { return false; },
    every: function () { return true; },
    reduce: function (f, start) { return arguments.length > 1 ? start : none.reduce(f); }
  };
  t[Symbol.iterator] = function () { return none[Symbol.iterator](); };
  return t;
}
function __rdaEvaluateComputed(state, tables, commands) {
  function isDecl(d) { return d !== null && typeof d === "object" && !Array.isArray(d); }
  function computes(d) { return isDecl(d) && typeof d.compute === "function"; }
  var raw = {}, results = {}, computing = {}, out = {};
  Object.keys(state).forEach(function (k) { raw[k] = state[k]; });
  function valueOf(k) {
    var d = raw[k];
    if (computes(d)) {
      if ("value" in d) return d.value;
      if (k in results) return results[k];
      if (computing[k]) {
        var loop = new Error("it reads state." + k + ", which is itself or is computed from it -- a value " +
                             "made from its own last value never settles");
        loop.__rdaPlain = true;
        throw loop;
      }
      computing[k] = true;
      try { results[k] = d.compute(); } finally { computing[k] = false; }
      return results[k];
    }
    if (isDecl(d) && "value" in d) return d.value;
    return d;
  }
  Object.keys(raw).forEach(function (k) {
    Object.defineProperty(state, k, { get: function () { return valueOf(k); }, enumerable: true, configurable: true });
  });
  if (tables !== null && typeof tables === "object") {
    Object.keys(tables).forEach(function (k) {
      Object.defineProperty(tables, k, { value: __rdaEmptyTable(), enumerable: true, configurable: true, writable: true });
    });
  }
  if (commands !== null && typeof commands === "object") {
    Object.keys(commands).forEach(function (k) {
      Object.defineProperty(commands, k, { enumerable: true, configurable: true, writable: true, value: function () {
        var e = new Error("it calls commands." + k + " -- a computed value runs every time what it reads " +
                          "changes, and a command changes something. Call it from an action instead");
        e.__rdaPlain = true;
        throw e;
      } });
    });
  }
  function noTimer() {
    var e = new Error("it starts a timer -- a computed value runs every time what it reads changes, " +
                      "and a timer is something happening. Start it from an action instead");
    e.__rdaPlain = true;
    throw e;
  }
  var savedTimeout = typeof setTimeout === "function" ? setTimeout : undefined;
  var savedInterval = typeof setInterval === "function" ? setInterval : undefined;
  setTimeout = noTimer;
  setInterval = noTimer;
  try {
    Object.keys(raw).forEach(function (k) {
      if (!computes(raw[k]) || "value" in raw[k]) return;
      try { out[k] = { value: valueOf(k) }; }
      catch (e) {
        out[k] = { error: String(e && e.message ? e.message : e), plain: !!(e && e.__rdaPlain),
                   stack: e && typeof e.stack === "string" ? e.stack : "" };
      }
    });
  } finally {
    setTimeout = savedTimeout;
    setInterval = savedInterval;
  }
  return out;
}
