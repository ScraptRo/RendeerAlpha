
var __rdaHas = Object.prototype.hasOwnProperty;
function __rdaName(k) { return __rdaHas.call(__rdaScopeMap, k) ? __rdaScopeMap[k] : k; }
var __rdaEngine = new Proxy(Object.create(null), {
  get: function (_, k) { return typeof k === "string" ? __rda.get("rda." + k) : undefined; },
  set: function (_, k, v) { if (typeof k !== "string") return false; __rda.set("rda." + k, v); return true; },
  has: function (_, k) { return typeof k === "string" && __rda.has("rda." + k); }
});
var state = new Proxy(Object.create(null), {
  get: function (_, k) { if (typeof k !== "string") return undefined; return k === "rda" ? __rdaEngine : __rda.get(__rdaName(k)); },
  set: function (_, k, v) { if (typeof k !== "string") return false; __rda.set(__rdaName(k), v); return true; },
  has: function (_, k) { return typeof k === "string" && (k === "rda" || __rda.has(__rdaName(k))); }
});
var commands = new Proxy(Object.create(null), {
  get: function (_, k) { if (typeof k !== "string") return undefined; return function () { return __rda.command(k, Array.prototype.slice.call(arguments)); }; }
});
function __rdaRow(id, i) {
  return new Proxy(Object.create(null), {
    get: function (_, c) { return typeof c === "string" ? __rda.cell(id, i, c) : undefined; },
    has: function (_, c) { return typeof c === "string" && __rda.column(id, c); }
  });
}
function __rdaTable(name) {
  var id = __rda.table(name);
  if (id < 0) throw new ReferenceError("tables." + name + " is not a table this application declared");
  var t = {
    get length() { return __rda.rows(id); },
    at: function (i) { var n = __rda.rows(id); i = Math.trunc(i); if (i < 0) i += n; return i >= 0 && i < n ? __rdaRow(id, i) : undefined; },
    toArray: function () { var n = __rda.rows(id), out = new Array(n); for (var i = 0; i < n; i++) out[i] = __rdaRow(id, i); return out; },
    forEach: function (f) { var n = __rda.rows(id); for (var i = 0; i < n; i++) f(__rdaRow(id, i), i); },
    map: function (f) { return t.toArray().map(f); },
    filter: function (f) { return t.toArray().filter(f); },
    find: function (f) { var n = __rda.rows(id); for (var i = 0; i < n; i++) { var r = __rdaRow(id, i); if (f(r, i)) return r; } return undefined; },
    findIndex: function (f) { var n = __rda.rows(id); for (var i = 0; i < n; i++) { if (f(__rdaRow(id, i), i)) return i; } return -1; },
    some: function (f) { return t.findIndex(f) >= 0; },
    every: function (f) { var n = __rda.rows(id); for (var i = 0; i < n; i++) { if (!f(__rdaRow(id, i), i)) return false; } return true; },
    reduce: function (f, start) { var a = t.toArray(); return arguments.length > 1 ? a.reduce(f, start) : a.reduce(f); }
  };
  t[Symbol.iterator] = function () {
    var n = __rda.rows(id), i = 0;
    return { next: function () { return i < n ? { value: __rdaRow(id, i++), done: false } : { value: undefined, done: true }; } };
  };
  return t;
}
var tables = new Proxy(Object.create(null), { get: function (_, k) { return typeof k === "string" ? __rdaTable(k) : undefined; } });
function __rdaText(v) { if (typeof v === "string") return v; try { var j = JSON.stringify(v); return j === undefined ? String(v) : j; } catch (e) { return String(v); } }
function __rdaSay(level) { return function () { __rda.log(level, Array.prototype.map.call(arguments, __rdaText).join(" ")); }; }
var console = { log: __rdaSay(0), info: __rdaSay(0), debug: __rdaSay(0), warn: __rdaSay(1), error: __rdaSay(2) };
