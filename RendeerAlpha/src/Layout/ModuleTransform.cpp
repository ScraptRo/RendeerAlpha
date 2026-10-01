#include "ModuleTransform.h"
#include <Core/Location.h>
#include <vector>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <fstream>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace RDA::Layout {

	namespace {
		// npm writes a .cmd shim on Windows and a plain executable elsewhere.
#if defined(_WIN32)
		constexpr const char* kEsbuildName = "esbuild.cmd";
		constexpr const char* kEsbuildBinary = "esbuild.exe";
#else
		constexpr const char* kEsbuildName = "esbuild";
		constexpr const char* kEsbuildBinary = "esbuild";
#endif

		// Runs a command and captures everything it writes. stderr is folded in because a
		// failed transform reports there, and that text is the error worth showing.
		bool runCapturing(const std::string& command, std::string& output) {
#if defined(_WIN32)
			// cmd.exe strips the outermost pair of quotes, so the whole line is wrapped in
			// one more than it looks like it needs. A shell does not do that, which is why
			// this is not shared.
			const std::string wrapped = "\"" + command + "\" 2>&1";
			FILE* pipe = _popen(wrapped.c_str(), "r");
#else
			const std::string wrapped = command + " 2>&1";
			FILE* pipe = popen(wrapped.c_str(), "r");
#endif
			if (!pipe) return false;

			std::array<char, 4096> buffer{};
			output.clear();
			while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
				output += buffer.data();
			}
#if defined(_WIN32)
			return _pclose(pipe) == 0;
#else
			return pclose(pipe) == 0;
#endif
		}
	}

	std::string findEsbuild() {
		// RDA_ESBUILD wins. node_modules holds binaries for one platform, so a checkout
		// shared between Windows and Linux -- or any packager with its own copy -- needs
		// to say where a usable one is without disturbing the install already there.
		if (const char* fromEnvironment = std::getenv("RDA_ESBUILD")) {
			if (*fromEnvironment) return fromEnvironment;
		}

#ifdef RDA_ESBUILD_DEFAULT
		// What the build was configured with. Ahead of the search below because it is the
		// answer this application actually used, rather than whatever happens to sit above
		// the directory it was started from -- and an application outside the engine's
		// checkout has nothing above it to find.
		{
			std::error_code baked;
			if (std::filesystem::exists(RDA_ESBUILD_DEFAULT, baked)) return RDA_ESBUILD_DEFAULT;
		}
#endif

		std::error_code ec;
		// Beside the engine itself: a staged bin/ holds the tool, the shared library and
		// a standalone esbuild together, so a project with no node_modules anywhere --
		// a Python one, say -- still compiles. The bare binary, not npm's .cmd shim,
		// because there is no npm in that picture.
		for (const std::filesystem::path& base : { engineModuleDirectory(), executableDirectory() }) {
			if (base.empty()) continue;
			for (const char* name : { kEsbuildBinary, kEsbuildName }) {
				const std::filesystem::path candidate = base / name;
				if (std::filesystem::exists(candidate, ec)) return candidate.string();
			}
		}

		std::filesystem::path dir = std::filesystem::current_path(ec);
		if (ec) return {};
		for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
			const std::filesystem::path candidate = dir / "node_modules" / ".bin" / kEsbuildName;
			if (std::filesystem::exists(candidate, ec)) return candidate.string();
			if (!dir.has_parent_path() || dir.parent_path() == dir) break;
			dir = dir.parent_path();
		}
		return {};
	}

	bool transformModule(const std::string& sourcePath, const std::string& esbuildPath,
	                     bool withJsx, std::string& js, std::string& error) {
		const std::string exe = esbuildPath.empty() ? findEsbuild() : esbuildPath;
		if (exe.empty()) {
			error = "esbuild not found. Run 'npm install' in the engine checkout or in "
			        "this project, set RDA_ESBUILD to a copy, or configure with "
			        "-DRDA_COMPILE_LAYOUTS=OFF to use the blueprints as they are.";
			return false;
		}

		std::string command = "\"" + exe + "\" \"" + sourcePath + "\"";
		if (withJsx) command += " --jsx-factory=h --jsx-fragment=Fragment";
		// --bundle so a file may import another. Without it an import survives into the
		// JavaScript QuickJS evaluates, with nothing there to resolve it -- which is why
		// every helper used to be defined in the file that needed it.
		//
		// It resolves node_modules too. That is not encouraged and not supported: a
		// package written for a browser or for node will evaluate here and fail in ways
		// that have nothing to do with this engine. Import your own files.
		command += " --bundle --format=iife --global-name=__module --target=es2020";

		if (!runCapturing(command, js)) {
			error = js.empty() ? "esbuild failed" : js;
			return false;
		}
		return true;
	}

	// Colour helpers, available to every theme and layout without importing anything.
	//
	// Ambient globals rather than a module, for the same reason `h` and `state` are: the
	// transform step does not bundle, so an import would survive into the evaluated
	// JavaScript with nothing to resolve it. These run while the theme is compiled and
	// are gone afterwards -- what reaches the blueprint is the string they returned.
	//
	// Every one of them returns "#RRGGBB" or "#RRGGBBAA", which is what the theme reader
	// already parses, so nothing downstream had to learn about them.
	// What a layout gets on top of the colour helpers.
	//
	// signal() declares state the layout owns: a panel being open, which tab is selected.
	// It is collected while the module is evaluated and never reaches the running program
	// as a function -- what reaches it is a name, a type and an initial value.
	//
	// Scoped to the layout by default, so two files may both call something `open`. A
	// signal meant to be shared says so, and then it is the same signal C++ and every
	// other layout see.
	const char* layoutPrelude() {
		return R"JS(
var __rdaSignals = [];
function signal(name, value, options) {
  if (typeof name !== "string" || name.length === 0) {
    throw new Error("signal() needs a name: signal(\"open\", false)");
  }
  __rdaSignals.push({ name: name, value: value, global: !!(options && options.global) });
}
)JS";
	}


	// What a mesh is written with.
	//
	// All of it runs at build time and none of it survives: what reaches the .rdam is
	// flat arrays of numbers. The functions here exist so that an author writes shapes
	// rather than vertex tables, and so that the C++ that reads the result never has to
	// walk a nested object.
	//
	// A vertex is eight numbers -- anchor x, anchor y, offset x, offset y, u, v, groups,
	// param -- because the alternative is an object per vertex and a mesh has hundreds.
	const char* meshPrelude() {
		return R"JS(
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
  if (!tl && !tr && !br && !bl) return rect(spec, opts);

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
)JS";
	}

	const char* colourPrelude() {
		return R"JS(
function __rdaByte(n) {
  n = Math.round(n);
  return n < 0 ? 0 : (n > 255 ? 255 : n);
}
function __rdaHex(n) {
  return __rdaByte(n).toString(16).padStart(2, "0").toUpperCase();
}
function __rdaParse(colour) {
  if (typeof colour !== "string") throw new Error("expected a colour string, got " + typeof colour);
  var s = colour.trim();
  if (s.charAt(0) === "#") s = s.slice(1);
  if (s.length === 3) s = s.charAt(0) + s.charAt(0) + s.charAt(1) + s.charAt(1) + s.charAt(2) + s.charAt(2);
  if (s.length === 6) s = s + "FF";
  if (s.length !== 8 || /[^0-9a-fA-F]/.test(s)) {
    throw new Error("'" + colour + "' is not a colour: expected #RGB, #RRGGBB or #RRGGBBAA");
  }
  return [parseInt(s.slice(0, 2), 16), parseInt(s.slice(2, 4), 16),
          parseInt(s.slice(4, 6), 16), parseInt(s.slice(6, 8), 16)];
}
function rgb(r, g, b) {
  return "#" + __rdaHex(r) + __rdaHex(g) + __rdaHex(b);
}
function rgba(r, g, b, a) {
  return rgb(r, g, b) + __rdaHex(a * 255);
}
function hsl(h, s, l) {
  h = ((h % 360) + 360) % 360 / 360;
  var q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  var p = 2 * l - q;
  function channel(t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  }
  if (s === 0) return rgb(l * 255, l * 255, l * 255);
  return rgb(channel(h + 1 / 3) * 255, channel(h) * 255, channel(h - 1 / 3) * 255);
}
function fade(colour, alpha) {
  var c = __rdaParse(colour);
  return "#" + __rdaHex(c[0]) + __rdaHex(c[1]) + __rdaHex(c[2]) + __rdaHex(alpha * 255);
}
function mix(a, b, t) {
  var x = __rdaParse(a), y = __rdaParse(b);
  var k = t < 0 ? 0 : (t > 1 ? 1 : t);
  return "#" + __rdaHex(x[0] + (y[0] - x[0]) * k)
             + __rdaHex(x[1] + (y[1] - x[1]) * k)
             + __rdaHex(x[2] + (y[2] - x[2]) * k)
             + __rdaHex(x[3] + (y[3] - x[3]) * k);
}
function lighten(colour, amount) { return mix(colour, "#FFFFFFFF", amount); }
function darken(colour, amount)  { return mix(colour, "#000000FF", amount); }
function alphaOf(colour) { return __rdaParse(colour)[3] / 255; }
)JS";
	}

	// Every file an entry pulls in, transitively, itself included.
	//
	// Read out of the source rather than asked of esbuild: --metafile refuses to run
	// without an output path, so getting the list from the bundler would mean writing the
	// bundle to a temporary file and parsing JSON to learn something the import lines
	// already say.
	//
	// Static relative imports only. A bare specifier names a package, which is not
	// supported, and a dynamic import is not something the compiler accepts either.
	std::vector<std::string> importedFiles(const std::string& entry) {
		namespace fs = std::filesystem;
		std::vector<std::string> found;
		std::vector<std::string> pending{ entry };
		std::unordered_set<std::string> seen;

		auto resolve = [](const fs::path& from, const std::string& specifier) -> std::string {
			const fs::path base = from.parent_path() / specifier;
			std::error_code ec;
			// The extension is usually left off, the way TypeScript is written.
			for (const char* suffix : { "", ".ts", ".tsx", "/index.ts", "/index.tsx" }) {
				const fs::path candidate = base.string() + suffix;
				if (fs::is_regular_file(candidate, ec)) return candidate.lexically_normal().string();
			}
			return {};
		};

		while (!pending.empty()) {
			const std::string path = pending.back();
			pending.pop_back();
			if (path.empty() || !seen.insert(path).second) continue;
			found.push_back(path);

			std::ifstream file(path, std::ios::binary);
			if (!file) continue;
			const std::string text((std::istreambuf_iterator<char>(file)),
			                       std::istreambuf_iterator<char>());

			// Walk the keywords rather than every quoted string, so a path-shaped literal
			// in ordinary code is not mistaken for a dependency.
			for (const char* keyword : { "from", "import" }) {
				const size_t length = std::strlen(keyword);
				size_t at = 0;
				while ((at = text.find(keyword, at)) != std::string::npos) {
					size_t cursor = at + length;
					at = cursor;
					// A keyword has to stand alone: `fromage` is not `from`.
					if (cursor < text.size() && (std::isalnum(static_cast<unsigned char>(text[cursor])) ||
					                             text[cursor] == '_')) continue;
					while (cursor < text.size() &&
					       std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
					if (cursor >= text.size()) break;
					const char quote = text[cursor];
					if (quote != '"' && quote != '\'') continue;
					const size_t start = ++cursor;
					while (cursor < text.size() && text[cursor] != quote) ++cursor;
					if (cursor >= text.size()) break;

					const std::string specifier = text.substr(start, cursor - start);
					if (specifier.rfind("./", 0) != 0 && specifier.rfind("../", 0) != 0) continue;
					const std::string resolved = resolve(fs::path(path), specifier);
					if (!resolved.empty()) pending.push_back(resolved);
				}
			}
		}
		return found;
	}
}
