#pragma once
#include <cstdint>
#include <GraphicalObjects/GuiTypes.h>
#include <memory>
#include <string>
#include <vector>

// The arrangement of docked panels, as a tree of splits.
//
// A node is one of two things:
//
//   Split - an orientation, a ratio, and exactly two children.
//   Leaf  - a group of containers shown as tabs, one of them active.
//
// so a window like
//
//   +----------+---------------------+
//   |          |      viewport       |
//   | explorer +---------------------+
//   |          | console | problems  |
//   +----------+---------------------+
//
// is the tree
//
//   split h 0.25
//     leaf [explorer]
//     split v 0.70
//       leaf [viewport]
//       leaf [console, problems]
//
// This replaces the previous model of five fixed slots (Left/Right/Top/Bottom/Center),
// which could only ever describe one panel per edge because a panel's position was a
// global property. Here position is relative: a panel sits somewhere *within* another
// pane, so any arrangement nests to any depth.
//
// Containers are referenced by id rather than by pointer. That keeps the tree free of
// the widget layer's lifetime rules, makes saving it a direct write of what is in
// memory, and lets the whole structure be exercised without constructing a single
// widget. Ids must not contain whitespace (the save format is line/space delimited).
//
// Only *docked* containers appear here. A floating container is simply one the tree
// does not mention, which keeps the two representations from disagreeing.
namespace RDA {

	// Where a drop lands relative to the leaf under the pointer. Center joins that
	// leaf's tab group; the four edges split it and put the newcomer on that side.
	enum class DockDrop : uint8_t { None, Center, Left, Right, Top, Bottom };

	class DockNode {
	public:
		enum class Kind : uint8_t { Leaf, Split };

		Kind kind = Kind::Leaf;

		// ---- Split ----
		// `vertical` describes how the area is cut: true stacks the children top over
		// bottom, false places them left beside right. `ratio` is the first child's
		// share of the axis, after the splitter gap is taken out.
		bool  vertical = false;
		float ratio = 0.5f;
		std::unique_ptr<DockNode> first, second;

		// ---- Leaf ----
		std::vector<std::string> tabs;
		int active = 0;

		DockNode* parent = nullptr;
		Rect rect{};        // filled by DockLayout::layout()

		bool isLeaf()  const { return kind == Kind::Leaf; }
		bool isSplit() const { return kind == Kind::Split; }
		bool empty()   const { return isLeaf() && tabs.empty(); }

		// The tab currently shown, or nullptr for an empty leaf. `active` is kept in
		// range here rather than trusted, so a stale index can never index out.
		const std::string* activeTab() const;
		int indexOf(const std::string& id) const;

		// The gap between the two children, i.e. the draggable splitter. Only meaningful
		// on a Split, and only after layout().
		Rect handleRect(float thickness) const;
	};

	class DockLayout {
	public:
		DockLayout();

		DockNode* root() const { return mRoot.get(); }
		// True when nothing is docked at all, so the caller can give the whole window to
		// something else rather than draw an empty pane.
		bool empty() const;
		size_t count() const;

		// Assigns every node its rect. `thickness` is the splitter gap reserved between
		// a split's children, and must match what collectHandles() is later given.
		void layout(Rect area, float thickness = 6.0f);

		// ---- queries (rect-based ones need a layout() first) ----
		DockNode* leafFor(const std::string& id) const;
		DockNode* leafAt(glm::vec2 point) const;
		bool contains(const std::string& id) const { return leafFor(id) != nullptr; }
		void collectLeaves(std::vector<DockNode*>& out) const;
		void collectSplits(std::vector<DockNode*>& out) const;

		// Which part of `leaf` the pointer is over. The edge bands are a fraction of the
		// pane, bounded so a small pane does not become all edge and no center.
		DockDrop dropZoneAt(const DockNode* leaf, glm::vec2 point, Rect* previewOut) const;

		// ---- mutation ----
		// Adds to a leaf's tab group and makes it active. Safe on any leaf in this tree.
		void addTab(DockNode* leaf, const std::string& id);
		// Turns `leaf` into a split, with `id` alone in a new leaf on the given side and
		// the original contents moved into the other. Returns the leaf holding `id`.
		//
		// Note that `leaf` is converted *in place* — it has to be, since it occupies its
		// parent's slot — so afterwards the pointer you passed in names the split, not a
		// pane, and its former contents live in one of its children. Do not keep pane
		// pointers across a split; re-acquire them with leafFor().
		DockNode* split(DockNode* leaf, DockDrop side, const std::string& id, float ratio = 0.5f);
		// The two above, chosen by the drop zone. `where` == None or Center adds a tab.
		DockNode* dropOnto(DockNode* leaf, DockDrop where, const std::string& id);
		// Splits the whole tree, so `id` gets a strip across the full width or height
		// rather than a share of one pane. This is what a drop on the window's own edge
		// means, and what the old five-slot layout did for every docked panel.
		DockNode* splitRoot(DockDrop side, const std::string& id, float ratio = 0.25f);
		// Docks into the tree when it is empty, so the first drop has somewhere to go.
		DockNode* dockFirst(const std::string& id);

		// Detaches a container. When that empties a leaf, the leaf's parent split is
		// replaced by the surviving sibling, so the tree never keeps a blank pane.
		bool remove(const std::string& id);
		// Makes `id` the active tab of whichever leaf holds it.
		bool activate(const std::string& id);
		void clear();

		// ---- splitter dragging ----
		// Sets a split's ratio from a pointer position, clamped so neither child can be
		// squeezed below `minPane` pixels.
		void setRatioFromPointer(DockNode* split, glm::vec2 pointer, float thickness,
		                         float minPane = 90.0f);

		// ---- persistence ----
		// Pre-order text: a split is always followed by its two subtrees, so no
		// indentation or delimiters are needed to reconstruct it.
		std::string save() const;
		// Accepts the current format and migrates the previous five-slot one. Returns
		// false (leaving the layout untouched) if the text parses to nothing usable.
		bool load(const std::string& text);

		// The reference window the v1 migration converts absolute slot sizes against.
		// v1 stored pixel widths and the tree stores fractions, so a layout saved by the
		// old code comes back proportioned for a window of about this size.
		static constexpr float kMigrationRefW = 1280.0f;
		static constexpr float kMigrationRefH = 720.0f;

	private:
		void layoutNode(DockNode* node, Rect area, float thickness);
		// Lifts `keep` into the place occupied by its parent split, discarding the split
		// and the other child.
		void collapseInto(DockNode* split, std::unique_ptr<DockNode> keep);
		bool loadV1(const std::string& text);
		bool loadV2(const std::string& text);

		std::unique_ptr<DockNode> mRoot;
	};
}
