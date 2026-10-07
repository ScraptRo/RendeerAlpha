#pragma once
#include <cstdint>
#include <Core/BuildMode.h>
#include <string>
#include <vector>

namespace RDA {
	class Theme;
}

// The theme, and the meshes it names, reloaded when they change.
//
// LayoutHost does this for the tree: edit a .tsx and the widgets are rebuilt from it. What
// a widget *looks like* is the other half of what somebody edits in a loop, and it was the
// half that needed the application restarted -- which is worst exactly where it matters
// most, because authoring a shape is iterative by its nature. You move a corner by two
// pixels and look at it. Twenty times.
//
// Three kinds of file move, watched for one reason in a fixed order:
//
//   res/mesh/*.ts    -> .rdam   a shape. Recompiled; the mesh library is cleared.
//   res/themes/*.ts  -> .rdth   the palette. Recompiled; the theme is replaced.
//   either artifact, written by something else -- `rda build` in another terminal, or a
//   preview tool -- which is reloaded without this build needing a compiler at all.
//
// Meshes go first, and the order is the build's own: what a mesh declares is part of what
// a theme may carry, so a theme cannot be checked until every mesh has spoken. The
// declarations are read back out of the compiled meshes rather than by evaluating their
// sources again, which also means a mesh shipped without its source still contributes them.
//
// A reload of the theme *replaces* it rather than merging: deleting a line from a theme
// should put that colour back to the default, and the loop this exists for is mostly
// deleting lines and trying again. The cost is that variants a C++ application defined in
// code are forgotten until it restarts.
//
// Two modes, the same two the layout host has. In a build that can compile, editing a
// source is the whole loop. In one that cannot -- a shipped application -- the artifacts
// are still watched and still swapped, so something else writing a new .rdth retints a
// running program and this build contains no compiler at all. Checking costs one stat() per
// file a few times a second, which is what the artifact half is worth.
namespace RDA::Layout {

	class StyleWatch {
	public:
		// `theme` must outlive this. `compiledThemePath` is the .rdth the application was
		// started with; empty is fine, and then only meshes are watched.
		void open(RDA::Theme& theme, std::string compiledThemePath);

		// Reloads whatever moved. True when anything was swapped, which is the caller's
		// cue to ask for a frame -- an idle window has no other reason to look again.
		bool reloadIfChanged();

		// Why the last reload did not happen, if it did not. The interface keeps the look
		// that last compiled, the way it keeps the tree that last compiled.
		const std::string& lastError() const { return mLastError; }

		// Whether this build can turn a source file back into an artifact.
		static constexpr bool canCompile() { return RDA_ENABLE_HOT_RELOAD_BUILD; }

	private:
		// One artifact and the source beside it. See stampPair.
		struct Watched {
			std::string compiled;
			std::string source;      // empty when there is none to compile
			int64_t     stamp = 0;
		};

		int64_t stampPair(const Watched& one) const;
		bool moved(Watched& one);             // and records the new stamp
		void restamp(Watched& one);           // after a compile this process did
		void noteMeshes();                   // whatever has been asked for since last time
		bool reloadMeshes();
		bool reloadTheme();

		RDA::Theme* mTheme = nullptr;
		Watched     mThemeFile;
		std::vector<Watched> mMeshes;
		std::string mLastError;
		int64_t     mNextCheckMicros = 0;
	};

	// The engine's one: opened when the theme is, polled by the frame. An application does
	// not call it -- unlike a layout, which the application names, a theme and its meshes
	// are the engine's own assets and it knows where they are.
	StyleWatch& styleWatch();
}
