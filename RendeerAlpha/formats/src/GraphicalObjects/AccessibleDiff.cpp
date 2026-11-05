#include <GraphicalObjects/AccessibleDiff.h>

namespace RDA {

	namespace {

		// The same ids in the same order. Not the same thing as nothing having been added or
		// removed: a sorted list has neither, and a client still has to read it again.
		bool sameShape(const AccessibleTree& before, const AccessibleTree& now) {
			if (before.nodes.size() != now.nodes.size()) return false;
			for (size_t i = 0; i < now.nodes.size(); ++i) {
				if (now.nodes[i].id != before.nodes[i].id) return false;
			}
			return true;
		}

		AccessibleChange compare(const AccessibleNode& before, const AccessibleNode& now) {
			const AccessibleInfo& a = before.info;
			const AccessibleInfo& b = now.info;
			AccessibleChange change;
			change.before = &before;
			change.now = &now;
			change.toggled  = a.toggled != b.toggled && b.toggled >= 0;
			change.expanded = a.expanded != b.expanded && b.expanded >= 0;
			change.selected = a.selected != b.selected && b.selected >= 0;
			change.name     = a.name != b.name;
			change.value    = a.value != b.value;
			change.range    = b.hasRange && a.rangeValue != b.rangeValue;
			change.caret    = now.text.present && before.text.caret != now.text.caret;
			change.anchor   = now.text.present && before.text.anchor != now.text.anchor;
			return change;
		}

		bool anythingIn(const AccessibleChange& c) {
			return c.toggled || c.expanded || c.selected || c.name || c.value || c.range ||
			       c.caret || c.anchor;
		}
	}

	AccessibleDiff diffAccessibleTrees(const AccessibleTree* before, const AccessibleTree& now) {
		AccessibleDiff diff;
		diff.reshaped = !before || !sameShape(*before, now);
		diff.focused = now.focused >= 0 ? now.nodes[static_cast<size_t>(now.focused)].id : 0;

		if (before) {
			for (const AccessibleNode& node : before->nodes) {
				if (now.find(node.id) < 0) diff.removed.push_back(&node);
			}
		}
		for (const AccessibleNode& node : now.nodes) {
			const int was = before ? before->find(node.id) : -1;
			if (was < 0) {
				AccessibleChange arrived;
				arrived.now = &node;
				diff.changes.push_back(arrived);
				continue;
			}
			const AccessibleChange change = compare(before->nodes[static_cast<size_t>(was)], node);
			if (anythingIn(change)) diff.changes.push_back(change);
		}
		return diff;
	}
}
