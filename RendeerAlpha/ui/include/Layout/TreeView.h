#pragma once
#include <Layout/ListView.h>
#include <Layout/Selection.h>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

// A list of rows that hang off one another: a file tree, an outline, a scene graph.
//
// The rows are a declared table like a <list>'s, written in the order they are read --
// each node, then everything under it -- with a `depth` column saying how far in it is.
// That is the whole of the shape. Nothing is nested, so a tree with fifty thousand nodes is
// fifty thousand rows in a table and about thirty widgets on screen, the same bargain a
// list makes; and a backend fills it with the loop it already has for a list, plus one
// number per row.
//
// What is open is the tree's own business, the way what is typed into a field is: it is
// what the reader did, and it is remembered by each node's `id` so it survives the table
// being refilled. The backend hears about it -- onExpand, onCollapse -- which is how a
// tree loads its children only when somebody opens a node: a row says `hasChildren`, the
// reader opens it, the backend appends what is under it.
//
// Built on ListView rather than beside it. A tree is a list that shows some of its rows,
// moved in by their depth, with an arrow in front of the ones that have something under
// them -- so the pool, the scrolling, the bar, the row template and the per-row handlers
// are all the list's, and nothing about them had to be written twice.
namespace RDA {

	class TreeView : public ListView {
	public:
		explicit TreeView(std::string id) : ListView(std::move(id)) { rowHeight = 24.0f; }

		// The columns it reads. Each has a default, so a table with `depth` and `id`
		// columns needs nothing said.
		std::string depthColumn = "depth";       // number: 0 at the top
		std::string keyColumn = "id";            // what a node is known by
		// bool, optional: whether a node has something under it before anything is. Without
		// it, a node has children when the row after it is deeper.
		std::string childrenColumn = "hasChildren";

		float indent = 16.0f;   // how far in each level is, and how wide the arrow's column is
		// Nodes shallower than this start open. 0 starts everything shut; a large number
		// starts everything open. Only a start: once the reader has opened or shut a node,
		// that is what it is.
		int   openDepth = 0;

		// The chosen node, by its key. Bindable, and the layout owns it: the tree reports a
		// choice with onSelect and does not write it, the rule every two-way property here
		// follows.
		std::string selected;

		std::function<void(const std::string&)> onSelect;    // clicked, or arrowed to
		std::function<void(const std::string&)> onActivate;  // double-clicked, or Enter
		std::function<void(const std::string&)> onExpand;    // opened
		std::function<void(const std::string&)> onCollapse;  // shut

		// Several nodes chosen at once, the way a <grid> does it: Ctrl+click, Shift+click,
		// Shift with the arrows, Ctrl+A. `selected` is then the node the keyboard is on.
		bool multiSelect = false;
		std::function<void(const std::string&)> onSelectionChange;  // the chosen keys, one per line
		void setSelectedKeys(std::string_view joined) { mPicked.assign(joined); }
		const RowSelection& selection() const { return mPicked; }

		bool isOpen(size_t row) const;
		std::string keyOf(size_t row) const;

		bool describe(AccessibleInfo& out) const override;
		void accessibleItemAction(int item, AccessibleAction action, const std::string& value) override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;

	protected:
		size_t shownRows(const Table& table) const override;
		size_t tableRow(size_t shown) const override;
		size_t shownIndexOf(size_t row) const override;
		void   prepare(Gui& gui, const Rect& view, Table& table) override;
		Rect   arrangeRow(Gui& gui, const Rect& placed, size_t row) override;
		void   describeRow(Gui& gui, size_t row, Widget* root, const Rect& placed) override;

	private:
		const Table* mTableSeen = nullptr;
		uint32_t mDepthCol = 0xFFFFFFFFu;
		uint32_t mKeyCol = 0xFFFFFFFFu;
		uint32_t mKidsCol = 0xFFFFFFFFu;
		uint64_t mColumnsVersion = 0;
		bool     mWarnedDepth = false;

		// What is shown, in order, as table rows; and the other way round.
		std::vector<size_t> mShown;
		std::vector<size_t> mShownAt;
		uint64_t mShownVersion = 0;
		bool     mShownStale = true;

		// What the reader changed from where `openDepth` started it, by key.
		std::unordered_set<std::string> mOpened;
		std::unordered_set<std::string> mShut;

		// Opened or shut by a press during this frame's walk, applied at the start of the
		// next -- the rows are being placed from the shown list while the press is read,
		// and changing it under them would put this frame's rows in the wrong places.
		size_t mPendingToggle = static_cast<size_t>(-1);

		RowSelection mPicked;
		void pick(Pick how, size_t shown);
		void reportPicked();
		bool chosen(size_t row) const;

		int    depthOf(size_t row) const;
		bool   hasChildren(size_t row) const;
		void   resolveColumns(const Table& table);
		void   rebuildShown(const Table& table);
		void   toggle(size_t row);
		void   choose(size_t row);
		size_t parentOf(size_t shown) const;
		void   keyboard(Gui& gui);
	};
}
