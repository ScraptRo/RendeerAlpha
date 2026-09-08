#pragma once
#include <cstdint>
#include <Core/BuildMode.h>
#include <Layout/LayoutLoader.h>
#include <string>

namespace RDA {
	class Widget;
}

// A layout that reloads when its file changes.
//
// The reload is total: the widgets are thrown away and rebuilt from the new blueprint.
// That is a deliberate simplification, and it is only correct because of where state
// lives. Signals are C++ objects outside the artifact being recompiled, so nothing the
// application knows is inside the thing being replaced — a counter keeps counting
// across a reload without anyone preserving it.
//
// This is the part Fast Refresh and Vite HMR have to work hardest at, and they have to
// because their state lives inside the module being swapped. Here it does not, so the
// simplest possible reload is also the correct one.
//
// Two modes, decided by what it is given and how it was built:
//
//   With a source path, in a development build, it recompiles the .tsx itself when that
//   file changes. One process, and editing a layout is the whole loop.
//
//   Otherwise it watches only the blueprint. The application then contains no compiler
//   at all — and still reloads, whenever something else writes a new one. That is the
//   path a preview tool will drive.
namespace RDA::Layout {

	class LayoutHost {
	public:
		// `parent` must outlive this. `sourcePath` is optional and is only consulted in a
		// build that can compile.
		bool open(RDA::Widget& parent, std::string blueprintPath, std::string sourcePath = {});

		// Checks whether anything moved and reloads if so. Returns true when it did.
		//
		// Call it from the application's update, not from a widget callback: reloading
		// restructures the tree, and a callback fires while that tree is being walked.
		bool reloadIfChanged();

		// Rebuilds regardless. Returns false and leaves the current interface alone if the
		// new blueprint cannot be read — a layout that will not load should not take the
		// running one down with it.
		bool reload();

		const LayoutInstance& instance() const { return mInstance; }
		RDA::Widget* root() const { return mInstance.root(); }

		const std::string& lastError() const { return mLastError; }

		// Whether this build can turn a source file back into a blueprint.
		static constexpr bool canCompile() { return RDA_ENABLE_HOT_RELOAD_BUILD; }

	private:
		bool sourceChanged();
		void rediscover();          // refill mWatched from the source's imports
		int64_t stampAll() const;   // one number for every watched file
		bool blueprintChanged();

		RDA::Widget*   mParent = nullptr;
		std::string    mBlueprintPath;
		std::string    mSourcePath;
		LayoutInstance mInstance;

		// The source and everything it imports. A module may be split across files now,
		// so watching only the one that was named would miss an edit to any of the rest.
		std::vector<std::string> mWatched;
		int64_t     mSourceStamp = 0;
		int64_t     mBlueprintStamp = 0;
		std::string mLastError;
	};
}
