
var __rdaGroups = [];
function __rdaGroupBit(name) {
  var at = __rdaGroups.indexOf(name);
  if (at < 0) { at = __rdaGroups.length; __rdaGroups.push(name); }
  if (at > 31) throw new Error("a mesh may have 32 vertex groups; '" + name + "' is one too many");
  return 1 << at;
}

// An edge of a box: [anchor, pixels]. A bare number is an anchor with no offset, so
// `{ left: 0.5 }` is the middle and `{ left: [0, 8] }` is eight pixels in from the left.
function __rdaEdge(e, fallback) {
  if (e === undefined) return fallback;
  if (typeof e === "number") return [e, 0];
  if (Array.isArray(e) && e.length === 2) return [e[0], e[1]];
  throw new Error("an edge is a number or [anchor, pixels], got " + JSON.stringify(e));
}

// The whole rect, unless told otherwise.
function __rdaBox(spec) {
  spec = spec || {};
  return {
    left:   __rdaEdge(spec.left,   [0, 0]),
    top:    __rdaEdge(spec.top,    [0, 0]),
    right:  __rdaEdge(spec.right,  [1, 0]),
    bottom: __rdaEdge(spec.bottom, [1, 0]),
  };
}

function inset(n) { return { left: [0, n], top: [0, n], right: [1, -n], bottom: [1, -n] }; }

// A vertex carries the *name* of the parameter that colours it, not its index: the
// states are written inside the same object literal that declares the params, so they
// are built before the params exist. mesh() resolves them once both are in hand.
function __rdaFill(opts) {
  var fill = opts && opts.fill;
  if (fill === undefined || fill === null) return null;   // the widget's own colour
  if (typeof fill !== "string") throw new Error("fill names a parameter: fill: \"seam\"");
  return fill;
}

function __rdaVert(ax, ay, ox, oy, fill) {
  return [ax, ay, ox, oy, 0, 0, 0, fill];
}

// A fan, which is how every primitive here is triangulated: a centre vertex and a ring
// around it. Convex only, which every box-derived shape is.
function __rdaFan(centre, ring) {
  var v = [centre];
  for (var i = 0; i < ring.length; ++i) v.push(ring[i]);
  var idx = [];
  for (var i = 0; i < ring.length; ++i) {
    idx.push(0, 1 + i, 1 + ((i + 1) % ring.length));
  }
  return { v: v, i: idx };
}

function rect(spec, opts) {
  var b = __rdaBox(spec);
  var p = __rdaFill(opts);
  var cx = [(b.left[0] + b.right[0]) / 2, (b.left[1] + b.right[1]) / 2];
  var cy = [(b.top[0] + b.bottom[0]) / 2, (b.top[1] + b.bottom[1]) / 2];
  return __rdaFan(__rdaVert(cx[0], cy[0], cx[1], cy[1], p), [
    __rdaVert(b.left[0],  b.top[0],    b.left[1],  b.top[1],    p),
    __rdaVert(b.right[0], b.top[0],    b.right[1], b.top[1],    p),
    __rdaVert(b.right[0], b.bottom[0], b.right[1], b.bottom[1], p),
    __rdaVert(b.left[0],  b.bottom[0], b.left[1],  b.bottom[1], p),
  ]);
}

// Corners are pixel radii, pinned to the corner they belong to -- so a wide button keeps
// round corners instead of elliptical ones. That is the whole reason an anchor and an
// offset are separate numbers.
function rrect(spec, radius, opts) {
  var b = __rdaBox(spec);
  var p = __rdaFill(opts);
  var r = radius || 0;
  var tl = r, tr = r, br = r, bl = r;
  if (r && typeof r === "object") { tl = r.tl || 0; tr = r.tr || 0; br = r.br || 0; bl = r.bl || 0; }
  // No shortcut to rect() when every corner is square, even though the shape is the same
  // one. A corner being square already emits its arc points coincident, so `{tl: 6, br: 0}`
  // blends with `{tl: 0, br: 6}` -- and an author who writes a radius of 0 in one state is
  // doing it *because* another state is rounded. Handing that state four triangles instead
  // of twenty would refuse the one thing the rounding is varying for. Somebody who wants a
  // plain rectangle writes rect().

  var steps = 4;   // five points per corner: enough at the sizes a widget is
  var ring = [];
  // Always the same number of points, whatever the radius. A square corner emits its
  // five coincident -- the triangles between them are degenerate and draw nothing, and
  // in exchange every rrect in a mesh shares a topology and can be blended with every
  // other. Emitting one point for a square corner would make `normal` and `selected`
  // different meshes over a difference nobody can see.
  function corner(ax, ay, px, py, rad, from, sx, sy) {
    if (rad <= 0) {
      for (var k = 0; k <= steps; ++k) ring.push(__rdaVert(ax, ay, px, py, p));
      return;
    }
    for (var k = 0; k <= steps; ++k) {
      var a = from + (Math.PI / 2) * (k / steps);
      ring.push(__rdaVert(ax, ay,
                          px + sx * rad + Math.cos(a) * rad,
                          py + sy * rad + Math.sin(a) * rad, p));
    }
  }
  // Clockwise from the top-left, each arc centred one radius inside its corner.
  corner(b.left[0],  b.top[0],    b.left[1],  b.top[1],    tl, Math.PI,        1,  1);
  corner(b.right[0], b.top[0],    b.right[1], b.top[1],    tr, -Math.PI / 2,  -1,  1);
  corner(b.right[0], b.bottom[0], b.right[1], b.bottom[1], br, 0,             -1, -1);
  corner(b.left[0],  b.bottom[0], b.left[1],  b.bottom[1], bl, Math.PI / 2,    1, -1);

  var cx = [(b.left[0] + b.right[0]) / 2, (b.left[1] + b.right[1]) / 2];
  var cy = [(b.top[0] + b.bottom[0]) / 2, (b.top[1] + b.bottom[1]) / 2];
  return __rdaFan(__rdaVert(cx[0], cy[0], cx[1], cy[1], p), ring);
}

// Concatenate, offsetting the second's indices. The composition primitive.
function union() {
  var v = [], i = [];
  for (var s = 0; s < arguments.length; ++s) {
    var shape = arguments[s];
    if (!shape || !shape.v) throw new Error("union takes shapes");
    var base = v.length;
    for (var k = 0; k < shape.v.length; ++k) v.push(shape.v[k].slice());
    for (var k = 0; k < shape.i.length; ++k) i.push(shape.i[k] + base);
  }
  return { v: v, i: i };
}

// Tag every vertex, so a kernel or a weld can address this part later.
function group(shape, name) {
  var bit = __rdaGroupBit(name);
  var v = [];
  for (var k = 0; k < shape.v.length; ++k) {
    var one = shape.v[k].slice();
    one[6] |= bit;
    v.push(one);
  }
  return { v: v, i: shape.i.slice() };
}

// Join coincident geometry: each vertex snaps to the lowest-indexed one within `radius`,
// which makes the result independent of the order they were authored in. Build-time, so
// the vertices genuinely go away.
function weld(shape, radius) {
  var r = radius === undefined ? 0.5 : radius;
  var map = [], v = [];
  for (var k = 0; k < shape.v.length; ++k) {
    var a = shape.v[k];
    var to = -1;
    for (var j = 0; j < v.length; ++j) {
      var b = v[j];
      if (Math.abs(a[0] - b[0]) < 1e-6 && Math.abs(a[1] - b[1]) < 1e-6 &&
          Math.abs(a[2] - b[2]) <= r && Math.abs(a[3] - b[3]) <= r) { to = j; break; }
    }
    if (to < 0) { to = v.length; v.push(a.slice()); }
    else { v[to][6] |= a[6]; }   // the survivor belongs to both groups
    map.push(to);
  }
  var i = [];
  for (var k = 0; k + 2 < shape.i.length; k += 3) {
    var x = map[shape.i[k]], y = map[shape.i[k + 1]], z = map[shape.i[k + 2]];
    if (x !== y && y !== z && x !== z) i.push(x, y, z);   // drop what collapsed
  }
  return { v: v, i: i };
}

function __rdaRegion(kind, name, spec) {
  var b = __rdaBox(spec);
  return { kind: kind, name: name,
           box: [b.left[0], b.left[1], b.top[0], b.top[1],
                 b.right[0], b.right[1], b.bottom[0], b.bottom[1]] };
}
function hit(name, spec)  { return __rdaRegion(0, name, spec); }
function text(name, spec) { return __rdaRegion(1, name, spec); }
function clip(name, spec) { return __rdaRegion(2, name, spec); }

function colour(value, doc) { return { kind: 0, colour: value, doc: doc || "" }; }
function number(value, doc) { return { kind: 1, number: value, doc: doc || "" }; }

// What the compiler reads. Everything is flattened here rather than in C++, because
// walking a nested object through a JavaScript engine's C API is a great deal of code
// to do something a loop in JavaScript does in four lines.
function mesh(def) {
  if (!def || typeof def !== "object") throw new Error("mesh() takes an object");

  var paramNames = [], paramKinds = [], paramNumbers = [], paramColours = [], paramDocs = [];
  var declared = def.params || {};
  for (var name in declared) {
    var one = declared[name];
    if (!one || (one.kind !== 0 && one.kind !== 1)) {
      throw new Error("params." + name + " should be colour(...) or number(...)");
    }
    paramNames.push(name);
    paramKinds.push(one.kind);
    paramNumbers.push(one.kind === 1 ? one.number : 0);
    paramColours.push(one.kind === 0 ? one.colour : "#00000000");
    paramDocs.push(one.doc);
  }

  var states = def.states;
  if (typeof states === "function") states = { normal: states() };
  if (states && states.v) states = { normal: states };
  if (!states || typeof states !== "object") throw new Error("mesh() needs states");

  var stateNames = [], shapes = [];
  for (var s in states) {
    var shape = states[s];
    if (typeof shape === "function") shape = shape();
    if (!shape || !shape.v) throw new Error("state '" + s + "' is not a shape");
    stateNames.push(s);
    shapes.push(shape);
  }
  if (stateNames.length === 0) throw new Error("mesh() needs at least one state");

  // Every state describes the same surface at a different moment, so they share a
  // topology -- that is what lets the runtime blend them instead of swapping. A state
  // with fewer vertices is padded with copies of its last, which are degenerate and draw
  // nothing; one with a different triangle list is a different mesh and is refused.
  var most = 0;
  for (var k = 0; k < shapes.length; ++k) most = Math.max(most, shapes[k].v.length);
  for (var k = 0; k < shapes.length; ++k) {
    var shape = shapes[k];
    if (shape.i.length !== shapes[0].i.length) {
      throw new Error("state '" + stateNames[k] + "' has " + (shape.i.length / 3) +
                      " triangles and '" + stateNames[0] + "' has " + (shapes[0].i.length / 3) +
                      ": states must share a topology so they can be blended");
    }
    while (shape.v.length < most) shape.v.push(shape.v[shape.v.length - 1].slice());
  }

  var verts = [];
  for (var k = 0; k < shapes.length; ++k) {
    for (var j = 0; j < most; ++j) {
      var one = shapes[k].v[j];
      var fill = one[7];
      var param = 255;
      if (fill !== null && fill !== undefined) {
        param = paramNames.indexOf(fill);
        if (param < 0) {
          throw new Error("state '" + stateNames[k] + "' fills with '" + fill +
                          "', which is not one of its params");
        }
      }
      for (var n = 0; n < 7; ++n) verts.push(one[n]);
      verts.push(param);
    }
  }

  var regions = def.regions || [];
  var regionNames = [], regionKinds = [], regionBoxes = [];
  for (var k = 0; k < regions.length; ++k) {
    regionNames.push(regions[k].name);
    regionKinds.push(regions[k].kind);
    for (var n = 0; n < 8; ++n) regionBoxes.push(regions[k].box[n]);
  }

  return {
    __rdaMesh: 1,
    paramNames: paramNames, paramKinds: paramKinds, paramNumbers: paramNumbers,
    paramColours: paramColours, paramDocs: paramDocs,
    stateNames: stateNames, vertsPerState: most, verts: verts, idx: shapes[0].i,
    regionNames: regionNames, regionKinds: regionKinds, regionBoxes: regionBoxes,
    groupNames: __rdaGroups,
  };
}
