#pragma once
#include <cstdint>
#include <Layout/Blueprint.h>
#include <string>
#include <vector>

// Turning a layout source file into a blueprint.
//
// The pipeline has three steps and only the middle one is ours:
//
//   1. esbuild strips the TypeScript types and rewrites JSX into calls to h(),
//      producing plain ES module source. No parser of our own is involved.
//   2. That module is evaluated once, here, in a throwaway QuickJS runtime whose only
//      global is h(). Its default export is the component's builder function; calling
//      it returns a tree of plain objects.
//   3. That tree is flattened into a Blueprint.
//
// Step 2 is why the builder function runs exactly once. Everything it does with
// ordinary JavaScript — loops, helpers, composition — happens now, at compile time,
// and leaves no trace in the output. Structure that depends on data which only exists
// at runtime cannot be expressed this way, and will need the For/Show primitives; that
// is a deliberate consequence of running once, not an oversight.
//
// Authoring only. An application links the loader and never this -- and because the
// engine is a static library, one that never calls it does not carry it.
namespace RDA::Layout {

	struct CompileResult {
		bool                 ok = false;
		std::vector<uint8_t> bytes;      // the blueprint, when ok
		std::string          error;      // what went wrong, when not
		size_t               nodeCount = 0;
		size_t               bindingCount = 0;
		double               transformMs = 0.0; // step 1
		double               evaluateMs  = 0.0; // steps 2 and 3
	};

	struct CompileOptions {
		// The esbuild executable. When empty the compiler looks for
		// node_modules/.bin/esbuild.cmd upward from the working directory, which is
		// where a project-local install puts it.
		std::string esbuildPath;
		// Passed to the builder function as its props argument, as a JSON object. The
		// walking skeleton has no props worth passing; this is the seam for them.
		std::string propsJson = "{}";
	};

	// Compiles one source file. Accepts .tsx, .ts, .jsx and .js — esbuild picks its
	// loader from the extension, so a plain .js layout skips type stripping and costs
	// nothing extra.
	CompileResult compileFile(const std::string& sourcePath, const CompileOptions& options = {});

	// Compiles and writes the blueprint next to wherever the caller asks. Returns the
	// same result, so a caller can report timings whether or not it wanted the file.
	CompileResult compileToFile(const std::string& sourcePath, const std::string& outputPath,
	                            const CompileOptions& options = {});

	// Where a compiled blueprint conventionally lives for a given source: the same path
	// with the extension replaced by .rdab.
	std::string blueprintPathFor(const std::string& sourcePath);
}
