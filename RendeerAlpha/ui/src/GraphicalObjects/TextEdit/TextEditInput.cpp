#include "TextEdit.h"
#include <Core/Utf8.h>
#include <algorithm>
#include <cctype>

// What the keyboard and an input method do to a text field: the caret moves, selection,
// the clipboard, undo and redo, the keys that send or leave, and composition.
namespace RDA::TextEdit {

	using TextState = Frame::TextState;

	// How much a text field remembers, and how long a run of edits stays one step.
	//
	// 0.7s is the gap that separates "still typing" from "came back to it". Long enough
	// that a sentence typed at speed is one Ctrl+Z, short enough that a pause to think
	// puts a boundary where the reader would draw one themselves.
	//
	// The caps are what stops a field that is never closed from keeping every version of
	// itself: whichever is reached first drops the oldest step, and a step older than a
	// hundred edits ago is not one anybody is reaching for.
	static constexpr float  kUndoCoalesceSeconds = 0.7f;
	static constexpr size_t kUndoSteps = 100;
	static constexpr size_t kUndoBytes = 1u << 20;   // 1 MB of remembered text per field

	namespace {
		// Previous / next word boundary from index i (Ctrl+Left / Ctrl+Right).
		int wordLeft(const std::string& s, int i) {
			if (i > 0) --i;
			while (i > 0 && std::isspace(static_cast<unsigned char>(s[i]))) --i;
			while (i > 0 && isWordChar(s[i - 1])) --i;
			return i;
		}
		int wordRight(const std::string& s, int i) {
			int n = static_cast<int>(s.size());
			while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
			while (i < n && isWordChar(s[i])) ++i;
			return i;
		}

		// The current selection as text (linear span, or the box rectangle joined by \n).
		std::string selectionText(Frame& f) {
			if (f.st.boxMode) {
				int cl, cc; caretToLineCol(f.starts, f.st.caret, cl, cc);
				int lo = (std::min)(f.st.boxAnchorLine, cl), hi = (std::max)(f.st.boxAnchorLine, cl);
				int a = (std::min)(f.st.boxAnchorCol, cc), b = (std::max)(f.st.boxAnchorCol, cc);
				std::string out;
				for (int L = lo; L <= hi; ++L) {
					int ll = lineLength(f.starts, f.text, L);
					int x0 = (std::min)(a, ll), x1 = (std::min)(b, ll);
					out += f.text.substr(f.starts[L] + x0, x1 - x0);
					if (L < hi) out += '\n';
				}
				return out;
			}
			if (hasLinearSel(f)) {
				int a = (std::min)(f.st.selectAnchor, f.st.caret), b = (std::max)(f.st.selectAnchor, f.st.caret);
				return f.text.substr(a, b - a);
			}
			return {};
		}

		// Delete the current selection; leaves the caret at the deletion point.
		bool deleteSelection(Frame& f) {
			if (f.st.boxMode) {
				int cl, cc; caretToLineCol(f.starts, f.st.caret, cl, cc);
				int lo = (std::min)(f.st.boxAnchorLine, cl), hi = (std::max)(f.st.boxAnchorLine, cl);
				int a = (std::min)(f.st.boxAnchorCol, cc), b = (std::max)(f.st.boxAnchorCol, cc);
				f.st.boxMode = false; f.st.selectAnchor = -1;
				if (a == b) { f.st.caret = f.starts[lo] + (std::min)(a, lineLength(f.starts, f.text, lo)); return false; }
				for (int L = hi; L >= lo; --L) { // erase bottom-up so earlier line starts stay valid
					int ll = lineLength(f.starts, f.text, L);
					int x0 = f.starts[L] + (std::min)(a, ll), x1 = f.starts[L] + (std::min)(b, ll);
					if (x1 > x0) f.text.erase(f.text.begin() + x0, f.text.begin() + x1);
				}
				computeLineStarts(f.text, f.starts);
				f.lineCount = static_cast<int>(f.starts.size());
				int L = (std::min)(lo, f.lineCount - 1);
				f.st.caret = f.starts[L] + (std::min)(a, lineLength(f.starts, f.text, L));
				return true;
			}
			if (hasLinearSel(f)) {
				int a = (std::min)(f.st.selectAnchor, f.st.caret), b = (std::max)(f.st.selectAnchor, f.st.caret);
				f.text.erase(f.text.begin() + a, f.text.begin() + b);
				f.st.caret = a; f.st.selectAnchor = -1;
				computeLineStarts(f.text, f.starts);
				f.lineCount = static_cast<int>(f.starts.size());
				return true;
			}
			f.st.selectAnchor = -1;
			return false;
		}

		// --- undo ---
		//
		// A step is taken before an edit, not after: what Ctrl+Z wants is the text as it
		// was, and the only moment that exists is just before something changes it.
		//
		// Runs of the same kind of edit collapse into one step while they keep coming, so
		// typing a word and pressing Ctrl+Z once leaves the word gone rather than its
		// last letter. A pause, a different kind of edit, or the caret being moved by
		// hand ends the run -- which is what puts the boundaries where a reader expects.
		void noteForUndo(Frame& f, TextState::EditKind kind) {
			const bool joinsRun = kind == f.st.undoKind &&
			                      kind != TextState::EditKind::Other &&
			                      f.st.undoAge <= kUndoCoalesceSeconds;
			f.st.undoKind = kind;
			f.st.undoAge = 0.0f;
			// An edit made after undoing is a new future; the old one is unreachable.
			f.st.redo.clear();
			if (joinsRun) return;
			f.st.undoBytes += f.text.size();
			f.st.undo.push_back({ f.text, f.st.caret, f.st.selectAnchor });
			while (!f.st.undo.empty() &&
			       (f.st.undo.size() > kUndoSteps || f.st.undoBytes > kUndoBytes)) {
				f.st.undoBytes -= f.st.undo.front().text.size();
				f.st.undo.erase(f.st.undo.begin());
			}
		}

		// Typed text replaces any selection.
		void typeText(Frame& f) {
			if (!f.in.typed.empty()) {
				noteForUndo(f, TextState::EditKind::Typing);
				deleteSelection(f);
				for (char c : f.in.typed) {
					if (c == '\n' || c == '\r') continue;
					f.text.insert(f.text.begin() + f.st.caret, c); f.st.caret++; f.textEdited = true;
				}
				f.st.selectAnchor = -1; f.activity = true;
			}
		}

		// One key, applied. The fields it reads are all in the frame, and the line index is
		// rebuilt first because the key before this one may have changed the text.
		void editKey(Frame& f, GuiEditKey key) {
			computeLineStarts(f.text, f.starts);
			f.lineCount = static_cast<int>(f.starts.size());
			int line, col; caretToLineCol(f.starts, f.st.caret, line, col);
			bool shift = f.in.shift, ctrl = f.in.ctrl;
			int size = static_cast<int>(f.text.size());
			auto beginMove = [&]() {
				// Moving the caret by hand ends the run of edits, for the same reason
				// clicking does: what follows is a separate thing the reader did.
				f.st.undoKind = TextState::EditKind::None;
				if (shift) { if (f.st.selectAnchor < 0 && !f.st.boxMode) f.st.selectAnchor = f.st.caret; }
				else { f.st.selectAnchor = -1; f.st.boxMode = false; }
			};
			switch (key) {
			// Backspace, Delete, Left and Right move by a character rather than by a
			// byte. Typing 'ă' and pressing Backspace once has to remove the letter,
			// not half of it -- and half of it is a broken sequence the rest of this
			// field would then have to survive.
			case GuiEditKey::Backspace:
				noteForUndo(f, TextState::EditKind::Deleting);
				if (hasLinearSel(f) || f.st.boxMode) deleteSelection(f);
				else if (f.st.caret > 0) {
					const int from = static_cast<int>(Utf8::prev(f.text, static_cast<size_t>(f.st.caret)));
					f.text.erase(f.text.begin() + from, f.text.begin() + f.st.caret);
					f.st.caret = from;
				}
				f.textEdited = true; break;
			case GuiEditKey::Delete:
				noteForUndo(f, TextState::EditKind::Deleting);
				if (hasLinearSel(f) || f.st.boxMode) deleteSelection(f);
				else if (f.st.caret < size) {
					const int to = static_cast<int>(Utf8::next(f.text, static_cast<size_t>(f.st.caret)));
					f.text.erase(f.text.begin() + f.st.caret, f.text.begin() + to);
				}
				f.textEdited = true; break;
			case GuiEditKey::Left:
				if (!shift && !ctrl && hasLinearSel(f)) { f.st.caret = (std::min)(f.st.selectAnchor, f.st.caret); f.st.selectAnchor = -1; }
				else { beginMove(); f.st.caret = ctrl ? wordLeft(f.text, f.st.caret)
				                                      : static_cast<int>(Utf8::prev(f.text, static_cast<size_t>(f.st.caret))); }
				break;
			case GuiEditKey::Right:
				if (!shift && !ctrl && hasLinearSel(f)) { f.st.caret = (std::max)(f.st.selectAnchor, f.st.caret); f.st.selectAnchor = -1; }
				else { beginMove(); f.st.caret = ctrl ? wordRight(f.text, f.st.caret)
				                                      : static_cast<int>(Utf8::next(f.text, static_cast<size_t>(f.st.caret))); }
				break;
			case GuiEditKey::Home: beginMove(); f.st.caret = f.starts[line]; break;
			case GuiEditKey::End:  beginMove(); f.st.caret = f.starts[line] + lineLength(f.starts, f.text, line); break;
			case GuiEditKey::Up:
				if (f.style.multiline && line > 0) { beginMove(); int len = lineLength(f.starts, f.text, line - 1); f.st.caret = f.starts[line - 1] + (std::min)(col, len); }
				break;
			case GuiEditKey::Down:
				if (f.style.multiline && line + 1 < f.lineCount) { beginMove(); int len = lineLength(f.starts, f.text, line + 1); f.st.caret = f.starts[line + 1] + (std::min)(col, len); }
				break;
			case GuiEditKey::Enter: {
				// Which chord sends -- submitsOn in GuiTypes.h, where the rule is
				// stated once and checked against a table. A multi-line field's
				// Ctrl+Enter used to be swallowed in silence, so hearing about it is
				// new and takes nothing away from anybody.
				const bool sending = submitsOn(f.extras ? f.extras->submit : SubmitKey::Default,
				                               f.style.multiline, ctrl, shift);
				if (sending && f.extras && f.extras->submitted) *f.extras->submitted = true;

				if (!f.style.multiline) {
					// A single line has nowhere to put a newline, so Enter means
					// "done with this" whether or not anybody is listening.
					if (!ctrl) f.focusId = 0;
					break;
				}
				// Anything that did not send is a line break -- except Ctrl+Enter,
				// which is a gesture rather than a character. Swallowed either way,
				// so a field that does not send on it does not grow a line from it.
				if (sending || ctrl) break;
				noteForUndo(f, TextState::EditKind::Other);
				deleteSelection(f); f.text.insert(f.text.begin() + f.st.caret, '\n'); f.st.caret++; f.textEdited = true;
				break;
			}
			case GuiEditKey::Tab:
				// With a completion showing, Tab takes it. Before the indent, because
				// a code field is exactly where completions are offered and taking
				// one is what the reader meant.
				//
				// The insert is an ordinary edit, so undo and onChange see it the way
				// they see typing -- which is right: once it is taken it is text.
				if (f.offering) {
					noteForUndo(f, TextState::EditKind::Other);
					deleteSelection(f);
					const std::string_view add(f.extras->suggestion);
					f.text.insert(static_cast<size_t>(f.st.caret), add);
					f.st.caret += static_cast<int>(add.size());
					f.textEdited = true;
					if (f.extras->accepted) *f.extras->accepted = true;
					f.gui.consumeFocusMove();
					break;
				}
				// A code field indents with it, and then it is not a focus move: the
				// walk is told so rather than both happening.
				if (f.style.mode == TextFieldMode::Code) {
					noteForUndo(f, TextState::EditKind::Other);
					deleteSelection(f);
					for (int i = 0; i < 4; ++i) { f.text.insert(f.text.begin() + f.st.caret, ' '); f.st.caret++; }
					f.textEdited = true;
					f.gui.consumeFocusMove();
				}
				break;
			case GuiEditKey::Escape:
				// With a completion showing, Escape drops that rather than the focus.
				// One key, two meanings, in the order the reader expects: the nearest
				// thing goes first, and pressing it again leaves the field.
				if (f.offering) {
					if (f.extras->dismissed) *f.extras->dismissed = true;
					f.activity = true;
					break;
				}
				// Leaves the field rather than the application: a field is the one
				// thing on screen that swallows every other key, so it owes the
				// reader a way out that does not involve the mouse.
				f.focusId = 0;
				break;
			case GuiEditKey::Space:
				break; // arrives as typed text too, and that is where it is inserted
			}
			f.activity = true;
		}

		// Cut is an edit, so it stays here. Copy and select-all are not, and have
		// moved above the guard -- see there.
		void cutAndPaste(Frame& f) {
			if (f.in.cut) {
				std::string sel = selectionText(f);
				if (!sel.empty()) {
					if (f.setClipboard) f.setClipboard(sel.c_str());
					noteForUndo(f, TextState::EditKind::Other);
					deleteSelection(f);
					f.textEdited = true;
				}
				f.activity = true;
			}
			if (f.in.paste && f.getClipboard) {
				std::string clip = f.getClipboard();
				noteForUndo(f, TextState::EditKind::Other);
				deleteSelection(f);
				for (char c : clip) {
					if (c == '\r') continue;
					if (c == '\n' && !f.style.multiline) continue;
					f.text.insert(f.text.begin() + f.st.caret, c); f.st.caret++;
				}
				f.st.selectAnchor = -1; f.textEdited = true; f.activity = true;
			}
		}
	}

	void undoRedo(Frame& f) {
		f.st.undoAge += f.in.dt;

		if (f.focused && !f.style.readOnly && (f.in.undo || f.in.redo)) {
			auto stepBack = [&](std::vector<TextState::Step>& from,
			                    std::vector<TextState::Step>& to) {
				if (from.empty()) { f.activity = true; return; }
				to.push_back({ f.text, f.st.caret, f.st.selectAnchor });
				TextState::Step& into = from.back();
				f.text = std::move(into.text);
				f.st.caret = into.caret;
				f.st.selectAnchor = into.anchor;
				from.pop_back();
				f.st.boxMode = false;
				// The other ring is bounded by this one -- every entry in it came from
				// here -- so only `undo`'s weight is tracked.
				f.st.undoBytes = 0;
				for (const TextState::Step& step : f.st.undo) f.st.undoBytes += step.text.size();
				// Whatever is typed next begins its own step rather than joining the run
				// this interrupted.
				f.st.undoKind = TextState::EditKind::None;
				f.textEdited = true;
				f.activity = true;
				computeLineStarts(f.text, f.starts);
				f.lineCount = static_cast<int>(f.starts.size());
			};
			if (f.in.undo) stepBack(f.st.undo, f.st.redo);
			else           stepBack(f.st.redo, f.st.undo);
		}
	}

	// Copy and select-all, before the guard below shuts read-only out.
	//
	// Selecting with the mouse already works on any field -- drag, double-click for a
	// word, triple-click for a line -- because that is handled above. Leaving Ctrl+C
	// down with the edits meant a read-only field you could select and could not
	// copy, which is the one thing selecting it was for. Neither of these changes the
	// text, so neither belongs behind a flag that means "the text cannot change".
	void selectAndCopy(Frame& f) {
		if (f.focused) {
			if (f.in.selectAll) {
				f.st.boxMode = false;
				f.st.selectAnchor = 0;
				f.st.caret = static_cast<int>(f.text.size());
				f.activity = true;
			}
			if (f.in.copy) {
				const std::string sel = selectionText(f);
				if (!sel.empty() && f.setClipboard) f.setClipboard(sel.c_str());
				f.activity = true;
			}
		}
	}

	void edit(Frame& f) {
		if (f.focused && !f.style.readOnly) {
			typeText(f);
			for (GuiEditKey key : f.in.editKeys) editKey(f, key);
			cutAndPaste(f);

			if (f.textEdited || f.activity) f.st.blink = 0.0f;
		}
	}

	void inputMethod(Frame& f) {
		// Where the caret is, for an input method: its candidates open under this. The
		// real caret rather than the eased one, so the list does not chase it.
		if (f.focused) {
			f.textEntry = true;
			f.textCaret = Rect{ f.contentLeft + f.caretX - f.st.scrollX,
			                    f.contentTop + static_cast<float>(f.caretLine) * f.lineH - f.st.scrollY,
			                    f.style.caretWidth, f.lineH };
		}

		// What an input method is composing, which is not text yet: drawn over the field
		// at the caret, underlined the way every system underlines it, with the method's
		// own cursor inside it in place of the field's. Over rather than in, because it
		// can still turn into anything -- splicing it into the text would move every line
		// and wrap after it for a string that may be gone a keystroke later.
		f.composing = f.focused && !f.in.preedit.empty();
		if (f.composing) {
			const float px = f.contentLeft + f.caretX - f.st.scrollX;
			const float py = f.contentTop + static_cast<float>(f.caretLine) * f.lineH - f.st.scrollY;
			const float pw = f.gui.measureText(f.in.preedit.c_str());
			f.addRect({ px, py, pw + f.style.caretWidth, f.lineH }, f.style.background | 0xFF000000u);
			f.addText(px, py + f.font->ascent(), f.in.preedit.c_str(), f.style.text);
			f.addRect({ px, py + f.lineH - 2.0f, pw, 1.0f }, f.style.text);
			// The method's cursor, in characters, turned into a width.
			std::string before;
			int left = f.in.preeditCaret;
			for (size_t i = 0; i < f.in.preedit.size() && left > 0; --left) {
				uint32_t cp = 0;
				const size_t step = Utf8::decode(f.in.preedit.data() + i,
				                                 f.in.preedit.data() + f.in.preedit.size(), cp);
				if (step == 0) break;
				before.append(f.in.preedit, i, step);
				i += step;
			}
			const float at = px + f.gui.measureText(before.c_str());
			f.addRect({ at, py + 1.0f, f.style.caretWidth, f.lineH - 2.0f }, f.style.caret);
		}
	}
}
