#pragma once
#include <cstdint>
#include <Core/Signals.h>
#include <GraphicalObjects/Widget.h>
#include <Layout/Expression.h>
#include <memory>
#include <Layout/TableOrder.h>
#include <string>
#include <vector>

// A list of rows from a table, showing only as many widgets as fit on screen.
//
// The template is compiled once and instantiated a fixed number of times -- about as many
// as can be visible. Scrolling does not create or destroy anything: it changes which row
// index each pooled copy is showing, and re-evaluates that copy's bindings against it.
//
// Which is not a new mechanism. Rebinding a widget to a different value is the one thing
// this whole system does; a list is that, pointed at a row instead of a signal. The
// recycling everyone talks about is the easy half -- the hard half was having somewhere
// for ten thousand rows to live that is not ten thousand widgets, and that is the table.
namespace RDA {

	class Table;

	class ListView : public Widget {
	public:
		explicit ListView(std::string id) : Widget(std::move(id)) {}

		std::string of;             // which table
		float rowHeight = 28.0f;
		// A number column in the same table holding each row's own height, for a list
		// whose rows are not all alike -- a transcript where one message is a line and
		// the next is a paragraph.
		//
		// Named rather than measured. Measuring would mean binding every row and laying
		// it out to find out how tall it came to, which is the one cost this widget
		// exists to avoid: ten thousand rows would be ten thousand binds on every change
		// to the table. A column is one read per row and the sender already knows -- it
		// has rda_measure_text for exactly this.
		//
		// Empty is the ordinary list, and that path is untouched: no per-row array, no
		// search, a multiply as before. A row whose height is absent, zero or negative
		// falls back to `rowHeight`, so a column filled in for some rows and not others
		// is a list with a sensible default rather than a list with holes in it.
		std::string rowHeights;
		float spacing = 0.0f;
		float offset = 0.0f;        // how far down the list has been scrolled, in pixels
		float barWidth = 10.0f;
		float wheelStep = 52.0f;
		// Stay at the end when rows arrive -- but only while the reader is already there.
		//
		// A transcript that follows a streaming reply and a transcript that stays put
		// while you read back through it are the same list in two moods, and which one
		// you want is decided by where you have scrolled to, not by a setting. So this
		// does not pin to the bottom: it re-pins only if the view was at the bottom
		// before the content grew.
		bool  follow = false;

		// Scroll so this row is in view, then forget it was asked.
		//
		// A number rather than a call, so it is bindable like everything else: a search
		// result writes the row it found and the list goes there. Negative means nothing
		// is being asked for, which is also where it returns to -- otherwise re-binding
		// the same value would drag the reader back every time the list was re-bound.
		int   revealRow = -1;
		Variant variant = kDefaultVariant;

		// The table seen sorted or filtered, without the table changing: see TableOrder.h.
		// A <list> only -- a tree's order is its hierarchy, and these leave a tree alone.
		std::string sortBy;
		bool        sortDescending = false;
		std::string filter;
		std::string filterColumns;

		// What brings a pooled row up to date with the table row it shows.
		//
		// The list knows *when*: a row scrolled into a slot, the table moved, or something
		// the rows read changed. Whoever built the rows knows *how*, because they are its
		// bindings, compiled from the template and evaluated on its script host. So the
		// list asks, and the layout runtime answers -- which keeps a widget from depending
		// on the binding engine that sits above it.
		struct RowBinder {
			virtual ~RowBinder() = default;
			// Applies the bindings of the row in `slot` against table row `row`.
			virtual void bind(size_t slot, const Layout::RowRef& row) = 0;
			// What the template's text says for `row`, or empty: a row's name for a
			// screen reader, read without that row having to be on screen.
			virtual std::string text(const Layout::RowRef& row) = 0;
		};
		void setRowBinder(std::shared_ptr<RowBinder> binder) { mRowBinder = std::move(binder); }

		// Filled by the loader as it instantiates the template.
		void beginRow(Widget* root) { mPool.push_back(PooledRow{ root, kNoRow }); }
		// How many rows exist, built so far.
		size_t poolCount() const { return mPool.size(); }
		// Builds one more row. Set by the loader, which is what knows how; called by the list
		// when its view needs more rows than it has.
		void setRowFactory(std::function<void()> build) { mRowFactory = std::move(build); }
		// The slot the row just begun occupies, for a handler that needs to find its way
		// back here when it fires.
		size_t currentSlot() const { return mPool.empty() ? 0 : mPool.size() - 1; }

		// Which row `slot` is showing right now, as something evaluate() can take.
		//
		// This is the whole of what a handler inside a row template was missing. A value
		// binding is re-evaluated by rebind(), which knows the row because it chose it; a
		// handler fires later, from a click, long after that call returned. The pool is
		// the only thing that still knows, so the handler asks it.
		//
		// False when the slot is showing nothing -- a pooled row scrolled past the end of
		// a table that shrank, or a click that arrived in the same frame the list emptied.
		bool rowRefFor(size_t slot, Layout::RowRef& out) const;

		// A signal one of the row bindings reads has moved, so every built row is stale.
		//
		// A row binding is not registered with the signal graph the way an ordinary one
		// is -- a signal write cannot say which row is meant, so they are applied when a
		// row is bound and at no other time. That left a row that read both `item.x` and
		// `state.y` following the first and ignoring the second, silently. The list now
		// observes those signals on its rows' behalf and re-binds all of them, which is
		// the same answer at the only granularity available.
		void invalidateRows() { mRowsDirty = true; ++mNamesEpoch; }
		// How many rows the table holds, or 0 before the first paint resolves it.
		size_t rowCount() const;

		// Whether this list's rows are all the same height. False only when `rowHeights`
		// names a column that exists.
		bool uniformRows() const { return mHeightColumn == 0xFFFFFFFFu; }

		// A list puts its pooled rows in new places every frame as it scrolls, so easing
		// that is not a slide, it is a smear. Children stop looking for a duration here.
		bool inheritsMotion() const override { return false; }

		// Not its rows. A row is a pooled copy showing whichever row of the table scrolled
		// into it, so a shortcut in one would act on a row nobody chose -- and with the
		// list out of sight, on one nobody can see.
		void offerShortcuts(std::vector<Widget*>& out) override { (void)out; }

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;

	protected:
		static constexpr size_t kNoRow = static_cast<size_t>(-1);

		// ---- for a list that does not show every row ---------------------------------
		//
		// A list shows its table's rows in order, all of them. A tree shows some of them --
		// the ones whose parents are open -- and these are the three questions that makes
		// different. "Shown" counts what is on the list, in order; the table index is
		// which row of the table that is. For a list they are the same number, which is
		// what the defaults say, and the list pays nothing for the tree existing.
		virtual size_t shownRows(const Table& table) const;
		virtual size_t tableRow(size_t shown) const {
			if (!mViewOn) return shown;
			return shown < mView.size() ? mView[shown] : kNoRow;
		}
		// Where a table row is on the list, or kNoRow when it is not shown.
		virtual size_t shownIndexOf(size_t row) const {
			if (!mViewOn) return row;
			return row < mViewAt.size() ? mViewAt[row] : kNoRow;
		}
		// Called once a frame, after the table is found and before anything is placed.
		virtual void prepare(Gui& gui, const Rect& view, Table& table) {
			(void)gui; (void)view; (void)table;
		}
		// The rect a row's widgets are laid out in, given the one the list gave it. A tree
		// draws its disclosure arrow and selection here and returns what is left.
		virtual Rect arrangeRow(Gui& gui, const Rect& placed, size_t row) {
			(void)gui; (void)row;
			return placed;
		}
		// A row, as a screen reader is told about it: an item of this list, named by the
		// first text in it, standing for the row's widgets so what is in the row hangs
		// off it. Only the rows on screen -- the same ones that have widgets.
		//
		// `root` is null for a row scrolled out of view: it is described from the table
		// alone, so a screen reader can count the whole list and read any row of it.
		virtual void describeRow(Gui& gui, size_t row, Widget* root, const Rect& placed);
		// What a row is called: the first text its template's bindings give it, worked out
		// from the table without any widget, and remembered until the table or what the
		// bindings read changes. The same for a row on screen and off, so a row reads the
		// same wherever it is.
		std::string rowName(size_t row, Widget* root);
		// Scroll a row into view, from outside. A tree adds its own actions on top.
		void accessibleItemAction(int item, AccessibleAction action, const std::string& value) override;
		// Scroll until this shown row is in view, the way revealRow does for a table row.
		void revealShown(size_t shown) { mRevealShown = shown; }
		Rect lastView() const { return mLastView; }

	private:
		// What `follow` compares against: how far it could scroll last frame, and whether
		// it was there. Both are answers about the previous frame on purpose -- by the
		// time this frame's maxOffset is known, the content has already grown.
		bool  mRowsDirty = false;
		std::shared_ptr<RowBinder> mRowBinder;
		std::function<void()> mRowFactory;
		// Row names for a screen reader, by table row, and what they were worked out from.
		std::vector<std::string> mNames;
		std::vector<bool>        mNamed;
		uint64_t                 mNamesVersion = 0;
		uint64_t                 mNamesEpoch = 0;
		uint64_t                 mNamesBuiltEpoch = static_cast<uint64_t>(-1);
		int   mRevealed = -1;   // the request that has already been honoured
		float mLastMaxOffset = 0.0f;
		bool  mWasAtEnd = true;   // a list that has never scrolled is at its end
		size_t mRevealShown = static_cast<size_t>(-1);
		Rect   mLastView{};

		struct PooledRow {
			Widget* root = nullptr;
			size_t  showing = kNoRow;
		};

		void rebind(PooledRow& row, size_t index);

		// The sorted or filtered view, when one is asked for: which table rows, in order, and
		// where each table row landed. Off, a list is its table in order and pays nothing.
		bool                mViewOn = false;
		std::vector<size_t> mView;
		std::vector<size_t> mViewAt;
		TableViewSpec       mViewSpec;
		uint64_t            mViewVersion = 0;
		void updateView(const Table& table);

		std::vector<PooledRow> mPool;
		uint32_t mTable = 0xFFFFFFFFu;
		bool     mResolved = false;
		bool     mWarnedTable = false;
		bool     mWarnedPool = false;
		uint64_t mSeenVersion = 0;
		bool     mDraggingThumb = false;
		float    mThumbGrab = 0.0f;

		// --- rows that are not all the same height ---
		//
		// mRowTop[i] is where row i begins, and mRowTop[rows] is the whole content's
		// height: a running total, which is the price the flat version does not pay and
		// the only way a search or a scroll offset can be answered without walking.
		//
		// Rebuilt when the table moves or when anything the totals were computed from
		// changes. Nothing else is cached from it, so a rebuild is one pass and no
		// invalidation to get wrong.
		uint32_t mHeightColumn = 0xFFFFFFFFu;
		bool     mWarnedHeightColumn = false;
		std::vector<float> mRowTop;
		uint64_t mBuiltVersion = 0;
		uint64_t mBuiltEpoch = static_cast<uint64_t>(-1);
		size_t   mBuiltRows = static_cast<size_t>(-1);
		float    mBuiltRowHeight = -1.0f;
		float    mBuiltSpacing = -1.0f;

		// Row `index`'s height, and where it begins. Both answer for a uniform list too,
		// so the code that places rows does not have to ask which kind of list it is in.
		float heightOf(const Table& table, size_t index) const;
		float topOf(size_t index) const;
		void  rebuildTops(const Table& table);
	};
}
