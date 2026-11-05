#pragma once
#include <GraphicalObjects/Accessible.h>
#include <cstdint>
#include <vector>

// What is different between two published trees.
//
// Telling a screen reader about a frame is two questions. Which nodes came, went or changed,
// and how, has one answer, and it is this file's. What to say about that has one answer per
// platform, and it stays in the platform layer: UI Automation and AT-SPI do not have the same
// events, and they are not asked the same things. A single-select list losing its chosen row
// is an event on Linux and nothing on Windows, where the row that replaced it already says so.
//
// So a flag here means only "this differs and there is something to report it as". Whether it
// is worth an event, and which one, is the caller's.
namespace RDA {

	// One node that is new, or that is different from how it was.
	struct AccessibleChange {
		const AccessibleNode* before = nullptr;   // null: it was not in the last tree
		const AccessibleNode* now = nullptr;

		// What differs. All false on a new node, which has nothing to differ from.
		//
		// The first three are not set for a node that has stopped toggling, expanding or being
		// selectable: -1 is the absence of the state rather than a value of it, so there is
		// nothing to announce it as.
		bool toggled = false;
		bool expanded = false;
		bool selected = false;
		bool name = false;
		bool value = false;
		bool range = false;    // only on a node that has a range now
		bool caret = false;    // these two only on a node that has text now
		bool anchor = false;
	};

	// Everything here points into the two trees, so it lasts as long as they do.
	struct AccessibleDiff {
		std::vector<const AccessibleNode*> removed;   // in the order the last tree listed them
		// New nodes and changed nodes in one list, in the order this tree lists them, because
		// that order is the order a client hears them in. A node nothing happened to is not
		// here.
		std::vector<AccessibleChange> changes;
		// There is no last tree, or this one does not hold the same ids in the same order --
		// which a list being sorted is, with nothing added and nothing removed.
		bool     reshaped = false;
		uint32_t focused = 0;   // the id of the focused node in this tree; 0 for none
	};

	// `before` is null for the first tree there is, and every node in `now` is then new.
	AccessibleDiff diffAccessibleTrees(const AccessibleTree* before, const AccessibleTree& now);
}
