#include <Layout/GridView.h>
#include <Core/Tables.h>
#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Shortcut.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace RDA {

	namespace {
		constexpr size_t kNone = static_cast<size_t>(-1);
		// How far either side of a column's right edge a press takes hold of it to resize.
		constexpr float kEdgeGrip = 4.0f;
		// What a screen reader is told about without the rows being drawn: as many as a list
		// describes, for the same reason.
		constexpr size_t kMostDescribed = 2000;
	}

	// ---- reading the table ---------------------------------------------------------------

	std::string GridView::keyOf(const Table& table, size_t row) const {
		if (row >= table.rows()) return {};
		if (mKeyCol == kNoColumn) return std::to_string(row);  // the position, failing a name
		return tableCellText(table, row, mKeyCol);
	}

	void GridView::rebuildOrder(const Table& table) {
		TableViewSpec spec{ sortBy, sortDescending, filter, filterColumns };
		// Worked out again only when the table or what it is seen through moved: a grid that
		// is scrolled or hovered reads the order it already has.
		if (mBuilt && spec == mBuiltSpec && table.version() == mBuiltVersion) return;
		mOrder = tableView(table, spec);
		mBuiltSpec = std::move(spec);
		mBuiltVersion = table.version();
		mBuilt = true;
		++mOrderEpoch;
	}

	void GridView::choose(const Table& table, size_t row) {
		const std::string key = keyOf(table, row);
		if (key == selected) return;
		// Written here as well as reported, the way a tree does: the highlight moves on the
		// frame it was chosen, and a layout that binds nothing still shows what was clicked.
		selected = key;
		if (onSelect) onSelect(key);
	}

	void GridView::pick(const Table& table, Pick how, size_t shown) {
		if (!multiSelect || shown >= mOrder.size()) return;
		const bool changed = mPicked.apply(how, shown, mOrder.size(),
		                                   [&](size_t i) { return keyOf(table, mOrder[i]); });
		if (changed) reportPicked();
	}

	void GridView::reportPicked() {
		if (onSelectionChange) onSelectionChange(mPicked.joined());
	}

	std::vector<GridView::Placed> GridView::placeColumns(const Table& table, float available) {
		std::vector<Placed> out;
		float fixed = 0.0f, weights = 0.0f;
		for (const auto& child : children()) {
			auto* column = dynamic_cast<GridColumn*>(child.get());
			if (!column || !column->visible) continue;
			const uint32_t field = table.column(column->field);
			const auto resized = mResized.find(column->field);
			float w = 0.0f;
			// A width the reader dragged wins over the layout's, fill or not: having been
			// given a size by hand, a column keeps it.
			if (resized != mResized.end()) w = resized->second;
			else if (!column->fills()) w = column->width.value > 0.0f ? column->width.value : GridColumn::kDefaultWidth;
			if (resized == mResized.end() && column->fills()) weights += (std::max)(0.0f, column->width.value);
			else fixed += (std::max)(w, column->least());
			out.push_back(Placed{ column, field, 0.0f, w });
		}
		// What is left after the fixed columns, shared by weight. Never below a column's
		// minimum: a grid narrower than its columns scrolls sideways rather than squeezing
		// a column to nothing.
		const float left = (std::max)(0.0f, available - fixed);
		float x = 0.0f;
		for (Placed& p : out) {
			const bool share = p.column->fills() && mResized.find(p.column->field) == mResized.end();
			if (share) p.w = weights > 0.0f ? left * (std::max)(0.0f, p.column->width.value) / weights : 0.0f;
			p.w = (std::max)(p.w, p.column->least());
			p.x = x;
			x += p.w;
		}
		return out;
	}

	std::string GridView::rowName(const Table& table, const std::vector<Placed>& columns, size_t row) {
		if (table.version() != mNamesVersion || mNames.size() != table.rows()) {
			mNames.assign(table.rows(), std::string());
			mNamesVersion = table.version();
		}
		if (row >= mNames.size()) return {};
		std::string& name = mNames[row];
		if (!name.empty()) return name;
		// Every column, titled, in the order they are shown: "Name: notes.txt, Size: 120".
		// A screen reader reads a row as one thing, so this is the row read aloud. An empty
		// cell is left out rather than read as "Size: ", which says nothing twice.
		for (const Placed& p : columns) {
			const std::string cell = tableCellText(table, row, p.field, p.column->decimals);
			if (cell.empty()) continue;
			if (!name.empty()) name += ", ";
			const std::string& title = p.column->title.empty() ? p.column->field : p.column->title;
			// A bool's tick is a picture; said aloud it is the column's title.
			if (p.field != kNoColumn && table.columnType(p.field) == ColumnType::Bool) name += title;
			else name += title + ": " + cell;
		}
		if (name.empty()) name = keyOf(table, row);
		return name;
	}

	// ---- size ----------------------------------------------------------------------------

	glm::vec2 GridView::measureContent(Gui& gui, glm::vec2 available) const {
		const float header = headerHeight > 0.0f ? headerHeight : gui.lineHeight() + 10.0f;
		return { available.x, header + static_cast<float>(mOrder.size()) * (std::max)(1.0f, rowHeight) };
	}

	// ---- drawing and input ---------------------------------------------------------------

	void GridView::paint(Gui& gui, glm::vec2 origin) {
		const Rect view = placement(gui, origin);

		if (mTable == kNoTable) {
			mTable = tables().find(of);
			if (mTable == kNoTable) {
				if (!mWarned) {
					mWarned = true;
					RDA_LOG_WARNING("grid " << mId << ": there is no table named '" << of << "'");
				}
				return;
			}
		}
		Table& table = tables().at(mTable);
		mKeyCol = table.column(keyColumn);
		rebuildOrder(table);

		const TextFieldStyle& style = gui.theme().textField(variant);
		const GuiInput& in = gui.input();
		const uint32_t wid = gui.widgetId(mId.c_str());
		const float pitch = (std::max)(1.0f, rowHeight);
		const float header = headerHeight > 0.0f ? headerHeight : gui.lineHeight() + 10.0f;
		const float pad = 6.0f;

		// The vertical bar's room is always kept, as a list keeps it. The horizontal one is
		// kept only when the columns are wider than the grid -- which is decided by the
		// columns, not by the rows, so showing it cannot change whether it is needed.
		const float bodyW = (std::max)(0.0f, view.w - barWidth);
		std::vector<Placed> columns = placeColumns(table, bodyW);
		const float contentW = columns.empty() ? 0.0f : columns.back().x + columns.back().w;
		const bool sideways = contentW > bodyW + 0.5f;
		const float bodyH = (std::max)(0.0f, view.h - header - (sideways ? barWidth : 0.0f));
		const Rect headRect{ view.x, view.y, bodyW, header };
		const Rect body{ view.x, view.y + header, bodyW, bodyH };

		const size_t rows = mOrder.size();
		const float content = static_cast<float>(rows) * pitch;
		const float maxY = content > body.h ? content - body.h : 0.0f;
		const float maxX = sideways ? contentW - bodyW : 0.0f;

		// In the Tab order, and a press anywhere on it points the keyboard here.
		gui.focusable(wid, view);
		if (in.lPressed && view.contains(in.pointer)) gui.setFocus(wid);
		const bool focused = gui.focused() == wid;
		if (focused) gui.focusPages(multiSelect);

		// ---- the wheel: down the rows, or across them with Shift ----
		if (in.scroll != 0.0f && !gui.scrollConsumed() && view.contains(in.pointer)) {
			if (in.shift && maxX > 0.0f) { mOffsetX -= in.scroll * wheelStep; gui.consumeScroll(); }
			else if (maxY > 0.0f)        { mOffsetY -= in.scroll * wheelStep; gui.consumeScroll(); }
		}

		// ---- the bars, taken hold of ----
		const Rect trackV{ view.x + view.w - barWidth, view.y + header, barWidth, bodyH };
		const Rect trackH{ view.x, view.y + header + bodyH, bodyW, barWidth };
		float thumbV = 0.0f, rangeV = 0.0f, thumbH = 0.0f, rangeH = 0.0f;
		if (maxY > 0.0f) {
			thumbV = (std::max)(28.0f, trackV.h * (body.h / (std::max)(content, 1.0f)));
			rangeV = (std::max)(0.0f, trackV.h - thumbV);
			if (!mDragV && in.lPressed && trackV.contains(in.pointer)) {
				const float at = trackV.y + (mOffsetY / maxY) * rangeV;
				mGrab = (in.pointer.y < at || in.pointer.y > at + thumbV) ? thumbV * 0.5f : in.pointer.y - at;
				mDragV = true;
			}
			if (mDragV && rangeV > 0.0f) mOffsetY = ((in.pointer.y - mGrab - trackV.y) / rangeV) * maxY;
		}
		if (maxX > 0.0f) {
			thumbH = (std::max)(28.0f, trackH.w * (bodyW / (std::max)(contentW, 1.0f)));
			rangeH = (std::max)(0.0f, trackH.w - thumbH);
			if (!mDragH && in.lPressed && trackH.contains(in.pointer)) {
				const float at = trackH.x + (mOffsetX / maxX) * rangeH;
				mGrab = (in.pointer.x < at || in.pointer.x > at + thumbH) ? thumbH * 0.5f : in.pointer.x - at;
				mDragH = true;
			}
			if (mDragH && rangeH > 0.0f) mOffsetX = ((in.pointer.x - mGrab - trackH.x) / rangeH) * maxX;
		}

		// ---- the keyboard ----
		if (mAtEpoch != mOrderEpoch || mAtKey != selected) {
			mAt = kNone;
			if (!selected.empty()) {
				for (size_t i = 0; i < rows; ++i) {
					if (keyOf(table, mOrder[i]) == selected) { mAt = i; break; }
				}
			}
			mAtEpoch = mOrderEpoch;
			mAtKey = selected;
		}
		size_t at = mAt;  // where the chosen row is shown, if it is
		if (focused && rows > 0) {
			const size_t page = (std::max)(size_t{ 1 }, static_cast<size_t>(body.h / pitch) - 1);
			size_t to = at;
			Pick how = Pick::Only;
			for (const Chord& chord : in.chords) {
				const bool ctrl = (chord.mods & Chord::Ctrl) != 0;
				const bool shift = (chord.mods & Chord::Shift) != 0;
				// Shift and Ctrl mean something only where several rows can be chosen, and
				// nothing else held does here at all.
				if (chord.mods != 0 && (!multiSelect || (chord.mods & ~(Chord::Ctrl | Chord::Shift)) != 0)) continue;
				if (multiSelect && chord.mods == Chord::Ctrl && chord.key == Key::A) {
					if (mPicked.all(rows, [&](size_t i) { return keyOf(table, mOrder[i]); })) reportPicked();
					continue;
				}
				if (multiSelect && chord.mods == Chord::Ctrl && chord.key == Key::Space) {
					if (to != kNone) pick(table, Pick::Toggle, to);
					continue;
				}
				const size_t from = (to == kNone) ? 0 : to;
				how = pickForKey(multiSelect, ctrl, shift);
				switch (chord.key) {
				case Key::Down:     to = (to == kNone) ? 0 : (std::min)(from + 1, rows - 1); break;
				case Key::Up:       to = (to == kNone) ? 0 : (from > 0 ? from - 1 : 0); break;
				case Key::PageDown: to = (std::min)(from + page, rows - 1); break;
				case Key::PageUp:   to = from > page ? from - page : 0; break;
				case Key::Home:     to = 0; break;
				case Key::End:      to = rows - 1; break;
				case Key::Enter:
					if (to != kNone && onActivate) onActivate(keyOf(table, mOrder[to]));
					break;
				default: break;
				}
			}
			if (to != at && to != kNone) {
				choose(table, mOrder[to]);
				pick(table, how, to);
				at = to;
				mRevealShown = to;
			}
		}
		if (mRevealShown != kNone && mRevealShown < rows) {
			// Nudged, not centred: a row already in view stays where it is.
			const float top = static_cast<float>(mRevealShown) * pitch;
			if (top < mOffsetY)                      mOffsetY = top;
			else if (top + pitch > mOffsetY + body.h) mOffsetY = top + pitch - body.h;
		}
		mRevealShown = kNone;

		mOffsetY = std::clamp(mOffsetY, 0.0f, maxY);
		mOffsetX = std::clamp(mOffsetX, 0.0f, maxX);
		float drawnY = mOffsetY;
		const float scrollSeconds = motionSeconds();
		if (scrollSeconds > 0.0f && !mDragV) {
			drawnY = gui.motion().value(gui.motionKey(mId.c_str(), kMotionScroll), mOffsetY, scrollSeconds);
		}
		const float left = view.x - mOffsetX;   // where the first column starts on screen

		// ---- the header: titles, sorting, and edges to drag ----
		//
		// Resizing first, because a press near an edge is a resize and not a sort, and
		// which one it is has to be settled before a title hears about it.
		const bool overHead = headRect.contains(in.pointer);
		if (mResizing >= 0 && mResizing < static_cast<int>(columns.size())) {
			const Placed& p = columns[static_cast<size_t>(mResizing)];
			const float w = (std::max)(p.column->least(), in.pointer.x - mResizeFrom);
			if (w != p.w) {
				mResized[p.column->field] = w;
				gui.keepAwake();
			}
		}
		if (in.lReleased) mResizing = -1;
		int edgeUnder = -1;
		if (overHead && mResizing < 0) {
			for (size_t i = 0; i < columns.size(); ++i) {
				const float edge = left + columns[i].x + columns[i].w;
				if (std::fabs(in.pointer.x - edge) <= kEdgeGrip) edgeUnder = static_cast<int>(i);
			}
		}
		if (in.lPressed && overHead) {
			if (edgeUnder >= 0) {
				mResizing = edgeUnder;
				const Placed& p = columns[static_cast<size_t>(edgeUnder)];
				mResizeFrom = in.pointer.x - p.w;
				if (in.clickCount == 2) {
					// A double click on an edge gives the column back the width it was written
					// with, the way a spreadsheet's does -- a dragged width is otherwise forever.
					mResized.erase(p.column->field);
					mResizing = -1;
				}
			} else {
				mPressedHeader = -1;
				for (size_t i = 0; i < columns.size(); ++i) {
					const float x = left + columns[i].x;
					if (in.pointer.x >= x && in.pointer.x < x + columns[i].w) mPressedHeader = static_cast<int>(i);
				}
			}
		}

		// The strip runs over the bar's column too, so the titles sit on one band end to end.
		gui.drawRect(Rect{ view.x, view.y, view.w, header }, style.gutter);
		gui.pushClipRect(headRect);
		const TextStyle bold{ 0.0f, true };
		for (size_t i = 0; i < columns.size(); ++i) {
			const Placed& p = columns[i];
			const Rect cell{ left + p.x, headRect.y, p.w, headRect.h };
			if (cell.x > headRect.x + headRect.w || cell.x + cell.w < headRect.x) continue;
			const bool sorted = !sortBy.empty() && p.column->field == sortBy;
			const bool hot = (mPressedHeader == static_cast<int>(i) && in.lDown) ||
			                 (overHead && edgeUnder < 0 && mResizing < 0 && p.column->sortable &&
			                  in.pointer.x >= cell.x && in.pointer.x < cell.x + cell.w);
			if (hot) gui.drawRect(cell, style.currentLine);
			const std::string& title = p.column->title.empty() ? p.column->field : p.column->title;
			// The arrow's room comes off the title's, on the side away from where it is
			// aligned, so a right-aligned number column's title stays over its numbers.
			const float arrowRoom = sorted ? 14.0f : 0.0f;
			const float textW = gui.measureText(title.c_str(), bold);
			const float room = (std::max)(0.0f, cell.w - 2.0f * pad - arrowRoom);
			const float tx = cell.x + pad + (p.column->hAlign == TextAlign::End ? arrowRoom : 0.0f) +
			                 alignOffset(p.column->hAlign, room, textW);
			const float ty = cell.y + (cell.h - gui.lineHeight(bold)) * 0.5f;
			gui.pushClipRect(Rect{ cell.x + 1.0f, cell.y, (std::max)(0.0f, cell.w - 2.0f), cell.h });
			gui.drawText(title.c_str(), { tx, ty }, style.text, bold);
			if (sorted) {
				// Two strokes: up for ascending -- smallest at the top -- and down for descending.
				const float ax = p.column->hAlign == TextAlign::End ? cell.x + pad + 4.0f : cell.x + cell.w - pad - 4.0f;
				const float ay = cell.y + cell.h * 0.5f;
				const float s = 3.5f, d = sortDescending ? 1.0f : -1.0f;
				gui.drawLine({ ax - s, ay - d * s * 0.5f }, { ax, ay + d * s * 0.5f }, style.lineNumber, 1.5f);
				gui.drawLine({ ax, ay + d * s * 0.5f }, { ax + s, ay - d * s * 0.5f }, style.lineNumber, 1.5f);
			}
			gui.popClipRect();
			// The edge: a line, brighter where it can be taken hold of.
			const bool grip = edgeUnder == static_cast<int>(i) || mResizing == static_cast<int>(i);
			gui.drawRect(Rect{ cell.x + cell.w - 1.0f, cell.y + 4.0f, 1.0f, cell.h - 8.0f },
			             grip ? style.caret : style.border);
		}
		gui.popClipRect();

		// A title let go of where it was pressed sorts by it: the same column again turns the
		// order round, a different one starts ascending. Written here and reported, so the
		// grid sorts whether or not anything binds `sortBy`.
		if (in.lReleased && mPressedHeader >= 0) {
			const int pressed = mPressedHeader;
			mPressedHeader = -1;
			if (pressed < static_cast<int>(columns.size()) && overHead) {
				const Placed& p = columns[static_cast<size_t>(pressed)];
				const float x = left + p.x;
				if (p.column->sortable && in.pointer.x >= x && in.pointer.x < x + p.w) {
					if (sortBy == p.column->field) sortDescending = !sortDescending;
					else { sortBy = p.column->field; sortDescending = false; }
					if (onSort) onSort((sortDescending ? "-" : "") + sortBy);
					gui.keepAwake();
				}
			}
		}

		// ---- the rows ----
		const size_t first = static_cast<size_t>(drawnY / pitch);
		const size_t count = static_cast<size_t>(std::ceil(body.h / pitch)) + 1;
		const bool describing = gui.accessibilityWanted() && !gui.inert();
		const uint32_t gridId = gui.widgetId(mId.c_str());
		auto describeRow = [&](size_t shown, const Rect& where, bool offscreen) {
			const size_t row = mOrder[shown];
			AccessibleInfo item;
			item.role = AccessibleRole::ListItem;
			item.name = rowName(table, columns, row);
			item.offscreen = offscreen;
			const std::string key = keyOf(table, row);
			item.selected = (multiSelect ? mPicked.contains(key) : (!selected.empty() && key == selected)) ? 1 : 0;
			item.actions = kCanSelect | kCanInvoke;
			// Known by its key, so a row is the same item however the grid is sorted.
			uint32_t id = 2166136261u;
			for (const char c : key) { id ^= static_cast<uint8_t>(c); id *= 16777619u; }
			id ^= gridId;
			// The chosen row has the keyboard, as far as a screen reader is concerned: the arrows
			// move it, and it is what should be read as they do.
			// With several chosen, that is the row the keyboard is on, chosen or not.
			const bool current = multiSelect ? (!selected.empty() && key == selected) : item.selected == 1;
			gui.noteAccessibleItem(this, id, static_cast<int>(row), nullptr, where, item, mId + "#" + key,
			                       focused && current);
		};
		size_t describeFrom = 0, describeTo = 0;
		if (describing && rows > 0) {
			const size_t span = (std::min)(rows, kMostDescribed);
			const size_t lead = span > count ? (span - count) / 2 : 0;
			describeFrom = first > lead ? first - lead : 0;
			if (describeFrom + span > rows) describeFrom = rows - span;
			describeTo = describeFrom + span;
		}
		auto rowRect = [&](size_t shown) {
			return Rect{ view.x, body.y + static_cast<float>(shown) * pitch - drawnY, bodyW, pitch };
		};
		if (describing) {
			for (size_t i = describeFrom; i < (std::min)(first, describeTo); ++i) describeRow(i, rowRect(i), true);
		}

		gui.pushClipRect(body);
		const bool overBody = body.contains(in.pointer) && !mDragV && !mDragH && mResizing < 0;
		size_t drawnTo = first;
		for (size_t i = first; i < rows && i < first + count; ++i) {
			const size_t row = mOrder[i];
			const Rect line = rowRect(i);
			const bool over = overBody && line.contains(in.pointer);
			if (over && in.lPressed) {
				choose(table, row);
				pick(table, pickForClick(true, in.ctrl, in.shift), i);
				at = i;
				if (in.clickCount == 2 && onActivate) onActivate(keyOf(table, row));
			}
			const bool chosen = multiSelect ? mPicked.contains(keyOf(table, row)) : (at == i);
			if (chosen)    gui.drawRect(line, style.selection);
			else if (over) gui.drawRect(line, style.currentLine);
			// Where the keyboard is, among several chosen: an outline, so it can be seen
			// whether or not the row it is on is one of them.
			if (multiSelect && focused && at == i) {
				gui.drawRect(Rect{ line.x, line.y, line.w, 1.0f }, style.caret);
				gui.drawRect(Rect{ line.x, line.y + line.h - 1.0f, line.w, 1.0f }, style.caret);
				gui.drawRect(Rect{ line.x, line.y, 1.0f, line.h }, style.caret);
				gui.drawRect(Rect{ line.x + line.w - 1.0f, line.y, 1.0f, line.h }, style.caret);
			}
			for (const Placed& p : columns) {
				const Rect cell{ left + p.x, line.y, p.w, line.h };
				if (cell.x > body.x + body.w || cell.x + cell.w < body.x) continue;
				const std::string text = tableCellText(table, row, p.field, p.column->decimals);
				if (text.empty()) continue;
				const float w = gui.measureText(text.c_str());
				const float room = (std::max)(0.0f, cell.w - 2.0f * pad);
				const float tx = cell.x + pad + alignOffset(p.column->hAlign, room, w);
				const float ty = cell.y + (cell.h - gui.lineHeight()) * 0.5f;
				gui.pushClipRect(Rect{ cell.x + pad * 0.5f, cell.y, (std::max)(0.0f, cell.w - pad), cell.h });
				gui.drawText(text.c_str(), { tx, ty }, style.text);
				gui.popClipRect();
			}
			if (describing) describeRow(i, line, false);
			drawnTo = i + 1;
		}
		gui.popClipRect();
		if (describing) {
			for (size_t i = (std::max)(drawnTo, describeFrom); i < describeTo; ++i) describeRow(i, rowRect(i), true);
		}

		if (in.lReleased) { mDragV = false; mDragH = false; }

		// ---- the bars, drawn ----
		{
			const float frac = maxY > 0.0f ? drawnY / maxY : 0.0f;
			const Rect real{ trackV.x + 2.0f, trackV.y + frac * rangeV, barWidth - 4.0f, thumbV };
			const bool hot = mDragV || (maxY > 0.0f && real.contains(in.pointer));
			const Gui::ScrollBarLook bar = gui.scrollBar(gui.motionKey(mId.c_str(), kMotionKnob),
			                                             maxY > 0.0f, hot, thumbV, trackV.h, style);
			if (bar.presence > 0.004f) {
				gui.pushOpacity(bar.presence);
				gui.drawRect(trackV, style.scrollTrack);
				const float span = (std::max)(0.0f, trackV.h - bar.thumb);
				gui.drawRect(Rect{ trackV.x + 2.0f, trackV.y + frac * span, barWidth - 4.0f, bar.thumb }, bar.colour);
				gui.popOpacity();
			}
		}
		if (sideways) {
			const float frac = maxX > 0.0f ? mOffsetX / maxX : 0.0f;
			const Rect real{ trackH.x + frac * rangeH, trackH.y + 2.0f, thumbH, barWidth - 4.0f };
			const bool hot = mDragH || real.contains(in.pointer);
			gui.drawRect(trackH, style.scrollTrack);
			gui.drawRect(real, hot ? style.scrollThumbHover : style.scrollThumb);
		}
	}

	// ---- what a screen reader is told ----------------------------------------------------

	bool GridView::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::List;
		out.name = alt;
		out.focusable = true;
		out.multiSelectable = multiSelect;
		return true;
	}

	void GridView::accessibleAction(AccessibleAction action, const std::string& value) {
		(void)value;
		if (!multiSelect || mTable == kNoTable) return;
		Table& table = tables().at(mTable);
		bool changed = false;
		if (action == AccessibleAction::SelectAll)
			changed = mPicked.all(mOrder.size(), [&](size_t i) { return keyOf(table, mOrder[i]); });
		else if (action == AccessibleAction::ClearSelection)
			changed = mPicked.clear();
		if (changed) reportPicked();
	}

	void GridView::accessibleItemAction(int item, AccessibleAction action, const std::string& value) {
		(void)value;
		if (item < 0 || mTable == kNoTable) return;
		Table& table = tables().at(mTable);
		const size_t row = static_cast<size_t>(item);
		if (row >= table.rows()) return;
		const auto it = std::find(mOrder.begin(), mOrder.end(), row);
		const size_t shown = it == mOrder.end() ? kNone : static_cast<size_t>(it - mOrder.begin());
		switch (action) {
		case AccessibleAction::Select:
			choose(table, row);
			if (multiSelect && mPicked.assign(keyOf(table, row))) reportPicked();
			mRevealShown = shown;
			break;
		case AccessibleAction::AddToSelection:
			if (!multiSelect) { choose(table, row); break; }
			if (mPicked.add(keyOf(table, row))) reportPicked();
			break;
		case AccessibleAction::RemoveFromSelection:
			if (multiSelect && mPicked.remove(keyOf(table, row))) reportPicked();
			break;
		case AccessibleAction::Invoke:
			choose(table, row);
			if (onActivate) onActivate(keyOf(table, row));
			break;
		case AccessibleAction::ScrollIntoView:
			mRevealShown = shown;
			break;
		default:
			break;
		}
	}
}
