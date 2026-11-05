#include <GraphicalObjects/Gui/A11yCollector.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// A11yCollector: widgets describe themselves as the walk reaches them, and what they said is
// turned into a tree at the end of the frame.
namespace RDA {

	// ---- the frame ----------------------------------------------------------------
	void A11yCollector::beginFrame() {
		mThisFrame = false;
		// What a screen reader asked for since last frame. Taken before the cache decides,
		// because an action waiting is a reason to walk -- it is applied to its widget as
		// the walk reaches it, which is the only moment the widget is known to exist.
		if (mBridge && mBridge->wanted()) {
			std::vector<AccessibleRequest> asked = mBridge->takeRequests();
			for (AccessibleRequest& one : asked) mRequests.push_back(std::move(one));
		}
	}

	void A11yCollector::beginWalk() {
		// Walking, so describing -- when anybody is listening.
		mThisFrame = mBridge && mBridge->wanted();
		mRecords.clear();
		mSeen.clear();
	}

	void A11yCollector::forget() {
		mRecords.clear();
		mSeen.clear();
		mThisFrame = false;
	}

	void A11yCollector::endFrame(uint32_t focusedId, float unitScale) {
		if (!mThisFrame) return;
		build(focusedId, unitScale);
		mRequests.clear();
	}

	// ---- what the walk finds ------------------------------------------------------
	bool A11yCollector::noteWidget(Widget* widget, const Rect& where, const Interaction& ids, uint32_t& id) {
		if (!mThisFrame || !widget) return false;
		// Once per frame. A widget an item already stands for -- a list row -- is that
		// item, and a widget painted twice in one frame is still one widget.
		if (!mSeen.insert(widget).second) return false;
		Record record;
		if (!widget->describe(record.info)) {
			// Not a node of its own, but asked to be read out when it changes: a group,
			// named by everything it says, so a status made of two labels is one sentence.
			if (!widget->live) return false;
			record.info = AccessibleInfo{};
			record.info.role = AccessibleRole::Group;
			record.info.name = spokenText(*widget);
		}
		record.info.live = widget->live;
		record.widget = widget;
		record.id = ids.scopedId(widget->id().c_str());
		record.where = where;
		record.automationId = widget->id();
		mRecords.push_back(std::move(record));
		id = mRecords.back().id;
		return true;
	}

	bool A11yCollector::noteItem(Widget* owner, uint32_t id, int item, Widget* row,
	                             const Rect& where, const AccessibleInfo& info,
	                             const std::string& automationId, bool focused) {
		if (!mThisFrame || !owner) return false;
		Record record;
		record.owner = owner;
		record.row = row;
		record.item = item;
		record.id = id;
		record.where = where;
		record.info = info;
		record.automationId = automationId;
		record.focused = focused;
		if (row) mSeen.insert(row);
		mRecords.push_back(std::move(record));
		return true;
	}

	void A11yCollector::noteText(uint32_t id, AccessibleText text) {
		if (!mThisFrame) return;
		for (auto it = mRecords.rbegin(); it != mRecords.rend(); ++it) {
			if (it->id == id) { it->text = std::move(text); return; }
		}
	}

	std::vector<AccessibleRequest> A11yCollector::takeRequestsFor(uint32_t id) {
		std::vector<AccessibleRequest> taken;
		for (size_t i = 0; i < mRequests.size();) {
			if (mRequests[i].id != id) { ++i; continue; }
			taken.push_back(std::move(mRequests[i]));
			mRequests.erase(mRequests.begin() + static_cast<std::ptrdiff_t>(i));
		}
		return taken;
	}

	// Every name the visible widgets under `widget` have, in order, separated by spaces --
	// what a live container says when it changes.
	std::string A11yCollector::spokenText(const Widget& widget) {
		std::string out;
		for (const std::unique_ptr<Widget>& child : widget.children()) {
			if (!child || !child->visible) continue;
			AccessibleInfo info;
			if (child->describe(info)) {
				const std::string& said = info.value.empty() ? info.name : info.value;
				if (!said.empty()) { if (!out.empty()) out += ' '; out += said; }
				if (info.role != AccessibleRole::Group) continue;
			}
			const std::string inner = spokenText(*child);
			if (!inner.empty()) { if (!out.empty()) out += ' '; out += inner; }
		}
		return out;
	}

	void A11yCollector::build(uint32_t focusedId, float unitScale) {
		auto tree = std::make_shared<AccessibleTree>();
		tree->unitScale = unitScale;
		tree->version = ++mVersion;
		tree->nodes.reserve(mRecords.size());

		// Which node each widget became. An item counts for the row it stands for as well,
		// so what is inside a row hangs off the row.
		std::unordered_map<const Widget*, int> nodeOf;
		nodeOf.reserve(mRecords.size() * 2);
		// An item that stands in for its owner's focus takes it from the owner: the reader
		// is on the highlighted option, not on the dropdown around it.
		bool handedOn = false;
		for (const Record& r : mRecords) if (r.focused) handedOn = true;
		for (const Record& r : mRecords) {
			AccessibleNode node;
			node.id = r.id;
			node.info = r.info;
			node.bounds = r.where;
			node.automationId = r.automationId;
			node.text = r.text;
			node.focused = handedOn ? r.focused : (r.id != 0 && r.id == focusedId);
			const int at = static_cast<int>(tree->nodes.size());
			if (node.focused) tree->focused = at;
			tree->byId.emplace(node.id, at);
			tree->nodes.push_back(std::move(node));
			if (r.widget) nodeOf.emplace(r.widget, at);
			if (r.row) nodeOf.emplace(r.row, at);
		}

		// A node's parent is the nearest node above it in the widget tree. Containers that
		// are not nodes -- a stack, a scroll view -- are walked past, so their children
		// hang off whatever is.
		for (size_t i = 0; i < mRecords.size(); ++i) {
			const Record& r = mRecords[i];
			const int self = static_cast<int>(i);
			const Widget* from = r.owner ? r.owner : (r.widget ? r.widget->parent() : nullptr);
			int parent = -1;
			for (const Widget* w = from; w; w = w->parent()) {
				auto found = nodeOf.find(w);
				if (found != nodeOf.end() && found->second != self) { parent = found->second; break; }
			}
			tree->nodes[i].parent = parent;
			if (parent >= 0) tree->nodes[static_cast<size_t>(parent)].children.push_back(self);
			else             tree->roots.push_back(self);
		}

		mTree = tree;
		mBuilt = true;
		if (mBridge) mBridge->publish(std::move(tree));
	}
}
