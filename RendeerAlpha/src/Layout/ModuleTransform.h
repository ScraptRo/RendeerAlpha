#pragma once
#include <string>

// Turning a TypeScript file into plain JavaScript.
//
// Internal to the layout tools: both the layout compiler and anything else that reads a
// declaration written in TypeScript needs this, and neither should own it. Not in
// include/, because nothing outside these files has any business calling it.
namespace RDA::Layout {

	// Where esbuild is. RDA_ESBUILD if set, otherwise node_modules/.bin searched upward
	// from the working directory. Empty when there is none.
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
}
