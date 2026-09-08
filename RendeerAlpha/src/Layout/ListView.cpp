#include <Layout/ListView.h>
#include <Layout/Bindings.h>
#include <Core/Tables.h>
#include <Core/Signals.h>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <cmath>

namespace RDA {

	using namespace RDA::Layout;

	size_t ListView::rowCount() const {
		if (!mResolved || mTable == kNoTable) return 0;
		return tables().at(mTable).rows();
	}

	void ListView::rebind(PooledRow& row, size_t index) {
		Table& table = tables().at(mTable);
		const RowRef ref{ &table, index };
		for (RowBinding& binding : row.bindings) {
			if (!binding.target) continue;
			const EvalResult result = evaluate(binding.program, signals(), binding.ids,
			                                   nullptr, &ref);
			if (!result.ok) {
				RDA_LOG_WARNING("list " << mId << ": " << binding.property << " - " << result.error);
				continue;
			}
			applyBoundValue(*binding.target, binding.property, result.value);
		}
		row.showing = index;
	}

	glm::vec2 ListView::measureContent(Gui& gui, glm::vec2 available) const {
		(void)gui;
		const float pitch = rowHeight + spacing;
		const float total = static_cast<float>(rowCount()) * pitch;
		return { available.x, total };
	}

	void ListView::paint(Gui& gui, glm::vec2 origin) {
		const Rect view = placement(gui, origin);

		if (!mResolved) {
			mResolved = true;
			mTable = tables().find(of);
		}
		if (mTable == kNoTable) {
			if (!mWarnedTable) {
				mWarnedTable = true;
				RDA_LOG_WARNING("list " << mId << ": there is no table named '" << of << "'");
			}
			return;
		}

		Table& table = tables().at(mTable);
		const size_t rows = table.rows();
		const float pitch = (std::max)(1.0f, rowHeight + spacing);

		// The bar's width is always reserved, never depending on whether it shows: the
		// other way round, turning it on narrows the rows, which can turn it off again.
		const Rect inner{ view.x, view.y, (std::max)(0.0f, view.w - barWidth), view.h };
		const float content = static_cast<float>(rows) * pitch;
		const float maxOffset = (content > inner.h) ? (content - inner.h) : 0.0f;

		const GuiInput& in = gui.input();
		if (maxOffset > 0.0f && in.scroll != 0.0f && !gui.scrollConsumed() && view.contains(in.pointer)) {
			offset -= in.scroll * wheelStep;
			gui.consumeScroll();
		}

		const Rect track{ view.x + view.w - barWidth, view.y, barWidth, view.h };
		float thumbHeight = 0.0f, range = 0.0f;
		if (maxOffset > 0.0f) {
			thumbHeight = (std::max)(28.0f, view.h * (view.h / (std::max)(content, 1.0f)));
			range = (std::max)(0.0f, track.h - thumbHeight);
			if (!mDraggingThumb && in.pressed && track.contains(in.pointer)) {
				const float at = track.y + (offset / maxOffset) * range;
				mThumbGrab = (in.pointer.y < at || in.pointer.y > at + thumbHeight)
					? thumbHeight * 0.5f : in.pointer.y - at;
				mDraggingThumb = true;
			}
			if (mDraggingThumb && range > 0.0f) {
				offset = ((in.pointer.y - mThumbGrab - track.y) / range) * maxOffset;
			}
		}
		if (in.released) mDraggingThumb = false;
		offset = std::clamp(offset, 0.0f, maxOffset);

		// See Scroll::paint: `offset` is where it is scrolled to, this is where it is drawn
		// scrolled to, and a thumb being dragged is not eased because a drag is the hand.
		//
		// Everything below works from the drawn value -- which rows to bind as well as
		// where to put them. Choosing rows from one offset and placing them at another
		// would show the right rows in the wrong places, which is worse than either.
		float drawnOffset = offset;
		const float scrollSeconds = motionSeconds();
		if (scrollSeconds > 0.0f && !mDraggingThumb) {
			drawnOffset = gui.motion().value(gui.motionKey(mId.c_str(), kMotionScroll),
			                                 offset, scrollSeconds);
		}

		// Which rows the viewport is over. No search: every row is the same height, which
		// is exactly what buys this -- and what a variable-height list would have to
		// replace with a running total it maintains on every edit.
		const size_t first = static_cast<size_t>(drawnOffset / pitch);
		size_t wanted = static_cast<size_t>(std::ceil(inner.h / pitch)) + 1;
		if (wanted > mPool.size()) {
			if (!mWarnedPool) {
				mWarnedPool = true;
				RDA_LOG_WARNING("list " << mId << ": " << wanted << " rows fit but only "
				                << mPool.size() << " were built; raise poolSize");
			}
			wanted = mPool.size();
		}

		// Anything at all changed in the table: the rows on screen are re-read. There is
		// no per-row dirty set on purpose -- the visible set is tiny, so re-reading all of
		// it is cheaper than tracking which parts of it moved.
		const bool tableMoved = table.version() != mSeenVersion;
		mSeenVersion = table.version();

		gui.pushClipRect(inner);
		for (size_t slot = 0; slot < wanted; ++slot) {
			const size_t index = first + slot;
			if (index >= rows) break;
			PooledRow& row = mPool[slot];
			if (!row.root) continue;

			if (row.showing != index || tableMoved) rebind(row, index);

			const Rect placed{ inner.x, inner.y + static_cast<float>(index) * pitch - drawnOffset,
			                   inner.w, rowHeight };
			row.root->setArranged(placed);
			row.root->paint(gui, glm::vec2(placed.x, placed.y));
		}
		gui.popClipRect();

		// Asked for whether or not the table is long enough to scroll: a list filtered
		// down to what fits loses its bar by fading, and gets it back the same way.
		{
			const TextFieldStyle& style = gui.theme().textField(variant);
			const float frac = (maxOffset > 0.0f) ? (drawnOffset / maxOffset) : 0.0f;
			const Rect real{ track.x + 2.0f, track.y + frac * range, barWidth - 4.0f, thumbHeight };
			const bool hot = mDraggingThumb || (maxOffset > 0.0f && real.contains(in.pointer));
			const Gui::ScrollBarLook bar = gui.scrollBar(
				gui.motionKey(mId.c_str(), kMotionKnob), maxOffset > 0.0f, hot,
				thumbHeight, track.h, style);
			if (bar.presence > 0.004f) {
				gui.pushOpacity(bar.presence);
				gui.drawRect(track, style.scrollTrack);
				const float span = (std::max)(0.0f, track.h - bar.thumb);
				gui.drawRect(Rect{ track.x + 2.0f, track.y + frac * span,
				                   barWidth - 4.0f, bar.thumb }, bar.colour);
				gui.popOpacity();
			}
		}
	}
}
