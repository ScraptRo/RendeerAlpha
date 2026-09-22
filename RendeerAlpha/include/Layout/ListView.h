#pragma once
#include <cstdint>
#include <GraphicalObjects/Widget.h>
#include <Layout/Expression.h>
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

		// One compiled binding belonging to a pooled row. Held here rather than in the
		// global binding runtime because a signal write cannot say which row is meant:
		// these are applied when the list rebinds a row, and at no other time.
		struct RowBinding {
			Layout::Program       program;
			std::vector<uint32_t> ids;      // column indices, and signal ids for the rest
			Widget*               target = nullptr;
			std::string           property;
			bool                  warned = false; // said once, not once per rebind
		};

		// Filled by the loader as it instantiates the template.
		void beginRow(Widget* root) { mPool.push_back(PooledRow{ root, {}, kNoRow }); }
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
		void invalidateRows() { mRowsDirty = true; }
		void addRowBinding(RowBinding binding) {
			if (!mPool.empty()) mPool.back().bindings.push_back(std::move(binding));
		}
		// How many rows the table holds, or 0 before the first paint resolves it.
		size_t rowCount() const;

		// Whether this list's rows are all the same height. False only when `rowHeights`
		// names a column that exists.
		bool uniformRows() const { return mHeightColumn == 0xFFFFFFFFu; }

		// A list puts its pooled rows in new places every frame as it scrolls, so easing
		// that is not a slide, it is a smear. Children stop looking for a duration here.
		bool inheritsMotion() const override { return false; }

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		static constexpr size_t kNoRow = static_cast<size_t>(-1);
		// What `follow` compares against: how far it could scroll last frame, and whether
		// it was there. Both are answers about the previous frame on purpose -- by the
		// time this frame's maxOffset is known, the content has already grown.
		bool  mRowsDirty = false;
		int   mRevealed = -1;   // the request that has already been honoured
		float mLastMaxOffset = 0.0f;
		bool  mWasAtEnd = true;   // a list that has never scrolled is at its end

		struct PooledRow {
			Widget*                 root = nullptr;
			std::vector<RowBinding> bindings;
			size_t                  showing = kNoRow;
		};

		void rebind(PooledRow& row, size_t index);

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
