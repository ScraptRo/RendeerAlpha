#include <GraphicalObjects/DockTree.h>
#include <algorithm>
#include <sstream>

namespace RDA {

	namespace {
		// Edge bands are a share of the pane, capped so a large pane keeps a usable
		// center and a small one still has grabbable edges.
		constexpr float kEdgeShare = 0.30f;
		constexpr float kEdgeMax = 60.0f;

		std::unique_ptr<DockNode> makeLeaf(const std::string& id) {
			auto leaf = std::make_unique<DockNode>();
			if (!id.empty()) leaf->tabs.push_back(id);
			return leaf;
		}

		void detachFrom(DockNode* leaf, const std::string& id) {
			const int i = leaf->indexOf(id);
			if (i < 0) return;
			leaf->tabs.erase(leaf->tabs.begin() + i);
			if (leaf->active > i) --leaf->active;
			if (leaf->active >= static_cast<int>(leaf->tabs.size()))
				leaf->active = static_cast<int>(leaf->tabs.size()) - 1;
			if (leaf->active < 0) leaf->active = 0;
		}

		void gather(DockNode* node, std::vector<DockNode*>& out, bool wantLeaves) {
			if (!node) return;
			if (node->isLeaf()) { if (wantLeaves) out.push_back(node); return; }
			if (!wantLeaves) out.push_back(node);
			gather(node->first.get(), out, wantLeaves);
			gather(node->second.get(), out, wantLeaves);
		}

		void relink(DockNode* node, DockNode* parent) {
			if (!node) return;
			node->parent = parent;
			relink(node->first.get(), node);
			relink(node->second.get(), node);
		}
	}

	// ---- DockNode -----------------------------------------------------------------
	const std::string* DockNode::activeTab() const {
		if (!isLeaf() || tabs.empty()) return nullptr;
		int i = active;
		if (i < 0 || i >= static_cast<int>(tabs.size())) i = 0; // tolerate a stale index
		return &tabs[static_cast<size_t>(i)];
	}

	int DockNode::indexOf(const std::string& id) const {
		for (size_t i = 0; i < tabs.size(); ++i)
			if (tabs[i] == id) return static_cast<int>(i);
		return -1;
	}

	Rect DockNode::handleRect(float thickness) const {
		if (!isSplit()) return {};
		if (vertical) {
			const float avail = std::max(0.0f, rect.h - thickness);
			return { rect.x, rect.y + avail * ratio, rect.w, thickness };
		}
		const float avail = std::max(0.0f, rect.w - thickness);
		return { rect.x + avail * ratio, rect.y, thickness, rect.h };
	}

	// ---- DockLayout ---------------------------------------------------------------
	DockLayout::DockLayout() : mRoot(std::make_unique<DockNode>()) {}

	bool DockLayout::empty() const { return count() == 0; }

	size_t DockLayout::count() const {
		std::vector<DockNode*> leaves;
		collectLeaves(leaves);
		size_t n = 0;
		for (DockNode* leaf : leaves) n += leaf->tabs.size();
		return n;
	}

	void DockLayout::clear() {
		mRoot = std::make_unique<DockNode>();
	}

	void DockLayout::layout(Rect area, float thickness) {
		mThickness = thickness;
		layoutNode(mRoot.get(), area, thickness);
	}

	void DockLayout::layoutNode(DockNode* node, Rect area, float t) {
		node->rect = area;
		if (node->isLeaf()) return;
		if (node->vertical) {
			const float avail = std::max(0.0f, area.h - t);
			const float h1 = avail * node->ratio;
			layoutNode(node->first.get(), { area.x, area.y, area.w, h1 }, t);
			layoutNode(node->second.get(), { area.x, area.y + h1 + t, area.w, avail - h1 }, t);
		} else {
			const float avail = std::max(0.0f, area.w - t);
			const float w1 = avail * node->ratio;
			layoutNode(node->first.get(), { area.x, area.y, w1, area.h }, t);
			layoutNode(node->second.get(), { area.x + w1 + t, area.y, avail - w1, area.h }, t);
		}
	}

	void DockLayout::collectLeaves(std::vector<DockNode*>& out) const {
		gather(mRoot.get(), out, true);
	}
	void DockLayout::collectSplits(std::vector<DockNode*>& out) const {
		gather(mRoot.get(), out, false);
	}

	DockNode* DockLayout::leafFor(const std::string& id) const {
		std::vector<DockNode*> leaves;
		collectLeaves(leaves);
		for (DockNode* leaf : leaves)
			if (leaf->indexOf(id) >= 0) return leaf;
		return nullptr;
	}

	DockNode* DockLayout::leafAt(glm::vec2 point) const {
		std::vector<DockNode*> leaves;
		collectLeaves(leaves);
		for (DockNode* leaf : leaves)
			if (leaf->rect.contains(point)) return leaf;
		return nullptr;
	}

	DockDrop DockLayout::dropZoneAt(const DockNode* leaf, glm::vec2 point, Rect* previewOut) const {
		if (!leaf || !leaf->isLeaf() || !leaf->rect.contains(point)) return DockDrop::None;
		const Rect r = leaf->rect;
		const float bandX = std::min(r.w * kEdgeShare, kEdgeMax);
		const float bandY = std::min(r.h * kEdgeShare, kEdgeMax);

		// Nearest edge wins, but only if the pointer is actually inside that edge's band.
		float best = point.x - r.x, band = bandX;
		DockDrop where = DockDrop::Left;
		if (const float d = (r.x + r.w) - point.x; d < best) { best = d; band = bandX; where = DockDrop::Right; }
		if (const float d = point.y - r.y;         d < best) { best = d; band = bandY; where = DockDrop::Top; }
		if (const float d = (r.y + r.h) - point.y; d < best) { best = d; band = bandY; where = DockDrop::Bottom; }
		if (best > band) where = DockDrop::Center;

		if (previewOut) {
			switch (where) {
			case DockDrop::Left:   *previewOut = { r.x, r.y, r.w * 0.5f, r.h }; break;
			case DockDrop::Right:  *previewOut = { r.x + r.w * 0.5f, r.y, r.w * 0.5f, r.h }; break;
			case DockDrop::Top:    *previewOut = { r.x, r.y, r.w, r.h * 0.5f }; break;
			case DockDrop::Bottom: *previewOut = { r.x, r.y + r.h * 0.5f, r.w, r.h * 0.5f }; break;
			default:               *previewOut = r; break;
			}
		}
		return where;
	}

	void DockLayout::addTab(DockNode* leaf, const std::string& id) {
		if (!leaf || !leaf->isLeaf() || id.empty()) return;
		const int existing = leaf->indexOf(id);
		if (existing >= 0) { leaf->active = existing; return; }
		leaf->tabs.push_back(id);
		leaf->active = static_cast<int>(leaf->tabs.size()) - 1;
	}

	DockNode* DockLayout::split(DockNode* leaf, DockDrop side, const std::string& id, float ratio) {
		if (!leaf || !leaf->isLeaf() || id.empty()) return nullptr;
		if (side == DockDrop::None || side == DockDrop::Center) { addTab(leaf, id); return leaf; }
		// Splitting an empty pane would leave a blank child behind; just fill it.
		if (leaf->tabs.empty()) { addTab(leaf, id); return leaf; }

		auto existing = std::make_unique<DockNode>();
		existing->tabs = std::move(leaf->tabs);
		existing->active = leaf->active;
		auto fresh = makeLeaf(id);
		DockNode* result = fresh.get();

		leaf->kind = DockNode::Kind::Split;
		leaf->tabs.clear();
		leaf->active = 0;
		leaf->vertical = (side == DockDrop::Top || side == DockDrop::Bottom);

		if (side == DockDrop::Left || side == DockDrop::Top) {
			leaf->ratio = ratio;
			leaf->first = std::move(fresh);
			leaf->second = std::move(existing);
		} else {
			// `ratio` is always the newcomer's share, so mirror it when it lands second.
			leaf->ratio = 1.0f - ratio;
			leaf->first = std::move(existing);
			leaf->second = std::move(fresh);
		}
		leaf->first->parent = leaf;
		leaf->second->parent = leaf;
		return result;
	}

	DockNode* DockLayout::dockFirst(const std::string& id) {
		// Never a second copy: a panel already in the tree is merely selected. Without
		// this, any path that falls back to dockFirst() for an already-docked panel
		// would dock it twice and the two copies would fight over one widget's state.
		if (DockNode* existing = leafFor(id)) { existing->active = existing->indexOf(id); return existing; }

		std::vector<DockNode*> leaves;
		collectLeaves(leaves);
		DockNode* target = nullptr;
		for (DockNode* leaf : leaves) { if (leaf->tabs.empty()) { target = leaf; break; } }
		if (!target) target = leaves.empty() ? mRoot.get() : leaves.front();
		addTab(target, id);
		return target;
	}

	DockNode* DockLayout::splitRoot(DockDrop side, const std::string& id, float ratio) {
		if (id.empty()) return nullptr;
		if (side == DockDrop::None || side == DockDrop::Center) return dockFirst(id);
		if (leafFor(id)) remove(id);  // moving it here, not copying it
		if (empty()) return dockFirst(id); // nothing to split against yet

		auto fresh = makeLeaf(id);
		DockNode* result = fresh.get();
		auto split = std::make_unique<DockNode>();
		split->kind = DockNode::Kind::Split;
		split->vertical = (side == DockDrop::Top || side == DockDrop::Bottom);

		auto previous = std::move(mRoot);
		if (side == DockDrop::Left || side == DockDrop::Top) {
			split->ratio = ratio;
			split->first = std::move(fresh);
			split->second = std::move(previous);
		} else {
			split->ratio = 1.0f - ratio;
			split->first = std::move(previous);
			split->second = std::move(fresh);
		}
		mRoot = std::move(split);
		relink(mRoot.get(), nullptr);
		return result;
	}

	DockNode* DockLayout::dropOnto(DockNode* leaf, DockDrop where, const std::string& id) {
		if (id.empty()) return nullptr;
		if (!leaf || !leaf->isLeaf()) return dockFirst(id);

		DockNode* source = leafFor(id); // where it is docked now, if it is

		if (source == leaf) {
			// Dropped back onto its own pane.
			if (where == DockDrop::None || where == DockDrop::Center) { activate(id); return leaf; }
			// Tearing the only tab out of a pane and re-splitting that same pane would
			// leave one side empty; there is nothing to rearrange.
			if (leaf->tabs.size() <= 1) return leaf;
			detachFrom(leaf, id);
			return split(leaf, where, id);
		}

		// Moving a docked panel elsewhere: drop the old copy first so it cannot appear
		// twice. Collapsing lifts the surviving sibling into the split's place rather
		// than emptying it in situ, which is what keeps `leaf` valid across this.
		if (source) remove(id);

		if (where == DockDrop::None || where == DockDrop::Center) { addTab(leaf, id); return leaf; }
		return split(leaf, where, id);
	}

	bool DockLayout::remove(const std::string& id) {
		DockNode* leaf = leafFor(id);
		if (!leaf) return false;
		detachFrom(leaf, id);
		if (!leaf->tabs.empty()) return true;

		DockNode* parent = leaf->parent;
		if (!parent) return true; // the root pane, now simply empty

		std::unique_ptr<DockNode> keep =
			(parent->first.get() == leaf) ? std::move(parent->second) : std::move(parent->first);
		collapseInto(parent, std::move(keep));
		return true;
	}

	void DockLayout::collapseInto(DockNode* split, std::unique_ptr<DockNode> keep) {
		DockNode* grand = split->parent;
		keep->parent = grand;
		// The survivor takes the split's slot, so the split (and the emptied leaf under
		// it) are destroyed while every pointer into the surviving subtree stays good.
		if (!grand) { mRoot = std::move(keep); return; }
		if (grand->first.get() == split) grand->first = std::move(keep);
		else                             grand->second = std::move(keep);
	}

	bool DockLayout::activate(const std::string& id) {
		DockNode* leaf = leafFor(id);
		if (!leaf) return false;
		leaf->active = leaf->indexOf(id);
		return true;
	}

	void DockLayout::setRatioFromPointer(DockNode* split, glm::vec2 pointer, float thickness,
	                                     float minPane) {
		if (!split || !split->isSplit()) return;
		const float avail = (split->vertical ? split->rect.h : split->rect.w) - thickness;
		if (avail <= 1.0f) return;
		const float raw = (split->vertical ? (pointer.y - split->rect.y)
		                                   : (pointer.x - split->rect.x)) / avail;
		// Capped at 0.5 so the bounds stay ordered even in a pane too small for two.
		const float low = std::min(0.5f, minPane / avail);
		split->ratio = std::clamp(raw, low, 1.0f - low);
	}

	// ---- persistence --------------------------------------------------------------
	namespace {
		void writeNode(const DockNode* node, std::ostringstream& out) {
			if (!node) return;
			if (node->isSplit()) {
				out << "split " << (node->vertical ? 'v' : 'h') << ' ' << node->ratio << '\n';
				writeNode(node->first.get(), out);
				writeNode(node->second.get(), out);
				return;
			}
			out << "leaf " << node->active;
			for (const std::string& id : node->tabs) out << ' ' << id;
			out << '\n';
		}

		// Pre-order with fixed arity: a split is always followed by exactly its two
		// subtrees, so the stream reconstructs without brackets or indentation.
		std::unique_ptr<DockNode> readNode(const std::vector<std::string>& lines, size_t& i) {
			if (i >= lines.size()) return nullptr;
			std::istringstream ls(lines[i++]);
			std::string kind;
			ls >> kind;
			auto node = std::make_unique<DockNode>();
			if (kind == "split") {
				std::string axis; float ratio = 0.5f;
				ls >> axis >> ratio;
				node->kind = DockNode::Kind::Split;
				node->vertical = (axis == "v");
				node->ratio = std::clamp(ratio, 0.02f, 0.98f);
				node->first = readNode(lines, i);
				node->second = readNode(lines, i);
				if (!node->first || !node->second) return nullptr; // truncated stream
				return node;
			}
			if (kind != "leaf") return nullptr;
			ls >> node->active;
			std::string id;
			while (ls >> id) node->tabs.push_back(id);
			if (node->active < 0 || node->active >= static_cast<int>(node->tabs.size()))
				node->active = 0;
			return node;
		}
	}

	std::string DockLayout::save() const {
		std::ostringstream out;
		out << "rda-dock 2\n";
		writeNode(mRoot.get(), out);
		return out.str();
	}

	bool DockLayout::load(const std::string& text) {
		// The previous format is still readable; anything else is treated as current.
		std::istringstream head(text);
		std::string line;
		while (std::getline(head, line)) {
			if (line.empty()) continue;
			if (line.rfind("rda-dock 1", 0) == 0) return loadV1(text);
			break;
		}
		return loadV2(text);
	}

	bool DockLayout::loadV2(const std::string& text) {
		// Only node lines are consumed, so a caller may keep its own lines (floating
		// window rects, say) in the same file without disturbing the tree.
		std::vector<std::string> nodes;
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			if (line.rfind("split ", 0) == 0 || line.rfind("leaf", 0) == 0)
				nodes.push_back(line);
		}
		if (nodes.empty()) return false;

		size_t i = 0;
		std::unique_ptr<DockNode> root = readNode(nodes, i);
		if (!root) return false;
		mRoot = std::move(root);
		relink(mRoot.get(), nullptr);
		return true;
	}

	bool DockLayout::loadV1(const std::string& text) {
		// Five global slots, in the order the old layout carved them out of the window:
		// Left, Right, Top, Bottom, and whatever was left over as Center.
		enum { Left = 1, Right = 2, Top = 3, Bottom = 4, Center = 5, Slots = 6 };
		std::vector<std::string> ids[Slots];
		std::string activeId[Slots];
		float slotSize[Slots]{};
		float seedSize[Slots]{}; // first container's dockSize, used when the slot has none

		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream ls(line);
			char kind = 0;
			ls >> kind;
			if (kind == 'c') {
				std::string id, type; int side = 0; float fx, fy, fw, fh, dockSize = 0.0f;
				ls >> id >> type >> side >> fx >> fy >> fw >> fh >> dockSize;
				if (side >= Left && side <= Center) {
					if (ids[side].empty() && dockSize > 0.0f) seedSize[side] = dockSize;
					ids[side].push_back(id);
				}
			} else if (kind == 's') {
				int side = 0; float size = 0.0f; std::string active;
				ls >> side >> size >> active;
				if (side >= Left && side <= Center) {
					slotSize[side] = size;
					if (active != "-") activeId[side] = active;
				}
			}
		}

		auto leafFrom = [&](int slot) {
			auto leaf = std::make_unique<DockNode>();
			leaf->tabs = ids[slot];
			const int idx = leaf->indexOf(activeId[slot]);
			leaf->active = (idx >= 0) ? idx : 0;
			return leaf;
		};
		auto sizeOf = [&](int slot) {
			return slotSize[slot] > 0.0f ? slotSize[slot] : seedSize[slot];
		};

		std::unique_ptr<DockNode> tree = leafFrom(Center);

		// Rebuilt from the inside out, so the outermost wrap is the slot the old layout
		// carved first. Pixel sizes become fractions of a reference window; a layout
		// saved by the old code therefore returns proportioned rather than exact.
		auto wrap = [&](int slot, bool vertical, bool slotFirst, float span) {
			if (ids[slot].empty()) return;
			const float size = sizeOf(slot);
			float share = (size > 0.0f && span > 0.0f) ? (size / span) : 0.25f;
			share = std::clamp(share, 0.05f, 0.60f);
			auto split = std::make_unique<DockNode>();
			split->kind = DockNode::Kind::Split;
			split->vertical = vertical;
			split->ratio = slotFirst ? share : (1.0f - share);
			auto pane = leafFrom(slot);
			if (slotFirst) { split->first = std::move(pane);  split->second = std::move(tree); }
			else           { split->first = std::move(tree);  split->second = std::move(pane); }
			tree = std::move(split);
		};

		wrap(Bottom, true, false, kMigrationRefH);
		wrap(Top, true, true, kMigrationRefH);
		wrap(Right, false, false, kMigrationRefW);
		wrap(Left, false, true, kMigrationRefW);

		mRoot = std::move(tree);
		relink(mRoot.get(), nullptr);
		return true;
	}
}
