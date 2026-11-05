#include <Layout/TreeView.h>
#include <Core/Tables.h>
#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Shortcut.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace RDA {

	// ---- reading the table ---------------------------------------------------------------

	void TreeView::resolveColumns(const Table& table) {
		// Looked up again whenever the table changes, because a table's columns are
		// declared once but a tree may be pointed at a different one by a hot reload.
		if (&table == mTableSeen && table.version() == mColumnsVersion) return;
		mTableSeen = &table;
		mColumnsVersion = table.version();
		mDepthCol = table.column(depthColumn);
		mKeyCol = table.column(keyColumn);
		mKidsCol = table.column(childrenColumn);
		if (mKidsCol != kNoColumn && table.columnType(mKidsCol) != ColumnType::Bool) mKidsCol = kNoColumn;
		// Without depth there is no tree, only a list that draws arrows. Said once,
		// because it is the one mistake that makes everything else look broken.
		if ((mDepthCol == kNoColumn || table.columnType(mDepthCol) != ColumnType::Number)
		    && !mWarnedDepth) {
			mWarnedDepth = true;
			RDA_LOG_WARNING("tree " << mId << ": '" << of << "' has no number column named '"
			                << depthColumn << "', so every row is at the top level");
		}
	}

	int TreeView::depthOf(size_t row) const {
		if (!mTableSeen || mDepthCol == kNoColumn || row >= mTableSeen->rows()) return 0;
		if (mTableSeen->columnType(mDepthCol) != ColumnType::Number) return 0;
		const double depth = mTableSeen->number(row, mDepthCol);
		return depth > 0.0 ? static_cast<int>(depth) : 0;
	}

	bool TreeView::hasChildren(size_t row) const {
		if (!mTableSeen || row >= mTableSeen->rows()) return false;
		if (row + 1 < mTableSeen->rows() && depthOf(row + 1) > depthOf(row)) return true;
		// Nothing under it yet -- but the backend may know there will be, the moment
		// somebody opens it.
		return mKidsCol != kNoColumn && mTableSeen->boolean(row, mKidsCol);
	}

	std::string TreeView::keyOf(size_t row) const {
		if (!mTableSeen || row >= mTableSeen->rows()) return {};
		if (mKeyCol == kNoColumn) return std::to_string(row);  // the position, failing a name
		switch (mTableSeen->columnType(mKeyCol)) {
		case ColumnType::Text:
			return std::string(mTableSeen->text(row, mKeyCol));
		case ColumnType::Bool:
			return mTableSeen->boolean(row, mKeyCol) ? "true" : "false";
		case ColumnType::Number: {
			// A whole number reads as one -- "17", not "17.000000" -- because that is what a
			// handler comparing it against an id it wrote will expect.
			const double n = mTableSeen->number(row, mKeyCol);
			if (std::floor(n) == n && std::fabs(n) < 1.0e15) return std::to_string(static_cast<long long>(n));
			char text[32];
			std::snprintf(text, sizeof(text), "%g", n);
			return text;
		}
		}
		return {};
	}

	bool TreeView::isOpen(size_t row) const {
		const std::string key = keyOf(row);
		if (mOpened.count(key)) return true;
		if (mShut.count(key)) return false;
		// `openDepth` opens what is already there. A node whose children have not been
		// loaded starts shut, so that opening it is something the backend hears about --
		// otherwise it would sit open over nothing, and nobody would ever ask for what goes
		// under it.
		const bool loaded = mTableSeen && row + 1 < mTableSeen->rows() && depthOf(row + 1) > depthOf(row);
		return loaded && depthOf(row) < openDepth;
	}

	// ---- what is shown ---------------------------------------------------------------

	void TreeView::rebuildShown(const Table& table) {
		// One pass over the table: a shut node hides everything after it that is deeper,
		// up to the first row that is not. Run when the table or what is open changes,
		// never per frame -- the shown list is what every other question here reads.
		const size_t rows = table.rows();
		mShown.clear();
		mShownAt.assign(rows, kNoRow);
		int hiddenBelow = -1;   // depth of the shut node being skipped under, or -1
		for (size_t row = 0; row < rows; ++row) {
			const int depth = depthOf(row);
			if (hiddenBelow >= 0) {
				if (depth > hiddenBelow) continue;
				hiddenBelow = -1;
			}
			mShownAt[row] = mShown.size();
			mShown.push_back(row);
			if (hasChildren(row) && !isOpen(row)) hiddenBelow = depth;
		}
		mShownVersion = table.version();
		mShownStale = false;
	}

	size_t TreeView::shownRows(const Table& table) const {
		(void)table;
		return mShown.size();
	}

	size_t TreeView::tableRow(size_t shown) const {
		return shown < mShown.size() ? mShown[shown] : kNoRow;
	}

	size_t TreeView::shownIndexOf(size_t row) const {
		return row < mShownAt.size() ? mShownAt[row] : kNoRow;
	}

	size_t TreeView::parentOf(size_t shown) const {
		if (shown >= mShown.size()) return kNoRow;
		const int depth = depthOf(mShown[shown]);
		for (size_t at = shown; at-- > 0;) {
			if (depthOf(mShown[at]) < depth) return at;
		}
		return kNoRow;
	}

	// ---- what the reader does --------------------------------------------------------

	void TreeView::toggle(size_t row) {
		if (!hasChildren(row)) return;
		const std::string key = keyOf(row);
		const bool open = isOpen(row);
		// Recorded against the start rather than as a plain set of open nodes, so a node
		// that `openDepth` opened and the reader shut stays shut -- and one the reader
		// opened and shut again is back to whatever the start said.
		if (open) { mOpened.erase(key); if (depthOf(row) < openDepth) mShut.insert(key); }
		else      { mShut.erase(key);   if (depthOf(row) >= openDepth) mOpened.insert(key); }
		mShownStale = true;
		if (open) { if (onCollapse) onCollapse(key); }
		else      { if (onExpand) onExpand(key); }
	}

	void TreeView::choose(size_t row) {
		const std::string key = keyOf(row);
		if (key == selected) return;
		// Written here as well as reported, so the highlight moves on the frame it was
		// chosen; the layout's binding writes the same value back a frame later, and a
		// layout that does not bind it still gets a tree that shows what was clicked.
		selected = key;
		if (onSelect) onSelect(key);
	}

	// ---- several chosen ------------------------------------------------------------------

	bool TreeView::chosen(size_t row) const {
		const std::string key = keyOf(row);
		if (multiSelect) return mPicked.contains(key);
		return !selected.empty() && key == selected;
	}

	void TreeView::pick(Pick how, size_t shown) {
		if (!multiSelect || shown >= mShown.size()) return;
		// Only the nodes shown: a reach across a shut folder takes the folder, not what is
		// inside it, which is what the reader can see they are sweeping over.
		if (mPicked.apply(how, shown, mShown.size(), [&](size_t i) { return keyOf(mShown[i]); }))
			reportPicked();
	}

	void TreeView::reportPicked() {
		if (onSelectionChange) onSelectionChange(mPicked.joined());
	}

	void TreeView::keyboard(Gui& gui) {
		const uint32_t wid = gui.widgetId(mId.c_str());
		if (gui.focused() != wid) return;
		gui.focusPages(multiSelect);
		if (mShown.empty()) return;
		const GuiInput& in = gui.input();
		if (multiSelect) {
			for (const Chord& chord : in.chords) {
				if (chord.mods == Chord::Ctrl && chord.key == Key::A &&
				    mPicked.all(mShown.size(), [&](size_t i) { return keyOf(mShown[i]); }))
					reportPicked();
			}
		}
		if (in.editKeys.empty()) return;
		// With several chosen, Shift reaches and Ctrl moves without choosing.
		const Pick how = pickForKey(multiSelect, in.ctrl, in.shift);

		size_t at = kNoRow;
		for (size_t i = 0; i < mShown.size(); ++i) {
			if (keyOf(mShown[i]) == selected) { at = i; break; }
		}
		size_t to = at;
		for (const GuiEditKey key : gui.input().editKeys) {
			const size_t from = (to == kNoRow) ? 0 : to;
			switch (key) {
			case GuiEditKey::Down:  to = (to == kNoRow) ? 0 : (std::min)(from + 1, mShown.size() - 1); break;
			case GuiEditKey::Up:    to = (to == kNoRow) ? 0 : (from > 0 ? from - 1 : 0); break;
			case GuiEditKey::Home:  to = 0; break;
			case GuiEditKey::End:   to = mShown.size() - 1; break;
			case GuiEditKey::Right:
				// Open it; if it is open already, step into it.
				if (to == kNoRow) { to = 0; break; }
				if (hasChildren(mShown[from]) && !isOpen(mShown[from])) mPendingToggle = mShown[from];
				else if (hasChildren(mShown[from]) && from + 1 < mShown.size()) to = from + 1;
				break;
			case GuiEditKey::Left:
				// Shut it; if it is shut already, go to what it hangs off.
				if (to == kNoRow) { to = 0; break; }
				if (hasChildren(mShown[from]) && isOpen(mShown[from])) mPendingToggle = mShown[from];
				else if (const size_t up = parentOf(from); up != kNoRow) to = up;
				break;
			case GuiEditKey::Space:
				// Ctrl+Space chooses or unchooses, where several can be; Space opens and shuts.
				if (multiSelect && in.ctrl) { if (to != kNoRow) pick(Pick::Toggle, to); }
				else if (to != kNoRow) mPendingToggle = mShown[to];
				break;
			case GuiEditKey::Enter:
				if (to != kNoRow && onActivate) onActivate(keyOf(mShown[to]));
				break;
			default:
				break;
			}
		}
		if (to != at && to != kNoRow) {
			choose(mShown[to]);
			pick(how, to);
			revealShown(to);
		}
		if (mPendingToggle != kNoRow) gui.keepAwake();
	}

	void TreeView::prepare(Gui& gui, const Rect& view, Table& table) {
		resolveColumns(table);

		// A toggle asked for last frame lands now, before anything is placed from the
		// shown list it changes.
		if (mPendingToggle != kNoRow) {
			if (mPendingToggle < table.rows()) toggle(mPendingToggle);
			mPendingToggle = kNoRow;
		}
		if (mShownStale || table.version() != mShownVersion || mShownAt.size() != table.rows()) {
			rebuildShown(table);
			// The rows on screen show different table rows now; the pool re-binds any
			// slot whose row changed, so nothing more is needed than saying so.
			invalidateRows();
		}

		// In the Tab order, and a press anywhere on it points the keyboard here -- before
		// the rows paint, so a button in a row that takes the press takes the focus too.
		const uint32_t wid = gui.widgetId(mId.c_str());
		gui.focusable(wid, view);
		if (gui.input().lPressed && view.contains(gui.input().pointer)) gui.setFocus(wid);
		keyboard(gui);
	}

	Rect TreeView::arrangeRow(Gui& gui, const Rect& placed, size_t row) {
		const TextFieldStyle& style = gui.theme().textField(variant);
		const GuiInput& in = gui.input();
		const int depth = depthOf(row);
		const float inset = static_cast<float>(depth) * indent;
		const Rect arrow{ placed.x + inset, placed.y, indent, placed.h };
		const bool over = placed.contains(in.pointer) && gui.currentClipRect().contains(in.pointer);

		// The whole row, under its contents: chosen, or under the pointer.
		// Taken before anything is drawn, so a press shows on the frame it lands.
		if (over && in.lPressed && !(arrow.contains(in.pointer) && hasChildren(row))) {
			choose(row);
			pick(pickForClick(true, in.ctrl, in.shift), shownIndexOf(row));
		}
		if (chosen(row)) gui.drawRect(placed, style.selection);
		else if (over)   gui.drawRect(placed, style.currentLine);
		// Where the keyboard is, among several chosen.
		if (multiSelect && gui.focused() == gui.widgetId(mId.c_str()) && !selected.empty() &&
		    keyOf(row) == selected) {
			const uint32_t ink = style.caret;
			gui.drawRect(Rect{ placed.x, placed.y, placed.w, 1.0f }, ink);
			gui.drawRect(Rect{ placed.x, placed.y + placed.h - 1.0f, placed.w, 1.0f }, ink);
			gui.drawRect(Rect{ placed.x, placed.y, 1.0f, placed.h }, ink);
			gui.drawRect(Rect{ placed.x + placed.w - 1.0f, placed.y, 1.0f, placed.h }, ink);
		}

		if (hasChildren(row)) {
			// Two strokes, pointing right when shut and down when open. Drawn rather than
			// typed: a "v" and a ">" sit on different parts of a line of text, so the arrow
			// jumped when it turned.
			const glm::vec2 c{ arrow.x + arrow.w * 0.5f, placed.y + placed.h * 0.5f };
			const float s = 3.5f;
			const uint32_t ink = style.lineNumber;
			if (isOpen(row)) {
				gui.drawLine({ c.x - s, c.y - s * 0.5f }, { c.x, c.y + s * 0.5f }, ink, 1.5f);
				gui.drawLine({ c.x, c.y + s * 0.5f }, { c.x + s, c.y - s * 0.5f }, ink, 1.5f);
			} else {
				gui.drawLine({ c.x - s * 0.5f, c.y - s }, { c.x + s * 0.5f, c.y }, ink, 1.5f);
				gui.drawLine({ c.x + s * 0.5f, c.y }, { c.x - s * 0.5f, c.y + s }, ink, 1.5f);
			}
		}

		if (over && in.lPressed) {
			if (arrow.contains(in.pointer) && hasChildren(row)) {
				mPendingToggle = row;
				gui.keepAwake();
			} else {
				// Chosen above, before the row was drawn.
				// A double click opens what it is on -- a folder, by convention -- and says
				// so, so a file can be opened by the same gesture.
				if (in.clickCount == 2) {
					if (hasChildren(row)) { mPendingToggle = row; gui.keepAwake(); }
					if (onActivate) onActivate(keyOf(row));
				}
			}
		}

		const float taken = inset + indent;
		return Rect{ placed.x + taken, placed.y, (std::max)(0.0f, placed.w - taken), placed.h };
	}

	// ---- what a screen reader is told ------------------------------------------------

	bool TreeView::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Tree;
		out.name = alt;
		out.focusable = true;
		out.multiSelectable = multiSelect;
		return true;
	}

	void TreeView::accessibleAction(AccessibleAction action, const std::string& value) {
		(void)value;
		if (!multiSelect) return;
		bool changed = false;
		if (action == AccessibleAction::SelectAll)
			changed = mPicked.all(mShown.size(), [&](size_t i) { return keyOf(mShown[i]); });
		else if (action == AccessibleAction::ClearSelection)
			changed = mPicked.clear();
		if (changed) reportPicked();
	}

	void TreeView::describeRow(Gui& gui, size_t row, Widget* root, const Rect& placed) {
		AccessibleInfo item;
		item.role = AccessibleRole::TreeItem;
		item.name = rowName(row, root);
		if (item.name.empty()) item.name = keyOf(row);
		item.offscreen = (root == nullptr);
		const std::string key = keyOf(row);
		item.selected = chosen(row) ? 1 : 0;
		if (hasChildren(row)) item.expanded = isOpen(row) ? 1 : 0;
		item.level = depthOf(row) + 1;
		item.actions = kCanSelect | kCanInvoke | (hasChildren(row) ? kCanExpand : 0u);
		// Known by its key rather than its position, so a node is the same item after the
		// nodes above it open and shut. FNV-1a, mixed into the tree's own id.
		uint32_t id = 2166136261u;
		for (const char c : key) { id ^= static_cast<uint8_t>(c); id *= 16777619u; }
		id ^= gui.widgetId(mId.c_str());
		gui.noteAccessibleItem(this, id, static_cast<int>(row), root, placed, item, mId + "#" + key);
	}

	void TreeView::accessibleItemAction(int item, AccessibleAction action, const std::string& value) {
		(void)value;
		if (item < 0) return;
		const size_t row = static_cast<size_t>(item);
		switch (action) {
		case AccessibleAction::Select:
			choose(row);
			if (multiSelect && mPicked.assign(keyOf(row))) reportPicked();
			break;
		case AccessibleAction::AddToSelection:
			if (!multiSelect) { choose(row); break; }
			if (mPicked.add(keyOf(row))) reportPicked();
			break;
		case AccessibleAction::RemoveFromSelection:
			if (multiSelect && mPicked.remove(keyOf(row))) reportPicked();
			break;
		case AccessibleAction::Invoke:
			choose(row);
			if (onActivate) onActivate(keyOf(row));
			break;
		case AccessibleAction::Expand:
			if (hasChildren(row) && !isOpen(row)) mPendingToggle = row;
			break;
		case AccessibleAction::Collapse:
			if (hasChildren(row) && isOpen(row)) mPendingToggle = row;
			break;
		default:
			ListView::accessibleItemAction(item, action, value);
			break;
		}
	}
}
