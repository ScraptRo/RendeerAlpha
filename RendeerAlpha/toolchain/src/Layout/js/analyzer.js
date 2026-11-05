
var __rdaScript = (function () {
"use strict";

var MODULE = { ecmaVersion: "latest", sourceType: "module" };
var SCRIPT = { ecmaVersion: "latest", sourceType: "script" };

// What run-time code reads the interface through, given to it by the module it ships in.
var RUNTIME = { state: 1, commands: 1, tables: 1, console: 1 };
// The language's own, there at run time with nothing shipped.
var BUILTINS = Object.create(null);
("Object Function Array Number String Boolean Symbol BigInt Math JSON Date RegExp Map Set " +
 "WeakMap WeakSet WeakRef FinalizationRegistry Promise Proxy Reflect Error TypeError RangeError " +
 "SyntaxError ReferenceError EvalError URIError AggregateError ArrayBuffer SharedArrayBuffer " +
 "DataView Int8Array Uint8Array Uint8ClampedArray Int16Array Uint16Array Int32Array Uint32Array " +
 "Float32Array Float64Array BigInt64Array BigUint64Array parseInt parseFloat isNaN isFinite " +
 "encodeURI decodeURI encodeURIComponent decodeURIComponent escape unescape Infinity NaN " +
 "undefined globalThis Iterator").split(" ").forEach(function (n) { BUILTINS[n] = 1; });
// The colour helpers: ambient while the layout is built, and shipped too when run-time code
// calls one -- they are small, and a colour worked out from state is an ordinary thing to want.
var COLOUR = { rgb: 1, rgba: 1, hsl: 1, fade: 1, mix: 1, lighten: 1, darken: 1, alphaOf: 1 };
// The engine's helpers QuickJS lacks: `format`, and the timers. Shipped, like the colour
// helpers, only into a module whose code names them.
var FORMAT = { format: 1 };
var TIMERS = { setTimeout: 1, setInterval: 1, clearTimeout: 1, clearInterval: 1 };
// Names that exist only while the layout is built.
var JSX_ONLY = "JSX builds the layout, which happens once, while it is compiled; code that runs " +
               "later reads and writes state and cannot make widgets";
var DECLARES = "it declares state.ts, which happens once, while it is built; code that runs later " +
               "reads the live state, tables and commands by those names";
var BUILD_ONLY = {
  h: JSX_ONLY, Fragment: JSX_ONLY,
  signal: "signal() declares state while the layout is compiled; code that runs later reads " +
          "and writes the signal it declared, as state.<name>",
  defineState: DECLARES, defineTables: DECLARES, defineCommands: DECLARES
};
// In state.ts the declarations are top-level names too -- `export const state = ...` -- and
// code that runs means the live ones by them. Set by aliases(): each local name the bundle
// gave a declaration, mapped to the runtime object it stands for.
var ALIASES = Object.create(null);
function aliases(map) {
  ALIASES = Object.create(null);
  for (var k in map) ALIASES[k] = map[k];
}
function live(n) { return !!(RUNTIME[n] || ALIASES[n]); }
// Which live object a name is, if it is one.
function liveName(n) { return ALIASES[n] || (RUNTIME[n] ? n : null); }

// ---- names a piece of code refers to --------------------------------------------

// `state`, or what state.ts's bundle calls it. ALIASES is declared below and filled before
// any of this runs.
function isState(n) { return n === "state" || (typeof ALIASES === "object" && ALIASES[n] === "state"); }

function Scope(parent) { this.parent = parent; this.names = Object.create(null); }
Scope.prototype.declare = function (n) { this.names[n] = true; };
Scope.prototype.has = function (n) {
  for (var s = this; s; s = s.parent) if (s.names[n]) return true;
  return false;
};

function boundNames(p, out) {
  if (!p) return out;
  switch (p.type) {
    case "Identifier": out.push(p.name); break;
    case "ObjectPattern":
      p.properties.forEach(function (q) { boundNames(q.type === "RestElement" ? q.argument : q.value, out); });
      break;
    case "ArrayPattern": p.elements.forEach(function (e) { if (e) boundNames(e, out); }); break;
    case "AssignmentPattern": boundNames(p.left, out); break;
    case "RestElement": boundNames(p.argument, out); break;
  }
  return out;
}

function isFunctionNode(n) {
  return n.type === "FunctionDeclaration" || n.type === "FunctionExpression" ||
         n.type === "ArrowFunctionExpression";
}
function isClassNode(n) { return n.type === "ClassDeclaration" || n.type === "ClassExpression"; }

function children(node, visit) {
  for (var key in node) {
    if (key === "type" || key === "start" || key === "end" || key === "loc") continue;
    var v = node[key];
    if (Array.isArray(v)) { for (var i = 0; i < v.length; i++) if (v[i] && typeof v[i].type === "string") visit(v[i]); }
    else if (v && typeof v.type === "string") visit(v);
  }
}

// `var` anywhere in a function body belongs to the function, whichever block it is in.
function hoistVars(node, scope) {
  children(node, function walk(n) {
    if (isFunctionNode(n) || isClassNode(n)) return;
    if (n.type === "VariableDeclaration" && n.kind === "var") {
      n.declarations.forEach(function (d) { boundNames(d.id, []).forEach(function (x) { scope.declare(x); }); });
    }
    children(n, walk);
  });
}

// What a block declares for itself: let, const, class and function.
function blockDecls(statements, scope) {
  statements.forEach(function (s) {
    if (s.type === "VariableDeclaration" && s.kind !== "var") {
      s.declarations.forEach(function (d) { boundNames(d.id, []).forEach(function (x) { scope.declare(x); }); });
    } else if ((s.type === "FunctionDeclaration" || s.type === "ClassDeclaration") && s.id) {
      scope.declare(s.id.name);
    }
  });
}

// The free names of `root`: what it refers to and does not declare. Also which of them it
// assigns to, and whether it writes `state.<name>` -- the two things worth refusing or
// warning about before anything runs.
function freeOf(root) {
  var free = Object.create(null), assigned = Object.create(null), writesState = false;
  function ref(n, scope) { if (!scope.has(n)) free[n] = true; }
  function target(t, scope) {
    if (!t) return;
    switch (t.type) {
      case "Identifier": ref(t.name, scope); if (!scope.has(t.name)) assigned[t.name] = true; return;
      case "MemberExpression":
        if (t.object.type === "Identifier" && isState(t.object.name) && !scope.has(t.object.name)) writesState = true;
        if (t.object.type === "MemberExpression" && t.object.object.type === "Identifier" &&
            isState(t.object.object.name) && !scope.has(t.object.object.name)) writesState = true;
        walk(t, scope);
        return;
      case "ObjectPattern":
        t.properties.forEach(function (p) {
          if (p.type === "RestElement") { target(p.argument, scope); return; }
          if (p.computed) walk(p.key, scope);
          target(p.value, scope);
        });
        return;
      case "ArrayPattern": t.elements.forEach(function (e) { if (e) target(e, scope); }); return;
      case "AssignmentPattern": target(t.left, scope); walk(t.right, scope); return;
      case "RestElement": target(t.argument, scope); return;
      default: walk(t, scope);
    }
  }
  function defaults(p, scope) {
    if (!p) return;
    switch (p.type) {
      case "AssignmentPattern": defaults(p.left, scope); walk(p.right, scope); return;
      case "ObjectPattern":
        p.properties.forEach(function (q) {
          if (q.type === "RestElement") { defaults(q.argument, scope); return; }
          if (q.computed) walk(q.key, scope);
          defaults(q.value, scope);
        });
        return;
      case "ArrayPattern": p.elements.forEach(function (e) { if (e) defaults(e, scope); }); return;
      case "RestElement": defaults(p.argument, scope); return;
    }
  }
  function fn(node, outer) {
    var scope = new Scope(outer);
    if (node.type === "FunctionExpression" && node.id) scope.declare(node.id.name);
    if (node.type !== "ArrowFunctionExpression") scope.declare("arguments");
    node.params.forEach(function (p) { boundNames(p, []).forEach(function (x) { scope.declare(x); }); });
    node.params.forEach(function (p) { defaults(p, scope); });
    if (node.body.type === "BlockStatement") {
      hoistVars(node.body, scope);
      blockDecls(node.body.body, scope);
      node.body.body.forEach(function (s) { walk(s, scope); });
    } else {
      walk(node.body, scope);
    }
  }
  function cls(node, outer) {
    if (node.superClass) walk(node.superClass, outer);
    var scope = new Scope(outer);
    if (node.id) scope.declare(node.id.name);
    node.body.body.forEach(function (m) { walk(m, scope); });
  }
  function block(statements, outer) {
    var scope = new Scope(outer);
    blockDecls(statements, scope);
    statements.forEach(function (s) { walk(s, scope); });
  }
  function walk(node, scope) {
    if (!node || typeof node.type !== "string") return;
    switch (node.type) {
      case "Identifier": ref(node.name, scope); return;
      case "MemberExpression": walk(node.object, scope); if (node.computed) walk(node.property, scope); return;
      case "Property": case "MethodDefinition": case "PropertyDefinition":
        if (node.computed) walk(node.key, scope);
        walk(node.value, scope);
        return;
      case "LabeledStatement": walk(node.body, scope); return;
      case "BreakStatement": case "ContinueStatement": case "MetaProperty": case "PrivateIdentifier": return;
      case "FunctionDeclaration": case "FunctionExpression": case "ArrowFunctionExpression": fn(node, scope); return;
      case "ClassDeclaration": case "ClassExpression": cls(node, scope); return;
      case "BlockStatement": case "StaticBlock": block(node.body, scope); return;
      case "ForStatement": {
        var inner = new Scope(scope);
        if (node.init && node.init.type === "VariableDeclaration" && node.init.kind !== "var") blockDecls([node.init], inner);
        walk(node.init, inner); walk(node.test, inner); walk(node.update, inner); walk(node.body, inner);
        return;
      }
      case "ForInStatement": case "ForOfStatement": {
        var loop = new Scope(scope);
        if (node.left.type === "VariableDeclaration") {
          if (node.left.kind !== "var") blockDecls([node.left], loop);
          walk(node.left, loop);
        } else {
          target(node.left, loop);
        }
        walk(node.right, scope); walk(node.body, loop);
        return;
      }
      case "CatchClause": {
        var caught = new Scope(scope);
        if (node.param) { boundNames(node.param, []).forEach(function (x) { caught.declare(x); }); defaults(node.param, caught); }
        walk(node.body, caught);
        return;
      }
      case "VariableDeclaration":
        node.declarations.forEach(function (d) { defaults(d.id, scope); walk(d.init, scope); });
        return;
      case "AssignmentExpression": target(node.left, scope); walk(node.right, scope); return;
      case "UpdateExpression": target(node.argument, scope); return;
      case "SwitchStatement": {
        walk(node.discriminant, scope);
        var cases = new Scope(scope);
        node.cases.forEach(function (c) { blockDecls(c.consequent, cases); });
        node.cases.forEach(function (c) { walk(c.test, cases); c.consequent.forEach(function (s) { walk(s, cases); }); });
        return;
      }
    }
    children(node, function (n) { walk(n, scope); });
  }
  walk(root, new Scope(null));
  return { free: Object.keys(free), assigned: Object.keys(assigned), writesState: writesState };
}

// Whether evaluating `node` again does exactly what it did the first time: no call, no
// `new`, nothing assigned. Such an initializer ships as code; one that is not ships as the
// value it produced while the layout was built.
function isPlain(node) {
  var plain = true;
  (function walk(n) {
    if (!plain || !n || typeof n.type !== "string") return;
    if (isFunctionNode(n) || isClassNode(n)) return;
    switch (n.type) {
      case "CallExpression": case "NewExpression": case "TaggedTemplateExpression":
      case "AssignmentExpression": case "UpdateExpression": case "AwaitExpression":
      case "YieldExpression": case "ImportExpression":
        plain = false; return;
      case "UnaryExpression": if (n.operator === "delete") { plain = false; return; } break;
    }
    children(n, walk);
  })(node);
  return plain;
}

// ---- the bundle's top level ----------------------------------------------------

var bundle = "", decls = null, lineStarts = [0], origin = [], segments = [], program = null;

var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
function vlq(text) {
  var out = [], value = 0, shift = 0;
  for (var i = 0; i < text.length; i++) {
    var d = B64.indexOf(text.charAt(i));
    if (d < 0) continue;
    value += (d & 31) << shift;
    if (d & 32) { shift += 5; continue; }
    out.push(value & 1 ? -(value >>> 1) : value >>> 1);
    value = 0; shift = 0;
  }
  return out;
}

function shownPath(source, entry) {
  var p = String(source).replace(/^file:\/\//, "");
  try { p = decodeURIComponent(p); } catch (e) {}
  p = p.replace(/^\/([A-Za-z]:)/, "$1").replace(/\\/g, "/");
  var dir = String(entry).replace(/\\/g, "/");
  dir = dir.slice(0, dir.lastIndexOf("/") + 1);
  if (dir && p.toLowerCase().indexOf(dir.toLowerCase()) === 0) return p.slice(dir.length);
  return p;
}

// Bundle line -> "file:line" of the source it came from, through esbuild's source map.
function readMap(json, entry) {
  origin = [];
  segments = [];
  if (!json) return;
  var map;
  try { map = JSON.parse(json); } catch (e) { return; }
  var sources = (map.sources || []).map(function (s) { return shownPath(s, entry); });
  var lines = String(map.mappings || "").split(";");
  var src = 0, line = 0, col = 0;
  for (var l = 0; l < lines.length; l++) {
    origin[l] = "";
    segments[l] = [];
    if (!lines[l]) continue;
    var parts = lines[l].split(","), generated = 0;
    for (var s = 0; s < parts.length; s++) {
      var f = vlq(parts[s]);
      if (f.length < 1) continue;
      generated += f[0];
      if (f.length < 4) continue;
      src += f[1]; line += f[2]; col += f[3];
      if (sources[src] === undefined) continue;
      var where = sources[src] + ":" + (line + 1);
      if (!origin[l]) origin[l] = where;
      segments[l].push([generated, where]);
    }
  }
  for (var k = 1; k < origin.length; k++) if (!origin[k]) origin[k] = origin[k - 1];
}

// Where in the source an exact offset of the bundle came from: the last mapping at or
// before its column. Finer than a line's first mapping, which for a line of JSX esbuild
// reprinted can be a tag that opened lines earlier.
function originAt(offset) {
  var l = lineOf(offset), column = offset - lineStarts[l], found = origin[l] || "";
  var list = segments[l] || [];
  for (var i = 0; i < list.length && list[i][0] <= column; i++) found = list[i][1];
  return found;
}

function where(d) { var at = originAt(d.start); return at ? " (" + at + ")" : ""; }

function lineOf(offset) {
  var lo = 0, hi = lineStarts.length - 1;
  while (lo < hi) {
    var mid = (lo + hi + 1) >> 1;
    if (lineStarts[mid] <= offset) lo = mid; else hi = mid - 1;
  }
  return lo;
}

function analyse(text, mapJson, entry) {
  bundle = text;
  decls = Object.create(null);
  lineStarts = [0];
  for (var i = 0; i < text.length; i++) if (text.charCodeAt(i) === 10) lineStarts.push(i + 1);
  readMap(mapJson, entry);

  program = acorn.parse(text, MODULE);
  program.body.forEach(function (statement) {
    var node = statement;
    if (statement.type === "ExportNamedDeclaration" || statement.type === "ExportDefaultDeclaration") {
      if (!statement.declaration) return;
      node = statement.declaration;
    }
    if ((node.type === "FunctionDeclaration" || node.type === "ClassDeclaration") && node.id) {
      decls[node.id.name] = {
        name: node.id.name, node: node, start: node.start, end: node.end,
        kind: node.type === "FunctionDeclaration" ? "function" : "class", code: true, slice: true
      };
      return;
    }
    if (node.type !== "VariableDeclaration") return;
    node.declarations.forEach(function (d) {
      var single = d.id.type === "Identifier";
      var code = single && (!d.init || isPlain(d.init));
      var fn = code && !!d.init && (d.init.type === "ArrowFunctionExpression" || d.init.type === "FunctionExpression");
      boundNames(d.id, []).forEach(function (n) {
        decls[n] = {
          name: n, node: d, start: d.start, end: d.end, keyword: node.kind,
          kind: fn ? "function" : (code ? "plain" : "data"), code: code, slice: false
        };
      });
    });
  });
}

function isFunction(name) { var d = decls && decls[name]; return !!d && d.kind === "function"; }

// The names the code around a binding declares: its enclosing functions' parameters and
// locals, and the blocks it sits in -- not the module's top level. A binding is compiled
// from its own text, so a name it reads resolves at the top level; when the code around it
// declares the same name, the author meant that one, and resolving to the top level instead
// would compile and be quietly wrong. Null when the binding cannot be found in the bundle.
function surrounding(src) {
  var start = bundle.indexOf(src);
  if (start < 0 || !program) return null;
  var end = start + src.length, names = Object.create(null);
  function add(scope) { for (var n in scope.names) names[n] = true; }
  function visit(node, inside) {
    if (isFunctionNode(node)) {
      if (node.start === start && node.end === end) return true;     // the binding itself
      inside = true;
      var own = new Scope(null);
      node.params.forEach(function (p) { boundNames(p, []).forEach(function (x) { own.declare(x); }); });
      if (node.type === "FunctionExpression" && node.id) own.declare(node.id.name);
      if (node.body.type === "BlockStatement") { hoistVars(node.body, own); blockDecls(node.body.body, own); }
      add(own);
    } else if (inside && (node.type === "BlockStatement" || node.type === "StaticBlock")) {
      var block = new Scope(null);
      blockDecls(node.body, block);
      add(block);
    } else if (inside && node.type === "CatchClause" && node.param) {
      boundNames(node.param, []).forEach(function (x) { names[x] = true; });
    } else if (inside && (node.type === "ForStatement" || node.type === "ForInStatement" || node.type === "ForOfStatement")) {
      var head = node.type === "ForStatement" ? node.init : node.left;
      if (head && head.type === "VariableDeclaration") blockDecls([head], { declare: function (x) { names[x] = true; } });
    }
    var found = false;
    children(node, function (child) {
      if (!found && child.start <= start && child.end >= end) found = visit(child, inside);
    });
    return found;
  }
  return visit(program, false) ? names : null;
}

// Names in `free` that the code around the binding declares, other than the row's own.
function shadowed(src, free, row) {
  var around = surrounding(src);
  if (!around) return null;
  for (var i = 0; i < free.length; i++) {
    var n = free[i];
    if ((row && n === row) || n === "props") continue;
    if (around[n]) return n;
  }
  return null;
}

function localError(n) {
  return "'" + n + "' is declared by the code around this binding, and a binding is compiled from its " +
         "own source: it would read the '" + n + "' at the top of the file, not that one. Move what it " +
         "needs to the top level, pass it in state, or give the two different names";
}

// The calls a grammar binding makes, checked the same way.
function calls(src, names, row) {
  var n = shadowed(src, names, row);
  return n ? { ok: false, error: localError(n) } : { ok: true };
}

// A function's source as an expression. A method written in shorthand -- `run(id) { ... }`,
// which is what toString() gives for one -- is not one until it says `function`.
function normal(src) {
  try { acorn.parse("(" + src + ")", SCRIPT); return src; } catch (e) {}
  var m = /^(async\s+)?(\*\s*)?([A-Za-z_$][\w$]*)\s*\(/.exec(src);
  if (!m) return src;
  return (m[1] || "") + "function" + (m[2] ? "*" : "") + " " + src.slice((m[1] || "").length + (m[2] || "").length);
}

function parseThunk(src) {
  var program = acorn.parse("(" + normal(src) + ")", SCRIPT);
  return program.body[0].expression;
}

function short(src) {
  var s = String(src).replace(/\s+/g, " ");
  return s.length > 60 ? s.slice(0, 57) + "..." : s;
}

// A binding the grammar refused: may it be JavaScript? `ok` with whether it reads the row,
// or the reason it cannot.
function binding(src, row, isEvent, action, computed) {
  var thunk;
  try { thunk = parseThunk(src); }
  catch (e) { return { ok: false, error: "this is not something a binding can be: " + e.message }; }
  if (isFunctionNode(thunk) && !action) {
    if (!isEvent && thunk.params.length > 0) {
      return { ok: false, error: "a value binding takes no parameters -- it is called with nothing. " +
                                 "Only an event handler is handed a value" };
    }
    if (isEvent && thunk.params.length > 1) {
      return { ok: false, error: "a handler takes at most one parameter, naming the value the widget passes it" };
    }
  }
  var info = freeOf(thunk);
  var local = shadowed(src, info.free, row);
  if (local) return { ok: false, error: localError(local) };
  for (var i = 0; i < info.free.length; i++) {
    var n = info.free[i];
    if (row && n === row) continue;
    if (live(n) || (decls && decls[n]) || BUILTINS[n] || COLOUR[n] || FORMAT[n] || TIMERS[n]) continue;
    if (BUILD_ONLY[n]) return { ok: false, error: BUILD_ONLY[n] };
    if (n === "props") {
      return { ok: false, error: "props are resolved when the layout is compiled, so they cannot be " +
                                 "read from a binding -- use the value directly" };
    }
    return { ok: false, error: "'" + n + "' is not in scope. A binding is compiled from its own source, " +
             "so a variable from the code around it does not come along; it can read state, commands, " +
             "tables, and whatever this file and its imports define at their top level" };
  }
  if (!isEvent && info.free.some(function (n) { return liveName(n) === "commands"; })) {
    return { ok: false, error: computed
      ? "it calls a command -- a computed value runs every time what it reads changes, and a " +
        "command changes something. Call it from an action instead"
      : "a value binding cannot call a command -- it is evaluated every time " +
        "what it reads changes, and a command changes something. Call it from an event handler instead" };
  }
  if (!isEvent && info.free.some(function (n) { return TIMERS[n] && !(decls && decls[n]); })) {
    return { ok: false, error: computed
      ? "it starts a timer -- a computed value runs every time what it reads changes, and a timer " +
        "is something happening. Start it from an action instead"
      : "a value binding cannot start a timer -- it is evaluated every time what it reads changes, " +
        "and a timer is something happening. Start it from an event handler instead" };
  }
  if (!isEvent && info.writesState) {
    return { ok: false, error: computed
      ? "it writes state -- a computed value runs every time what it reads changes. Write it from " +
        "an action instead"
      : "a value binding cannot write state -- it is evaluated every time what it " +
        "reads changes. Write it from an event handler instead" };
  }
  return { ok: true, row: !!row && info.free.indexOf(row) >= 0 };
}

// ---- what a value becomes when it is written out ----------------------------------

function literal(v, path, seen, codeName) {
  if (v === undefined) return "undefined";
  if (v === null) return "null";
  var t = typeof v;
  if (t === "boolean") return String(v);
  if (t === "number") {
    if (v !== v) return "NaN";
    if (v === Infinity) return "Infinity";
    if (v === -Infinity) return "-Infinity";
    if (v === 0 && 1 / v < 0) return "-0";
    return String(v);
  }
  if (t === "bigint") return String(v) + "n";
  if (t === "string") return JSON.stringify(v);
  if (t === "symbol") throw new Error(path + " is a symbol, which cannot be written out to run later");
  if (t === "function") {
    var name = codeName(v);
    if (name) return name;
    throw new Error(path + " is a function made while the layout was built, and a function has to " +
                    "be written out to run later: define it at the top level as `function name(...) {}`");
  }
  if (seen.indexOf(v) >= 0) throw new Error(path + " contains itself, and a value that loops cannot be written out");
  seen.push(v);
  try {
    if (Array.isArray(v)) {
      var items = [];
      for (var i = 0; i < v.length; i++) items.push(literal(v[i], path + "[" + i + "]", seen, codeName));
      return "[" + items.join(", ") + "]";
    }
    if (v instanceof Date) return "new Date(" + v.getTime() + ")";
    if (v instanceof RegExp) return String(v);
    if (v instanceof Map) {
      var pairs = [];
      v.forEach(function (value, key) {
        pairs.push("[" + literal(key, path + " key", seen, codeName) + ", " +
                   literal(value, path + ".get(" + String(key) + ")", seen, codeName) + "]");
      });
      return "new Map([" + pairs.join(", ") + "])";
    }
    if (v instanceof Set) {
      var members = [];
      v.forEach(function (value) { members.push(literal(value, path + " member", seen, codeName)); });
      return "new Set([" + members.join(", ") + "])";
    }
    var proto = Object.getPrototypeOf(v);
    if (proto === Object.prototype || proto === null) {
      var keys = Object.keys(v), fields = [];
      for (var k = 0; k < keys.length; k++) {
        fields.push(JSON.stringify(keys[k]) + ": " + literal(v[keys[k]], path + "." + keys[k], seen, codeName));
      }
      var body = "{" + fields.join(", ") + "}";
      return proto === null ? "Object.assign(Object.create(null), " + body + ")" : body;
    }
    var kind = (v.constructor && v.constructor.name) || "object";
    throw new Error(path + " is a " + kind + ", and only plain data -- numbers, text, booleans, arrays, " +
                    "objects, Map, Set, Date, RegExp -- can be written out to run later");
  } finally {
    seen.pop();
  }
}

// ---- the module -----------------------------------------------------------------

function emit(plan) {
  var needed = Object.create(null), list = [], colour = false, formats = false, timers = false;
  var warnings = [], warned = Object.create(null);
  var functionValues = null;

  function reach(name, from, row) {
    if (row && name === row) return;
    var d = decls[name];
    if (ALIASES[name]) return;                 // state.ts's own declarations: the live ones
    if (RUNTIME[name]) {
      if (d && !plan.state) throw new Error("'" + name + "' is what run-time code reads the interface through, and " +
                                            "this layout also defines one at its top level; rename that one");
      return;
    }
    if (d) { if (!needed[name]) { needed[name] = from; list.push(name); } return; }
    if (BUILTINS[name]) return;
    if (COLOUR[name]) { colour = true; return; }
    if (FORMAT[name]) { formats = true; return; }
    if (TIMERS[name]) { timers = true; return; }
    if (BUILD_ONLY[name]) throw new Error(from + ": " + BUILD_ONLY[name]);
    throw new Error(from + " uses '" + name + "', which is nothing this layout or its imports define " +
                    "at their top level, and nothing the language has");
  }

  // The top-level function a value is, if it is one, so a snapshot can name it.
  function codeName(value) {
    if (!functionValues) {
      functionValues = [];
      for (var n in decls) {
        var d = decls[n];
        if (d.kind !== "function" && d.kind !== "class") continue;
        var v;
        try { v = __rdaModule.get(n); } catch (e) { continue; }
        if (typeof v === "function") functionValues.push([v, n]);
      }
    }
    for (var i = 0; i < functionValues.length; i++) {
      if (functionValues[i][0] === value) { reach(functionValues[i][1], "a value"); return functionValues[i][1]; }
    }
    return null;
  }

  plan.calls.forEach(function (n) { reach(n, "a binding"); });
  plan.bindings.forEach(function (b) {
    freeOf(parseThunk(b.src)).free.forEach(function (n) { reach(n, "the binding `" + short(b.src) + "`", b.row); });
  });
  var computed = plan.computed || [], actions = plan.actions || [], watches = plan.watches || [];
  computed.forEach(function (c) {
    freeOf(parseThunk(c.src)).free.forEach(function (n) { reach(n, "state." + c.name); });
  });
  actions.forEach(function (a) {
    freeOf(parseThunk(a.src)).free.forEach(function (n) { reach(n, "commands." + a.name); });
  });
  watches.forEach(function (w) {
    freeOf(parseThunk(w.src)).free.forEach(function (n) { reach(n, "state." + w.name + "'s watch"); });
  });

  var items = [];
  for (var i = 0; i < list.length; i++) {      // grows as it is walked
    var d = decls[list[i]];
    if (d.code) {
      var info = freeOf(d.slice ? d.node : (d.node.init || d.node));
      info.free.forEach(function (n) { reach(n, (d.kind === "plain" ? "'" + d.name + "'" : d.name + "()") + where(d)); });
      info.assigned.forEach(function (n) {
        var target = decls[n];
        if (!target || target.kind === "function" || target.kind === "class" || warned[n]) return;
        warned[n] = true;
        warnings.push("'" + n + "' is changed by run-time code (in " + d.name + "). It lives as long as the " +
                      "layout does, and starts over each time the layout is rebuilt");
      });
    } else {
      var value;
      try { value = __rdaModule.get(d.name); }
      catch (e) { throw new Error("'" + d.name + "' could not be read once the layout was built: " + e.message); }
      d.literal = literal(value, "'" + d.name + "'" + where(d), [], codeName);
    }
    items.push(d);
  }
  items.sort(function (a, b) { return a.start - b.start || (a.name < b.name ? -1 : 1); });

  var chunks = [];
  function chunk(text, offset, exact) {
    chunks.push({ text: text, line: offset === undefined || offset < 0 ? -1 : lineOf(offset),
                  first: exact && offset >= 0 ? originAt(offset) : "" });
  }

  chunk("(function (__rda, __rdaOwner) {\n\"use strict\";\nvar __rdaScopeMap = " + JSON.stringify(plan.declared || {}) + ";\n" +
        __rdaRuntimePrelude + "\n" + (colour ? __rdaColourPrelude + "\n" : "") +
        (formats ? __rdaFormatPrelude + "\n" : "") + (timers ? __rdaTimerPrelude + "\n" : ""));
  // The names the bundle gave state.ts's declarations, where esbuild renamed them apart.
  for (var alias in ALIASES) if (alias !== ALIASES[alias]) chunk("var " + alias + " = " + ALIASES[alias] + ";\n");
  var emitted = Object.create(null);
  items.forEach(function (d) {
    if (d.slice) { chunk(bundle.slice(d.start, d.end) + "\n", d.start); return; }
    if (d.code) {
      if (emitted[d.start]) return;           // one declarator, one declaration
      emitted[d.start] = true;
      chunk((d.keyword || "var") + " " + bundle.slice(d.start, d.end) + ";\n", d.start);
      return;
    }
    chunk((d.keyword || "var") + " " + d.name + " = " + d.literal + ";\n", d.start);
  });
  chunk("return {\n  f: {" + plan.calls.map(function (n) { return JSON.stringify(n) + ": " + n; }).join(", ") +
        "},\n  b: [\n");
  plan.bindings.forEach(function (b) {
    chunk("    (" + (b.row || "__rdaNoRow") + ") => (" + normal(b.src) + "),\n", bundle.indexOf(b.src), true);
  });
  chunk("  ],\n  c: {\n");
  computed.forEach(function (c) {
    chunk("    " + JSON.stringify(c.name) + ": (" + normal(c.src) + "),\n", bundle.indexOf(c.src), true);
  });
  chunk("  },\n  a: {\n");
  actions.forEach(function (a) {
    chunk("    " + JSON.stringify(a.name) + ": (" + normal(a.src) + "),\n", bundle.indexOf(a.src), true);
  });
  chunk("  },\n  w: {\n");
  watches.forEach(function (w) {
    chunk("    " + JSON.stringify(w.name) + ": (" + normal(w.src) + "),\n", bundle.indexOf(w.src), true);
  });
  chunk("  },\n  row: __rdaRow\n};\n})\n");

  var text = "", map = [];
  chunks.forEach(function (c) {
    text += c.text;
    var lines = c.text.split("\n").length - 1;
    for (var k = 0; k < lines; k++) map.push(c.line < 0 ? "" : ((k === 0 && c.first) || origin[c.line + k] || ""));
  });
  return { module: text, map: map.join("\n"), warnings: warnings };
}

return { analyse: analyse, isFunction: isFunction, binding: binding, calls: calls, emit: emit, aliases: aliases };
})();
