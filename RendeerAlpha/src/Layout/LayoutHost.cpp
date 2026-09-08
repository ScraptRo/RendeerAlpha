#include <Layout/LayoutHost.h>
#include "ModuleTransform.h"
#include <Layout/Bindings.h>
#include <GraphicalObjects/Widget.h>
#include <Logger/Logger.h>
#include <RendeerAlpha.h>

#if RDA_ENABLE_HOT_RELOAD_BUILD
#include <Layout/LayoutCompiler.h>
#endif

#include <chrono>
#include <filesystem>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		// The file's timestamp, or 0 if it is not there. Zero is a fine "no file" value:
		// a file that appears later reads as a change, which is what should happen.
		int64_t stampOf(const std::string& path) {
			if (path.empty()) return 0;
			std::error_code ec;
			const auto written = std::filesystem::last_write_time(path, ec);
			if (ec) return 0;
			return written.time_since_epoch().count();
		}
	}

	bool LayoutHost::open(Widget& parent, std::string blueprintPath, std::string sourcePath) {
		mParent = &parent;
		mBlueprintPath = std::move(blueprintPath);
		mSourcePath = std::move(sourcePath);

		if (!mSourcePath.empty() && !canCompile()) {
			// Worth saying rather than quietly ignoring: the application asked to watch a
			// source file and this build cannot turn one into a blueprint. It will still
			// reload when something else writes one.
			RDA_LOG_INFO("layout: this build cannot compile " << mSourcePath
			             << "; watching the blueprint only");
		}
		if (!reload()) return false;

		// Recorded after the first load, so the first check does not report a change that
		// is only "this has not been looked at before" and recompile for nothing.
		rediscover();
		mSourceStamp = stampAll();
		mBlueprintStamp = stampOf(mBlueprintPath);
		return true;
	}

	// The list is rebuilt only when something in it changed: an import can only be added
	// by editing a file that is already watched, so the moment a new one appears is a
	// moment this is running anyway.
	void LayoutHost::rediscover() {
		if (mSourcePath.empty()) { mWatched.clear(); return; }
		mWatched = importedFiles(mSourcePath);
		if (mWatched.empty()) mWatched.push_back(mSourcePath);
	}

	int64_t LayoutHost::stampAll() const {
		int64_t combined = static_cast<int64_t>(mWatched.size());
		for (const std::string& path : mWatched) {
			// Mixed rather than summed: two files whose times move in opposite directions
			// by the same amount would otherwise look like nothing happened.
			combined = combined * 31 + stampOf(path);
		}
		return combined;
	}

	bool LayoutHost::sourceChanged() {
		if (mSourcePath.empty()) return false;
		const int64_t stamp = stampAll();
		if (stamp == mSourceStamp) return false;

		// Whatever changed may have added or removed an import, so the set is found
		// again before the new stamp is taken -- otherwise a file added in this edit
		// would be reported as another change on the very next check.
		rediscover();
		mSourceStamp = stampAll();
		return true;
	}

	bool LayoutHost::blueprintChanged() {
		const int64_t stamp = stampOf(mBlueprintPath);
		if (stamp == mBlueprintStamp) return false;
		mBlueprintStamp = stamp;
		return true;
	}

	bool LayoutHost::reloadIfChanged() {
		bool recompiled = false;

#if RDA_ENABLE_HOT_RELOAD_BUILD
		if (sourceChanged()) {
			const CompileResult built = compileToFile(mSourcePath, mBlueprintPath);
			if (!built.ok) {
				// The interface on screen is the last one that compiled, and it stays
				// there. A layout with a syntax error in it should cost you a message,
				// not the window you were working in.
				mLastError = built.error;
				RDA_LOG_WARNING("layout: " << built.error);
				return false;
			}
			mLastError.clear();
			recompiled = true;
		}
#else
		// Still consumed, so the first blueprint change after a source edit is not
		// reported twice.
		(void)sourceChanged();
#endif

		if (!blueprintChanged() && !recompiled) return false;
		return reload();
	}

	bool LayoutHost::reload() {
		if (!mParent) return false;

		Blueprint blueprint;
		std::string error;
		if (!loadBlueprintFile(mBlueprintPath, blueprint, error)) {
			mLastError = error;
			RDA_LOG_WARNING("layout: " << error);
			return false;
		}

		// Only now is the old one taken down. Reading first means a blueprint that will
		// not parse leaves the running interface untouched instead of clearing it.
		if (Widget* previous = mInstance.root()) {
			const std::string id = previous->id();
			mInstance.release(); // forgets the bindings before their widgets go
			mParent->remove(id);
		}

		mInstance = instantiate(blueprint, *mParent);
		if (!mInstance.valid()) {
			mLastError = "the layout built nothing";
			RDA_LOG_WARNING("layout: " << mLastError);
			return false;
		}

		// Every bound property starts from what its expression says. The signals those
		// expressions read were never touched, so a counter keeps its count across a
		// reload and the new interface simply agrees with it.
		bindings().applyAll();

		mBlueprintStamp = stampOf(mBlueprintPath);
		mLastError.clear();

		// Not just a redraw: the tree was replaced, and the retained cache decides
		// whether to walk it by looking at input. Presenting again would present the
		// geometry the old tree left behind.
		rendeerInterfaceChanged();
		return true;
	}
}
