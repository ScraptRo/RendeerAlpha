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

	// JavaScript prepended before a transformed module is evaluated: the colour
	// helpers a theme is written with. Ambient rather than imported, because the
	// transform does not bundle and an import would have nothing to resolve it.
	const char* colourPrelude();

	// The rest of what a layout is evaluated with: signal(), which declares state the
	// layout owns. Themes do not get it -- a theme has no state to declare.
	const char* layoutPrelude();

	// Every file `entry` imports, transitively, with `entry` first. What hot reload
	// watches: since a module may be split across files, watching only the one that
	// was named would miss an edit to any of the others.
	std::vector<std::string> importedFiles(const std::string& entry);
}
