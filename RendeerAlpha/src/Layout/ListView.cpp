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

	float ListView::heightOf(const Table& table, size_t index) const {
		if (mHeightColumn == kNoColumn) return rowHeight;
		if (index >= table.rows() || mHeightColumn >= table.columns()) return rowHeight;
		const float own = static_cast<float>(table.number(index, mHeightColumn));
		// Absent, zero or nonsense means "the ordinary height", not "no row". A column
		// the sender has not filled in yet is the normal state of a list being built.
		return own > 0.0f ? own : rowHeight;
	}

	float ListView::topOf(size_t index) const {
		if (mHeightColumn == kNoColumn) {
			return static_cast<float>(index) * ((std::max)(1.0f, rowHeight + spacing));
		}
		if (mRowTop.empty()) return 0.0f;
		return mRowTop[(std::min)(index, mRowTop.size() - 1)];
	}

	void ListView::rebuildTops(const Table& table) {
		const size_t rows = table.rows();
		// One pass, and only when something it was computed from moved. Everything that
		// reads a position reads this, so there is nothing else to invalidate.
		const bool stale = table.version() != mBuiltVersion || rows != mBuiltRows ||
		                   rowHeight != mBuiltRowHeight || spacing != mBuiltSpacing ||
		                   mRowTop.size() != rows + 1;
		if (!stale) return;
		mBuiltVersion = table.version();
		mBuiltRows = rows;
		mBuiltRowHeight = rowHeight;
		mBuiltSpacing = spacing;
		mRowTop.resize(rows + 1);
		float top = 0.0f;
		for (size_t i = 0; i < rows; ++i) {
			mRowTop[i] = top;
			top += heightOf(table, i) + spacing;
		}
		// The last entry is the content's height, spacing after the final row included --
		// which is what the uniform path's rows * pitch also comes to, so both kinds of
		// list scroll to the same place at the end.
		mRowTop[rows] = top;
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
			// Checked, the way BindingRuntime checks it. Discarding this is how a bound
			// property that no widget accepts became invisible: the row simply kept
			// whatever it was built with, and the reason took a read of the engine to
			// find. Said once per property rather than once per row, because a pool
			// rebinding while it scrolls would otherwise say it hundreds of times.
			if (!applyBoundValue(*binding.target, binding.property, result.value) &&
			    !binding.warned) {
				binding.warned = true;
				RDA_LOG_WARNING("list " << mId << ": nothing named '" << binding.property
				                << "' on this row's widget to drive");
			}
		}
		row.showing = index;
	}

	bool ListView::rowRefFor(size_t slot, RowRef& out) const {
		if (slot >= mPool.size()) return false;
		const size_t index = mPool[slot].showing;
		if (index == kNoRow) return false;
		if (mTable == kNoTable) return false;
		Table& table = tables().at(mTable);
		if (index >= table.rows()) return false;
		out.table = &table;
		out.index = index;
		return true;
	}

	glm::vec2 ListView::measureContent(Gui& gui, glm::vec2 available) const {
		(void)gui;
		// The running total when there is one -- measuring a variable list as though its
		// rows were all the default height would give a container the wrong size to put
		// it in, which is the one thing measuring is for.
		if (mHeightColumn != kNoColumn && !mRowTop.empty()) {
			return { available.x, mRowTop.back() };
		}
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

		// The height column, resolved once against the table this list turned out to
		// show. Said rather than ignored: a name that is not a column, or is a column of
		// the wrong type, is a layout that thinks its rows vary and gets a list that does
		// not -- which looks like the feature not working.
		if (!rowHeights.empty() && mHeightColumn == kNoColumn && !mWarnedHeightColumn) {
			const uint32_t found = table.column(rowHeights);
			if (found == kNoColumn) {
				mWarnedHeightColumn = true;
				RDA_LOG_WARNING("list " << mId << ": rowHeights names '" << rowHeights
				                << "', which is not a column of '" << of
				                << "'; every row is rowHeight tall");
			} else if (table.columnType(found) != ColumnType::Number) {
				mWarnedHeightColumn = true;
				RDA_LOG_WARNING("list " << mId << ": rowHeights names '" << rowHeights
				                << "', which is not a number column; every row is "
				                   "rowHeight tall");
			} else {
				mHeightColumn = found;
			}
		}
		const bool varies = mHeightColumn != kNoColumn;
		if (varies) rebuildTops(table);

		// The bar's width is always reserved, never depending on whether it shows: the
		// other way round, turning it on narrows the rows, which can turn it off again.
		const Rect inner{ view.x, view.y, (std::max)(0.0f, view.w - barWidth), view.h };
		const float content = varies ? mRowTop.back() : static_cast<float>(rows) * pitch;
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

		// Asked to show a particular row. Nudged rather than centred: a row already on
		// screen should not move, and one just off the edge should come just on --
		// which is what a reader following a search expects, and what centring is not.
		if (revealRow >= 0 && revealRow != mRevealed && rows > 0) {
			mRevealed = revealRow;
			const size_t want = std::min<size_t>(static_cast<size_t>(revealRow), rows - 1);
			const float top = topOf(want);
			const float bottom = top + heightOf(table, want);
			if (top < offset)                 offset = top;
			else if (bottom > offset + inner.h) offset = bottom - inner.h;
		} else if (revealRow < 0) {
			mRevealed = -1;   // asked for nothing, so the next request is heard
		}

		// Follow: the content grew and the reader was at the end of it, so move the end
		// back under them. Before the clamp, so a list whose content shrank is handled by
		// the clamp rather than by this.
		//
		// The wheel and the thumb are read above, so a deliberate scroll this frame has
		// already moved `offset` and mWasAtEnd (from last frame) decides whether it is
		// dragged back. That is what makes scrolling up stick: one notch leaves the end,
		// and nothing pulls it back until the reader returns.
		if (follow && maxOffset > mLastMaxOffset && mWasAtEnd && !mDraggingThumb) {
			offset = maxOffset;
		}

		offset = std::clamp(offset, 0.0f, maxOffset);
		// Half a row of slack: landing exactly on maxOffset after an eased scroll is not
		// something floating point promises, and a transcript that stopped following
		// because it was two pixels short would look broken rather than deliberate.
		mWasAtEnd = (offset >= maxOffset - pitch * 0.5f);
		mLastMaxOffset = maxOffset;

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

		// Which rows the viewport is over.
		//
		// A uniform list divides: every row is the same height, which is exactly what
		// buys it. A list with a height column searches its running totals instead --
		// which is the cost the column is charged for, and it is a binary search over an
		// array, not a walk.
		size_t first = 0;
		size_t wanted = 0;
		if (varies) {
			// upper_bound then step back: the row the offset is *inside*, including the
			// case where it lands exactly on a boundary.
			const auto at = std::upper_bound(mRowTop.begin(), mRowTop.end() - 1, drawnOffset);
			first = static_cast<size_t>(at - mRowTop.begin());
			if (first > 0) --first;
			const float bottom = drawnOffset + inner.h;
			for (size_t i = first; i < rows && mRowTop[i] < bottom; ++i) ++wanted;
			if (wanted == 0 && first < rows) wanted = 1;
		} else {
			first = static_cast<size_t>(drawnOffset / pitch);
			wanted = static_cast<size_t>(std::ceil(inner.h / pitch)) + 1;
		}
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

			if (row.showing != index || tableMoved || mRowsDirty) rebind(row, index);

			const Rect placed{ inner.x, inner.y + topOf(index) - drawnOffset,
			                   inner.w, heightOf(table, index) };
			row.root->setArranged(placed);
			row.root->paint(gui, glm::vec2(placed.x, placed.y));
		}
		// Every visible row has been re-bound by now, so the flag has done its job. Rows
		// scrolled to later are re-bound anyway, by the index check above.
		mRowsDirty = false;
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
