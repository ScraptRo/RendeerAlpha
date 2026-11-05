#include <Layout/LayoutHost.h>
#include <Layout/ImportScan.h>
#include <Layout/Rebuilder.h>
#include <Layout/Bindings.h>
#include <Layout/ScriptHost.h>
#include <GraphicalObjects/Widget.h>
#include <Logger/Logger.h>
#include <Core/FileStamp.h>
#include <Core/Wake.h>

#include <chrono>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;
	}

	bool LayoutHost::open(Widget& parent, std::string blueprintPath, std::string sourcePath,
	                      Announce announce) {
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
		if (!rebuild(/*opening*/ true)) return false;

		// Recorded after the first load, so the first check does not report a change that
		// is only "this has not been looked at before" and recompile for nothing.
		rediscover();
		mBlueprintStamp = fileStamp(mBlueprintPath);
		if (announce == Announce::Now) announceOpen();
		return true;
	}

	// The list is rebuilt only when something in it changed: an import can only be added
	// by editing a file that is already watched, so the moment a new one appears is a
	// moment this is running anyway. Watching a set takes its stamp, so the new set is
	// recorded as it is now.
	void LayoutHost::rediscover() {
		if (mSourcePath.empty()) { mSource.watch({}); return; }
		std::vector<std::string> files = importedFiles(mSourcePath);
		if (files.empty()) files.push_back(mSourcePath);
		mSource.watch(std::move(files));
	}

	bool LayoutHost::sourceChanged() {
		if (mSourcePath.empty()) return false;
		if (!mSource.changed()) return false;

		// Whatever changed may have added or removed an import, so the set is found
		// again before the new stamp is taken -- otherwise a file added in this edit
		// would be reported as another change on the very next check.
		rediscover();
		return true;
	}

	bool LayoutHost::blueprintChanged() {
		const int64_t stamp = fileStamp(mBlueprintPath);
		if (stamp == mBlueprintStamp) return false;
		mBlueprintStamp = stamp;
		return true;
	}

	bool LayoutHost::canCompile() { return rebuilder() != nullptr; }

	bool LayoutHost::reloadIfChanged() {
		bool recompiled = false;

		// Consumed even when nothing can compile it, so the first blueprint change after a
		// source edit is not reported twice.
		const bool edited = sourceChanged();
		if (edited && canCompile()) {
			std::string error;
			if (!rebuilder()->layout(mSourcePath, mBlueprintPath, error)) {
				// The interface on screen is the last one that compiled, and it stays
				// there. A layout with a syntax error in it should cost you a message,
				// not the window you were working in.
				mLastError = error;
				RDA_LOG_WARNING("layout: " << error);
				return false;
			}
			mLastError.clear();
			recompiled = true;
		}

		if (!blueprintChanged() && !recompiled) return false;
		return reload();
	}

	bool LayoutHost::reload() { return rebuild(/*opening*/ false); }

	bool LayoutHost::rebuild(bool opening) {
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
		const bool wasOpen = mInstance.announced();
		if (opening) mInstance.announceClose();
		// A reload runs no onOpen, so what it stops here nothing will start again. Said,
		// rather than left as a timer that quietly stopped after a save.
		const std::shared_ptr<ScriptModule> module = mInstance.sink()->module;
		const size_t timers = !opening && module ? scriptHost().timerCount(*module) : 0;
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

		// The same layout, so still open, and it is the new onClose that will answer.
		if (!opening && wasOpen) mInstance.markAnnounced();
		if (timers > 0) {
			RDA_LOG_WARNING("layout: reloading " << mBlueprintPath << " stopped the " << timers
			                << (timers == 1 ? " timer" : " timers") << " it had running, and a reload "
			                "runs no onOpen to start them again. A timer that has to outlive a reload "
			                "belongs to an action in state.ts");
		}

		// Every bound property starts from what its expression says. The signals those
		// expressions read were never touched, so a counter keeps its count across a
		// reload and the new interface simply agrees with it.
		bindings().applyAll();

		mBlueprintStamp = fileStamp(mBlueprintPath);
		mLastError.clear();

		// Not just a redraw: the tree was replaced, and the retained cache decides
		// whether to walk it by looking at input. Presenting again would present the
		// geometry the old tree left behind.
		RDA::Wake::interfaceChanged();
		return true;
	}
}
