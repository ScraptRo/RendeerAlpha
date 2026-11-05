#pragma once
#include <GraphicalObjects/Widget.h>
#include <Layout/TableOrder.h>
#include <Layout/Selection.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// A table shown as a table: rows of a declared table under a row of column titles, sorted by
// clicking a title, filtered by a string, columns resized by dragging their edges.
//
// Where a <list> builds a few widgets and re-binds them as it scrolls, a grid builds none for
// its cells at all: a cell is a value from the table drawn as text, formatted by its column's
// type. That is what keeps a grid of a hundred thousand rows and ten columns at one widget --
// and it is the trade: a cell cannot hold a checkbox or a button. A row that needs widgets
// is a <list>.
//
// The columns are <column> children, which draw nothing themselves: they say which table
// column, what title, how wide, and how to align. Sorting and filtering are the shared view
// in TableOrder.h, so the table is never copied or reordered and the backend never hears
// about the order unless it asks.
namespace RDA {

	class Table;

	// One column of a <grid>. Read by the grid; never drawn by itself.
	//
	// How wide it is, is the `width` and `minWidth` every element has, read the way a stack
	// reads them: a number is that wide, "fill" shares what the fixed columns leave. Not
	// given, a column is kDefaultWidth.
	class GridColumn : public Widget {
	public:
		static constexpr float kDefaultWidth = 120.0f;
		static constexpr float kLeastWidth = 24.0f;   // what a drag cannot make it narrower than

		explicit GridColumn(std::string id) : Widget(std::move(id)) {}
		std::string field;        // the table column it shows
		std::string title;        // what its header says; the field's name when empty
		TextAlign   hAlign = TextAlign::Start;
		bool        sortable = true;
		int         decimals = -1;  // places for a number; -1 shows as many as it has

		bool  fills() const { return width.mode == SizeSpec::Mode::Fill; }
		float least() const { return width.min > 0.0f ? width.min : kLeastWidth; }

		bool inFlow() const override { return false; }
		void paint(Gui&, glm::vec2) override {}
	};

	class GridView : public Widget {
	public:
		explicit GridView(std::string id) : Widget(std::move(id)) {}

		std::string of;                      // the table
		std::string keyColumn = "id";        // what a row is known by, for `selected` and the handlers
		std::string selected;                // the chosen row's key; the grid reports choices with onSelect
		// Several rows chosen at once: Ctrl+click, Shift+click, Shift with the arrows. Then
		// `selected` is the row the keyboard is on, and the chosen ones are selectedKeys.
		bool        multiSelect = false;
		std::string sortBy;                  // a field; clicking a title sets it
		bool        sortDescending = false;
		std::string filter;                  // rows containing this, ignoring case
		std::string filterColumns;           // where to look, separated by '|'; empty is every text column
		float       rowHeight = 24.0f;
		float       headerHeight = 0.0f;     // 0 is a line of text and some room
		float       barWidth = 10.0f;
		float       wheelStep = 52.0f;
		Variant     variant = kDefaultVariant;  // a textfield variant, like a list's

		std::function<void(const std::string&)> onSelect;    // clicked or arrowed to
		std::function<void(const std::string&)> onActivate;  // double-clicked or Enter
		std::function<void(const std::string&)> onSort;      // "size", or "-size" for descending
		std::function<void(const std::string&)> onSelectionChange;  // the chosen keys, one per line

		// The chosen keys, one per line, as the layout's binding writes them.
		void setSelectedKeys(std::string_view joined) { mPicked.assign(joined); }
		const RowSelection& selection() const { return mPicked; }

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleItemAction(int item, AccessibleAction action, const std::string& value) override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;

		// How many rows are shown -- after the filter -- for the probe and for tests.
		size_t shownCount() const { return mOrder.size(); }
		// Which table row the n-th shown row is.
		size_t shownRow(size_t n) const { return n < mOrder.size() ? mOrder[n] : static_cast<size_t>(-1); }

	private:
		uint32_t mTable = 0xFFFFFFFFu;
		uint32_t mKeyCol = 0xFFFFFFFFu;

		// The view: which table rows, in what order, and what it was worked out from.
		std::vector<size_t> mOrder;
		TableViewSpec       mBuiltSpec;
		uint64_t            mBuiltVersion = 0;
		bool                mBuilt = false;
		uint64_t            mOrderEpoch = 0;   // bumped whenever mOrder is worked out again
		bool                mWarned = false;

		// Where the chosen row is shown, remembered against what it was found in, so a grid of
		// a hundred thousand rows is not searched every frame for the one that is selected.
		size_t      mAt = static_cast<size_t>(-1);
		std::string mAtKey;
		uint64_t    mAtEpoch = static_cast<uint64_t>(-1);

		float mOffsetY = 0.0f, mOffsetX = 0.0f;
		bool  mDragV = false, mDragH = false;
		float mGrab = 0.0f;
		// Widths a reader dragged, by field, which outlive the frame and win over the layout's.
		std::unordered_map<std::string, float> mResized;
		int   mResizing = -1;       // the column whose edge is held
		float mResizeFrom = 0.0f;   // pointer x minus that column's width when it was taken hold of
		int   mPressedHeader = -1;  // a title pressed, sorted on release if the pointer stays on it
		size_t mRevealShown = static_cast<size_t>(-1);

		// Row names for a screen reader, by table row; rebuilt with the table.
		std::vector<std::string> mNames;
		uint64_t mNamesVersion = 0;

		struct Placed { GridColumn* column; uint32_t field; float x; float w; };
		std::vector<Placed> placeColumns(const Table& table, float available);
		void        rebuildOrder(const Table& table);
		std::string keyOf(const Table& table, size_t row) const;
		void        choose(const Table& table, size_t row);
		std::string rowName(const Table& table, const std::vector<Placed>& columns, size_t row);

		RowSelection mPicked;
		// Applies a press or a key to the chosen rows, and says so if that changed them.
		void pick(const Table& table, Pick how, size_t shown);
		void reportPicked();
	};
}
