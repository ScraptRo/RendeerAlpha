#include <GraphicalObjects/Widgets/Controls/Select.h>
#include <GraphicalObjects/Gui.h>
#include <Core/Tables.h>
#include <algorithm>
#include <cmath>

namespace RDA {

	// ---- Select --------------------------------------------------------------------
	size_t Select::choiceCount() const {
		if (!of.empty()) {
			const uint32_t id = tables().find(of);
			return (id == kNoTable) ? 0 : tables().at(id).rows();
		}
		size_t count = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (one && one->visible) ++count;
		}
		return count;
	}

	bool Select::choiceAt(size_t index, std::string_view& text, std::string_view& value) const {
		if (!of.empty()) {
			const uint32_t id = tables().find(of);
			if (id == kNoTable) return false;
			Table& table = tables().at(id);
			if (index >= table.rows()) return false;
			const uint32_t textCol = table.column(textColumn);
			const uint32_t valueCol = table.column(valueColumn);
			// A column the table does not have reads as empty rather than refusing: a
			// select whose value column is missing still shows its text, which is enough
			// to see what went wrong.
			text  = (textCol  == kNoColumn) ? std::string_view{} : table.text(index, textCol);
			value = (valueCol == kNoColumn) ? std::string_view{} : table.text(index, valueCol);
			return true;
		}
		size_t at = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (!one || !one->visible) continue;
			if (at++ != index) continue;
			text = one->text;
			value = one->value;
			return true;
		}
		return false;
	}

	glm::vec2 Select::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		// Wide enough for the longest choice, so opening the list does not change what
		// the closed box looks like.
		float widest = gui.measureText(placeholder.c_str());
		const size_t choices = choiceCount();
		for (size_t i = 0; i < choices; ++i) {
			std::string_view text, ignored;
			if (!choiceAt(i, text, ignored)) continue;
			widest = (std::max)(widest, gui.measureText(std::string(text).c_str()));
		}
		return { widest + 34.0f, std::round(gui.lineHeight() + 10.0f) };
	}

	void Select::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		mClosedRect = box;

		const ButtonStyle& s = gui.theme().button(variant);
		const bool inside = box.contains(gui.input().pointer);
		if (inside && gui.input().lPressed) { mOpen = !mOpen; gui.setFocus(gui.widgetId(mId.c_str())); }

		// ---- the keyboard ------------------------------------------------------------
		//
		// Shut, it opens on Enter or Space. Open, the arrows move a highlight, Enter
		// takes what is highlighted and Escape leaves without changing anything -- which
		// is the one thing a dropdown must always allow and the reason it is listed
		// separately from choosing.
		const uint32_t wid = gui.widgetId(mId.c_str());
		gui.focusable(wid, box);
		// Through the accessors, not by counting <option> children: the choices may come
		// from a table now, and counting children gave one of `of=` and none of the other
		// -- a dropdown filled from data that the keyboard could open and not move in.
		const int options = static_cast<int>(choiceCount());
		if (gui.focusActivated(wid)) {
			if (!mOpen) {
				mOpen = true;
				// Opened from the keyboard, it starts on whatever is already chosen, so
				// the first arrow moves from there rather than from the top.
				mHighlight = 0;
				for (int at = 0; at < options; ++at) {
					std::string_view text, oneValue;
					if (!choiceAt(static_cast<size_t>(at), text, oneValue)) continue;
					if (oneValue == value) { mHighlight = at; break; }
				}
			} else {
				chooseHighlighted();
			}
		} else if (mOpen) {
			if (const int step = gui.focusStep(wid); step != 0 && options > 0) {
				if (mHighlight < 0) mHighlight = (step > 0) ? 0 : options - 1;
				else mHighlight = (mHighlight + step + options) % options;
			}
			if (gui.focusEscaped(wid)) { mOpen = false; mHighlight = -1; }
		}

		// Typing finds a choice by its first letters, open or shut: shut, it opens on the
		// match, so nothing is chosen until Enter takes it and Escape still changes nothing.
		if (gui.focused() == wid) {
			gui.claimTyping();
			mFindAge += gui.input().dt;
			if (!gui.input().typed.empty() && options > 0) findTyped(gui.input().typed);
		} else {
			mFind.clear();
		}

		// How far open it is, which is what the list is drawn from -- and is the reason
		// the list goes on being drawn after `mOpen` is false, since otherwise it would
		// have nowhere to close from. Asked for every frame, open or shut: a value nobody
		// asks for is dropped, and one that had been dropped would open at full height.
		mOpenAmount = mOpen ? 1.0f : 0.0f;
		if (s.motion.seconds > 0.0f) {
			mOpenAmount = gui.motion().value(gui.motionKey(mId.c_str(), kMotionOpen),
			                                 mOpenAmount, s.motion.seconds, s.motion.curve);
		}

		// The box's own fill follows what it is doing, like a button's -- which it did not
		// before, so a select was the one control on the screen that still snapped.
		gui.drawRectRounded(box, gui.motion().colour(
			gui.motionKey(mId.c_str(), kMotionFill),
			mOpen ? s.pressed : (inside ? s.hovered : s.normal),
			s.motion.seconds, s.motion.curve), s.radius);

		// Whatever is chosen, or the placeholder when the value matches no option -- which
		// is what an unset signal looks like, and reads better than an empty box.
		std::string shown = placeholder;
		const size_t choices = choiceCount();
		const int count = static_cast<int>(choices);
		for (size_t i = 0; i < choices; ++i) {
			std::string_view text, oneValue;
			if (!choiceAt(i, text, oneValue)) continue;
			if (oneValue == value) { shown.assign(text); break; }
		}
		const float textY = box.y + (box.h - gui.lineHeight(s.font)) * 0.5f;
		gui.drawText(shown.c_str(), { box.x + 10.0f, textY }, s.text, s.font);

		// The chevron. There is no transform to turn one over with -- every quad this GUI
		// draws is axis-aligned -- so the two glyphs cross-fade in place instead, which
		// over a tenth of a second reads as the one becoming the other.
		const glm::vec2 chevron{ box.x + box.w - 18.0f, textY };
		if (mOpenAmount < 0.999f) {
			gui.drawText("v", chevron, fadeTo(s.text, 1.0f - mOpenAmount), s.font);
		}
		if (mOpenAmount > 0.001f) {
			gui.drawText("^", chevron, fadeTo(s.text, mOpenAmount), s.font);
		}

		// The list has to escape whatever clips this widget, so it is not drawn here.
		if (mOpenAmount > 0.004f && count > 0) gui.drawAbove(this, origin);
	}

	namespace {
		// ASCII only, which is every letter a choice is likely to start with in a list
		// somebody is scanning by keyboard; anything else compares exactly.
		char foldCase(char c) {
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		}
		bool startsWithFolded(std::string_view text, std::string_view prefix) {
			if (prefix.size() > text.size()) return false;
			for (size_t i = 0; i < prefix.size(); ++i) {
				if (foldCase(text[i]) != foldCase(prefix[i])) return false;
			}
			return true;
		}
	}

	void Select::findTyped(const std::string& typed) {
		// About as long as anyone pauses inside a word and shorter than anyone waits
		// between two -- the same second most list boxes on most systems settled on.
		constexpr float kFindPause = 1.0f;
		if (mFindAge > kFindPause) mFind.clear();
		mFindAge = 0.0f;

		// A space that would start a search is the key that opens the list, and arrives as
		// a character as well; inside a search it is part of what is being looked for.
		const std::string_view add = (mFind.empty() && typed == " ") ? std::string_view{} : typed;
		if (add.empty()) return;
		mFind += add;

		const int options = static_cast<int>(choiceCount());
		if (options <= 0) return;

		// Shut, the search starts from what is chosen, the way it would have opened from
		// the keyboard -- so B on a list showing "Bucharest" goes to the next B.
		if (!mOpen) {
			mHighlight = -1;
			for (int at = 0; at < options; ++at) {
				std::string_view text, oneValue;
				if (choiceAt(static_cast<size_t>(at), text, oneValue) && oneValue == value) {
					mHighlight = at;
					break;
				}
			}
		}

		// The same letter again and again steps through everything starting with it, which
		// is how a long list of names is walked by its initial. Anything else narrows: it
		// looks from the row already found, so typing more of a word stays on that word.
		bool repeated = mFind.size() > 1;
		for (char c : mFind) if (foldCase(c) != foldCase(mFind[0])) { repeated = false; break; }
		const std::string_view prefix = repeated ? std::string_view(mFind).substr(0, 1)
		                                         : std::string_view(mFind);
		const int from = (mHighlight < 0) ? 0
		               : (repeated || mFind.size() == 1) ? mHighlight + 1 : mHighlight;

		for (int step = 0; step < options; ++step) {
			const int at = (from + step) % options;
			std::string_view text, ignored;
			if (!choiceAt(static_cast<size_t>(at), text, ignored)) continue;
			if (!startsWithFolded(text, prefix)) continue;
			mHighlight = at;
			mOpen = true;
			return;
		}
		// Nothing starts with that. Left where it was rather than jumping somewhere
		// arbitrary, and the letters stay until the pause, so a typo has to be waited out
		// -- which is what every list box does, and what readers already expect.
	}

	// What Enter does with the row the keyboard is on. Written once and called from the
	// key handling, so a keyboard choice and a mouse choice are the same event.
	void Select::chooseHighlighted() {
		mOpen = false;
		if (mHighlight < 0) return;
		std::string_view text, chosen;
		if (!choiceAt(static_cast<size_t>(mHighlight), text, chosen)) return;
		if (value != chosen) {
			value.assign(chosen);
			if (onChange) onChange(value);
		}
	}

	void Select::paintAbove(Gui& gui, glm::vec2 origin) {
		(void)origin;
		const ButtonStyle& s = gui.theme().button(variant);
		const float rowHeight = std::round(gui.lineHeight(s.font) + 8.0f);
		const size_t count = choiceCount();
		const Rect list{ mClosedRect.x, mClosedRect.y + mClosedRect.h + 2.0f,
		                 mClosedRect.w, rowHeight * static_cast<float>(count) };

		// Unrolled to however far open it is. The rows are laid out where they will
		// finally sit and the box revealing them grows past them, so it reads as a list
		// coming out of the control rather than as a panel appearing beside it.
		const Rect shown{ list.x, list.y, list.w, list.h * mOpenAmount };
		gui.pushClipRect(shown);
		gui.pushOpacity(mOpenAmount);
		// One on its way shut takes no input. It is still on the screen, and without this
		// the press that closed it would also pick whatever row it happened to be over.
		const bool closing = !mOpen;
		if (closing) gui.pushInert();

		// An open list is part of this control as far as the keyboard is concerned.
		//
		// The press that lands on a row is not on the control's own rect, so without this
		// it reads as a press on nothing and takes the keyboard away -- and since a row is
		// chosen on release, the value would still arrive while the ring quietly vanished.
		// Claimed rather than re-focused: it already has focus, and this only says the
		// press was ours.
		if (!closing && gui.input().lPressed && shown.contains(gui.input().pointer)) {
			gui.setFocus(gui.widgetId(mId.c_str()));
		}

		// Opaque, whatever the panel style says. A panel is translucent on purpose -- it
		// sits over content and is meant to. A dropdown is not: it is a list of words to
		// read, and the label it covers showing through it makes both unreadable.
		const uint32_t opaque = gui.theme().panel(kDefaultVariant).body | 0xFF000000u;
		gui.drawRectRounded(shown, opaque, s.radius);

		float y = list.y;
		for (size_t index = 0; index < count; ++index) {
			std::string_view oneText, oneValue;
			if (!choiceAt(index, oneText, oneValue)) continue;
			const Rect row{ list.x, y, list.w, rowHeight };
			// Each choice, for a screen reader, while the list is open: an item under the
			// dropdown, chosen if it is the one chosen, and -- if the arrows have put the
			// highlight on it -- where the reader is, so each arrow is read out as it moves.
			if (!closing && gui.accessibilityWanted()) {
				AccessibleInfo option;
				option.role = AccessibleRole::ListItem;
				option.name.assign(oneText);
				option.selected = (oneValue == value) ? 1 : 0;
				option.focusable = true;
				option.actions = kCanSelect | kCanInvoke;
				const uint32_t optionId = gui.widgetId(mId.c_str()) ^ (static_cast<uint32_t>(index + 1) * 2246822519u);
				gui.noteAccessibleItem(this, optionId, static_cast<int>(index), nullptr, row, option,
				                       mId + "#" + std::to_string(index),
				                       static_cast<int>(index) == mHighlight);
			}
			// The keyboard's row looks exactly like the pointer's: there is one
			// highlight in a list, however the reader is moving it.
			const bool over = row.contains(gui.input().pointer) ||
			                  (static_cast<int>(index) == mHighlight);
			// Faded to nothing rather than not drawn, so a pointer running down the list
			// leaves each row on its way out instead of switching them on and off. Only
			// the alpha moves: the two ends are the same colour.
			//
			// Keyed by position rather than by the option widget, because a table-backed
			// select has no widget per row -- and a row is the same row whichever it
			// came from.
			gui.drawRectRounded(row, gui.motion().colour(
				gui.motionKey(mId.c_str(), kMotionFill ^ static_cast<uint32_t>(index * 2654435761u)),
				over ? s.hovered : fadeTo(s.hovered, 0.0f),
				s.motion.seconds, s.motion.curve), 0.0f);
			gui.drawText(std::string(oneText).c_str(),
			             { row.x + 10.0f, row.y + (row.h - gui.lineHeight(s.font)) * 0.5f },
			             s.text, s.font);
			// Chosen on release rather than press: the press is what opened the list, and
			// acting on it would pick whatever happened to be under the pointer then.
			if (over && gui.input().lReleased) {
				mOpen = false;
				if (value != oneValue) {
					value.assign(oneValue);
					if (onChange) onChange(value);
				}
			}
			y += rowHeight;
		}

		if (closing) gui.popInert();
		gui.popOpacity();
		gui.popClipRect();

		// A press anywhere else closes it. Checked after the rows so a press on one of
		// them is not also a press outside, and outside the block above so it is asked
		// with the real pointer -- a list already closing has nothing to close.
		if (mOpen && gui.input().lPressed && !list.contains(gui.input().pointer)
		    && !mClosedRect.contains(gui.input().pointer)) {
			mOpen = false;
		}
	}

	bool Select::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::ComboBox;
		out.name = alt;
		const size_t count = choiceCount();
		for (size_t i = 0; i < count; ++i) {
			std::string_view text, oneValue;
			if (choiceAt(i, text, oneValue) && oneValue == value) { out.value.assign(text); break; }
		}
		out.focusable = true;
		out.expanded = mOpen ? 1 : 0;
		out.actions = kCanExpand | kCanSetValue;
		return true;
	}
	void Select::accessibleAction(AccessibleAction action, const std::string& wanted) {
		if (action == AccessibleAction::Expand)   { mOpen = true; return; }
		if (action == AccessibleAction::Collapse) { mOpen = false; mHighlight = -1; return; }
		if (action != AccessibleAction::SetValue) return;
		// By what a choice says or by what it means, whichever matches -- a screen reader
		// sends the words it read out.
		const size_t count = choiceCount();
		for (size_t i = 0; i < count; ++i) {
			std::string_view text, oneValue;
			if (!choiceAt(i, text, oneValue)) continue;
			if (text != wanted && oneValue != wanted) continue;
			if (value != oneValue) {
				value.assign(oneValue);
				if (onChange) onChange(value);
			}
			return;
		}
	}

	void Select::accessibleItemAction(int item, AccessibleAction action, const std::string& text) {
		(void)text;
		if (action != AccessibleAction::Select && action != AccessibleAction::Invoke) return;
		// Chosen from outside the way Enter chooses the highlighted row: the value, the
		// handler, and the list shut.
		mHighlight = item;
		chooseHighlighted();
	}
}
