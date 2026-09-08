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

	class ListView : public Widget {
	public:
		explicit ListView(std::string id) : Widget(std::move(id)) {}

		std::string of;             // which table
		float rowHeight = 28.0f;
		float spacing = 0.0f;
		float offset = 0.0f;        // how far down the list has been scrolled, in pixels
		float barWidth = 10.0f;
		float wheelStep = 52.0f;
		Variant variant = kDefaultVariant;

		// One compiled binding belonging to a pooled row. Held here rather than in the
		// global binding runtime because a signal write cannot say which row is meant:
		// these are applied when the list rebinds a row, and at no other time.
		struct RowBinding {
			Layout::Program       program;
			std::vector<uint32_t> ids;      // column indices, and signal ids for the rest
			Widget*               target = nullptr;
			std::string           property;
		};

		// Filled by the loader as it instantiates the template.
		void beginRow(Widget* root) { mPool.push_back(PooledRow{ root, {}, kNoRow }); }
		void addRowBinding(RowBinding binding) {
			if (!mPool.empty()) mPool.back().bindings.push_back(std::move(binding));
		}
		// How many rows the table holds, or 0 before the first paint resolves it.
		size_t rowCount() const;

		// A list puts its pooled rows in new places every frame as it scrolls, so easing
		// that is not a slide, it is a smear. Children stop looking for a duration here.
		bool inheritsMotion() const override { return false; }

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		static constexpr size_t kNoRow = static_cast<size_t>(-1);

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
	};
}
