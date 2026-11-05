#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Where a position in esbuild's output came from, read out of the source map it writes
// beside the output.
//
// What QuickJS evaluates at build time is never the file somebody wrote: the types are
// gone, the JSX is calls, every import is pasted in above it, and the whole is wrapped in
// a function -- esbuild's IIFE for a theme, a mesh or a declaration, and one more of the
// layout compiler's own for a layout. So an exception thrown while any of them was
// evaluated named a line of the bundle, and the line it pointed at in the file was
// something else -- a few lines off in the file itself, and in a file that imports, a
// line of a different file altogether.
//
// The run-time module has a line map of its own, worked out by the analysis in
// ScriptCompiler.cpp and shipped in the blueprint. This is the build-time half, in C++
// because the analysis is JavaScript that is loaded only with acorn, and an error has to
// be reported whether or not a binding ever needed either.
//
// Internal to the layout tools, like ModuleTransform.h.
namespace RDA::Layout {

	class SourceMap {
	public:
		SourceMap() = default;

		// `json` is the map itself, as bundleLayout and transformModule return it. `entry`
		// is the path the file was compiled as -- the same string QuickJS was given as the
		// file name, which is what a frame of the bundle says. A source that is that file
		// is called by that name, exactly as it was written, and any other is named
		// relative to it, so a file imported from beside the layout reads as a path beside
		// it.
		//
		// `linesBefore` is how many lines were put in front of the bundle before it was
		// evaluated: one for a layout, and for state.ts's code (wrapBundle); none for
		// transformModule's IIFE, which a theme, a mesh and state.ts's declaration are.
		SourceMap(std::string_view json, std::string entry, size_t linesBefore = 0);

		// Whether the map said anything. A map that would not read leaves this empty, and
		// an empty map changes nothing: a line of the bundle is still a line of something.
		bool empty() const { return mLines.empty(); }

		struct Origin {
			std::string file;
			uint32_t    line = 0;      // 1-based, as an editor counts
			uint32_t    column = 0;    // 1-based
		};

		// The source of `line`:`column` of the script as it was evaluated, both 1-based, as
		// QuickJS reports them. False for a position no source accounts for: the lines put
		// in front, the wrapper's own lines after, and whatever esbuild wrote itself.
		bool find(uint32_t line, uint32_t column, Origin& out) const;

		// `text` -- an exception's message and stack -- with every `<entry>:LINE:COL` in it
		// replaced by where that position came from. A position nothing accounts for becomes
		// "the engine's own code", as a run-time module's do. Frames of anything else -- the
		// helpers, acorn -- are left as they were.
		std::string rewrite(const std::string& text) const;

		// Only the frames of `stack` that a file accounts for, rewritten, one per line. For
		// an exception the engine caught and turned into a message of its own -- a computed
		// value that failed while state.ts was built -- where the message already says what
		// went wrong and the rest of the stack is the engine's helpers calling each other.
		// Empty when no frame is in a file.
		std::string ownFrames(const std::string& stack) const;

	private:
		// One `<entry>:LINE[:COL]` in a text: where it is, and what it says.
		struct Position {
			size_t   start = 0, end = 0;
			uint32_t line = 0, column = 0;
		};
		// The next one at or after `search`, which is moved past it.
		bool nextPosition(const std::string& text, size_t& search, Position& out) const;

		struct Segment {
			uint32_t column;         // of the bundle, 0-based
			uint32_t source;
			uint32_t line;           // of the source, 0-based
			uint32_t sourceColumn;   // 0-based
		};
		std::vector<std::vector<Segment>> mLines;    // by line of the bundle, 0-based
		std::vector<std::string>          mSources;  // each as it is shown
		std::string                       mEntry;
		size_t                            mLinesBefore = 0;
	};
}
