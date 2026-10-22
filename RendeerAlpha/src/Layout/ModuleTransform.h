#pragma once
#include <string>
#include <vector>

// Turning a TypeScript file into plain JavaScript.
//
// Internal to the layout tools: both the layout compiler and anything else that reads a
// declaration written in TypeScript needs this, and neither should own it. Not in
// include/, because nothing outside these files has any business calling it.
namespace RDA::Layout {

	// Where esbuild is. RDA_ESBUILD if set; then the path the build was configured
	// with; then beside the engine's own module and executable, which is where a staged
	// bin/ keeps one; then node_modules/.bin searched upward from the working directory.
	// Empty when there is none.
	std::string findEsbuild();

	// Transforms `sourcePath` into JavaScript that assigns its exports to a global named
	// `__module`.
	//
	// IIFE rather than ESM on purpose: the exports arrive as a plain global, so
	// evaluating the result needs no module loader and cannot hand back a promise to
	// unwrap.
	//
	// `withJsx` adds the JSX transform, which a layout needs and a plain declaration
	// does not -- and leaving it off means a stray angle bracket in a config file is an
	// error rather than markup.
	bool transformModule(const std::string& sourcePath, const std::string& esbuildPath,
	                     bool withJsx, std::string& js, std::string& error);

	// A layout, bundled as one ES module rather than an IIFE: every top-level name of
	// every file it imports then lives in one flat scope -- renamed apart where two files
	// used the same one -- which is the scope a binding's names are resolved in. The
	// source map comes back separately, as JSON, for turning a line of the bundle back
	// into a line of the file it came from. See ScriptCompiler.h.
	bool bundleLayout(const std::string& sourcePath, const std::string& esbuildPath,
	                  std::string& js, std::string& sourceMap, std::string& error);

	// JavaScript prepended before a transformed module is evaluated: the colour
	// helpers a theme is written with. Ambient rather than imported, because the
	// transform does not bundle and an import would have nothing to resolve it.
	const char* colourPrelude();

	// The rest of what a layout is evaluated with: signal(), which declares state the
	// layout owns. Themes do not get it -- a theme has no state to declare.
	const char* layoutPrelude();

	// What state.ts is evaluated with at build: defineState, defineTables and
	// defineCommands, which hand back what they were given -- they exist for TypeScript, so
	// the editor types run-time code as it runs -- and __rdaEvaluateComputed, which turns
	// the declarations into the starting values they describe and runs every computed one
	// once against them, for its type and its starting value.
	const char* statePrelude();

	// What a mesh is written with: boxes, shapes, and the three operations that combine
	// them. Also ambient, for the same reason -- and separate from the layout's prelude
	// because a mesh declares no state and a layout builds no geometry.
	const char* meshPrelude();

	// Every file `entry` imports, transitively, with `entry` first. What hot reload
	// watches: since a module may be split across files, watching only the one that
	// was named would miss an edit to any of the others.
	std::vector<std::string> importedFiles(const std::string& entry);
}
