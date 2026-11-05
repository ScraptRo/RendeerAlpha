#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Asking whether a file changed, one way.
//
// The layout host, the theme watcher, the build's staleness test and the prototype's
// state.ts watch each need the same answer, and each used to carry its own copy of the same
// idiom: ask the filesystem for a last write time, swallow the error, and call a missing
// file zero. Four copies of an idiom is four places for the day one of them is wrong, so it
// lives here.
//
// None of this reads a file. It is the modification time and nothing else, which is cheap
// enough to ask a few times a second and exactly as trustworthy as the filesystem's clock.
namespace RDA {

	// The file's last write time as a plain number, or 0 when it is not there or cannot be
	// read. Zero is a fine "no file" value: one that appears later reads as a change, which
	// is what should happen. The number is only ever compared with another taken the same
	// way, never shown, so the epoch and the unit do not matter.
	int64_t fileStamp(const std::string& path);

	// One number for a set of files. Mixed rather than summed: two files whose times move
	// in opposite directions by the same amount would otherwise look like nothing happened.
	// The count is part of it, and so is the order, so a set that gained or lost a member
	// differs from the one it was even when every time happens to line up.
	//
	// The mixing wraps around, which is the point of it rather than a hazard: only equality
	// of two such numbers is meaningful.
	int64_t fileStamps(const std::vector<std::string>& paths);

	// A set of files and what they were when last looked at. changed() is the question a
	// reload loop asks every so often; it answers yes once for each edit and then no, so the
	// caller does not have to remember to record anything.
	//
	// Not thread-safe: it belongs to whoever polls it.
	class FileWatch {
	public:
		// Replaces the set, and takes its stamp now: the first changed() after this reports
		// an edit made since, not "this has not been looked at before".
		void watch(std::vector<std::string> paths);

		// Whether any of them moved since the stamp was last taken, and records the new one.
		// Yes once per change.
		bool changed();

		const std::vector<std::string>& paths() const { return mPaths; }

		// Takes the stamp again without asking. For after this process wrote one of the
		// files itself, so its own write is not read back as somebody else's edit.
		void restamp();

	private:
		std::vector<std::string> mPaths;
		int64_t                  mStamp = 0;
	};
}
