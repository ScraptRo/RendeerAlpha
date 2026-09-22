#include <GraphicalObjects/Gui.h>
#include <Core/Utf8.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

// Additional immediate-mode widgets (still Gui members, split out to keep Gui.cpp
// focused on the core frame/draw machinery): checkbox, slider, and the text field.
namespace RDA {

	TextFieldStyle TextFieldStyle::forMode(TextFieldMode mode) {
		TextFieldStyle style;
		style.mode = mode;
		switch (mode) {
		case TextFieldMode::Line:
			style.multiline = false;
			style.padding = 6.0f;
			break;
		case TextFieldMode::Document:
			style.multiline = true;
			style.padding = 12.0f;
			style.background = rgba(24, 24, 28);
			break;
		case TextFieldMode::Code:
			style.multiline = true;
			style.showLineNumbers = true;
			style.highlightCurrentLine = true;
			style.padding = 6.0f;
			style.background = rgba(16, 18, 24);
			break;
		}
		return style;
	}

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

	// ---- checkbox -----------------------------------------------------------------
	bool Gui::checkbox(const char* id, const char* label, bool& value, const Rect& rect, Variant variant) {
		const CheckboxStyle& s = mTheme.checkbox(variant);
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mHot = wid;
		if (mHot == wid && mInput.pressed) mActive = wid;

		bool clicked = false;
		if (mActive == wid && mInput.released) {
			if (mHot == wid) clicked = true;
			mActive = 0;
		}
		if (mHot == wid && mInput.pressed) setFocus(wid);
		focusable(wid, rect);
		if (focusActivated(wid)) clicked = true;
		if (clicked) value = !value;

		float box = rect.h;
		Rect boxRect{ rect.x, rect.y, box, box };
		const uint32_t boxColour = motion().colour(wid ^ kMotionFill,
			(mHot == wid) ? s.boxHover : s.box, s.motion.seconds, s.motion.curve);
		const float radius = motion().value(wid ^ kMotionRadius, s.radius,
		                                    s.motion.seconds, s.motion.curve);
		addFrame(boxRect, boxColour, s.border, s.borderWidth, radius);

		// The mark grows and fades rather than appearing. Ticking a box is the smallest
		// thing an interface confirms, and it is the one most worth confirming visibly.
		const float shown = motion().value(wid ^ kMotionMark, value ? 1.0f : 0.0f,
		                                   s.motion.seconds, s.motion.curve);
		if (shown > 0.004f) {
			const float inset = box * s.checkInset;
			const float full = box - 2.0f * inset;
			const float size = full * shown;
			const float centre = (full - size) * 0.5f;
			addRectRounded({ rect.x + inset + centre, rect.y + inset + centre, size, size },
			               fadeTo(s.check, shown), (std::max)(0.0f, radius - inset));
		}
		if (mFont && label && label[0]) {
			float baselineY = rect.y + rect.h * 0.5f + baseline(s.font) * 0.35f;
			addText(rect.x + box + 8.0f, baselineY, label, s.label, s.font);
		}
		return clicked;
	}

	// ---- slider -------------------------------------------------------------------
	bool Gui::sliderFloat(const char* id, float& value, float minValue, float maxValue, const Rect& rect, Variant variant) {
		const SliderStyle& s = mTheme.slider(variant);
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mHot = wid;
		if (mHot == wid && mInput.pressed) mActive = wid;

		bool changed = false;
		if (mActive == wid) {
			float t = (rect.w > 0.0f) ? (mInput.pointer.x - rect.x) / rect.w : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			float next = minValue + t * (maxValue - minValue);
			if (next != value) { value = next; changed = true; }
			if (mInput.released) mActive = 0;
		}

		if (mHot == wid && mInput.pressed) setFocus(wid);
		focusable(wid, rect);
		// A twentieth of the range per press, which is a usable number of presses to
		// cross it and small enough to land on a particular value.
		if (const int step = focusStep(wid); step != 0) {
			const float span = maxValue - minValue;
			const float next = std::clamp(value + step * span * 0.05f, minValue, maxValue);
			if (next != value) { value = next; changed = true; }
		}

		float t = (maxValue > minValue) ? (value - minValue) / (maxValue - minValue) : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		const float radius = motion().value(wid ^ kMotionRadius, s.radius,
		                                    s.motion.seconds, s.motion.curve);
		addRectRounded(rect, s.track, radius);                                  // track
		addRectRounded({ rect.x, rect.y, rect.w * t, rect.h }, s.fill, radius); // fill
		float knob = s.knobWidth;
		addRectRounded({ rect.x + rect.w * t - knob * 0.5f, rect.y - 2.0f, knob, rect.h + 4.0f },
		               (mActive == wid) ? s.knobActive : s.knob, radius);
		return changed;
	}

	// ---- splitter -------------------------------------------------------------------
	bool Gui::splitter(const char* id, float& size, float minSize, float maxSize,
	                   const Rect& rect, bool vertical) {
		uint32_t wid = scopedId(id);
		const bool inside = rect.contains(mInput.pointer);
		if (inside) mHot = wid;
		if (mHot == wid && mInput.pressed) {
			mActive = wid;
			// The grab offset is stored as the size the pane had when the drag began,
			// biased by the pointer, so the bar does not jump to the cursor on grab.
			mTextStates[wid].scrollGrab = (vertical ? mInput.pointer.x : mInput.pointer.y) - size;
		}

		bool changed = false;
		if (mActive == wid) {
			mFocusClaimed = true;
			const float pointer = vertical ? mInput.pointer.x : mInput.pointer.y;
			float next = std::clamp(pointer - mTextStates[wid].scrollGrab, minSize, maxSize);
			if (next != size) { size = next; changed = true; }
			if (mInput.released) mActive = 0;
		}

		const bool active = (mActive == wid) || (mHot == wid);
		addRect(rect, active ? rgba(90, 110, 150) : rgba(52, 58, 70));
		// A short grip in the middle, so the bar reads as draggable rather than as a rule.
		const float gripLength = (vertical ? rect.h : rect.w) * 0.25f;
		if (vertical) {
			addRect({ rect.x + rect.w * 0.5f - 1.0f, rect.y + (rect.h - gripLength) * 0.5f,
			          2.0f, gripLength }, rgba(150, 165, 190));
		} else {
			addRect({ rect.x + (rect.w - gripLength) * 0.5f, rect.y + rect.h * 0.5f - 1.0f,
			          gripLength, 2.0f }, rgba(150, 165, 190));
		}
		return changed;
	}

	// ---- text field ---------------------------------------------------------------
	namespace {
		// Start index of each line in `s` (split on '\n'); always at least one line.
		void computeLineStarts(const std::string& s, std::vector<int>& starts) {
			starts.clear();
			starts.push_back(0);
			for (int i = 0; i < static_cast<int>(s.size()); ++i) {
				if (s[i] == '\n') starts.push_back(i + 1);
			}
		}
		void caretToLineCol(const std::vector<int>& starts, int caret, int& line, int& col) {
			line = 0;
			for (int i = 0; i < static_cast<int>(starts.size()); ++i) {
				if (starts[i] <= caret) line = i; else break;
			}
			col = caret - starts[line];
		}
		int lineLength(const std::vector<int>& starts, const std::string& s, int line) {
			int start = starts[line];
			int end = (line + 1 < static_cast<int>(starts.size())) ? starts[line + 1] - 1
			                                                       : static_cast<int>(s.size());
			return end - start;
		}
		// A byte at 0x80 or above is part of a UTF-8 sequence, and every codepoint this
		// engine draws above ASCII is a letter or a mark on one. Counting them as word
		// characters is what stops Ctrl+Left from halting in the middle of "café", and it
		// needs no decoding: a continuation byte is as much part of the word as its lead.
		bool isWordChar(char c) {
			const unsigned char b = static_cast<unsigned char>(c);
			return b >= 0x80 || std::isalnum(b) != 0 || b == '_';
		}
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
	}

	bool Gui::textField(const char* id, std::string& text, const std::string& placeholder, const Rect& rect, const TextFieldStyle& style,
	                    bool* outFocused, uint64_t version, const TextFieldExtras* extras) {
		uint32_t wid = scopedId(id);
		TextState& st = mTextStates[wid];

		// A suggestion only exists while there is one to show. Everything below asks this
		// rather than the pointer, so a field handed an empty string behaves as a field
		// that was handed nothing.
		const bool offering = extras && extras->suggestion && *extras->suggestion;

		const float lineH = mFont ? mFont->lineAdvance() : 16.0f;
		bool inside = rect.contains(mInput.pointer);

		// Gutter (Code line numbers) sits inside the field on the left. `starts` aliases
		// the per-field scratch buffer, so the repeated recomputes below reuse capacity.
		std::vector<int>& starts = st.lineStarts;

		// Rebuilding this walks the whole string, and it used to happen twice per frame
		// regardless of whether anything had changed -- on a large document that dwarfs
		// the cost of drawing the forty lines actually on screen. A caller that can say
		// its text is unchanged keeps the index instead.
		bool textEdited = false;
		const bool versioned = version != 0;
		if (!versioned || version != st.indexedVersion || text.size() != st.indexedSize) {
			computeLineStarts(text, starts);
			st.indexedVersion = version;
			st.indexedSize = text.size();
		}
		int lineCount = static_cast<int>(starts.size());

		float gutterW = 0.0f;
		if (style.showLineNumbers && mFont) {
			int digits = 1; for (int n = lineCount; n >= 10; n /= 10) ++digits;
			gutterW = mFont->advance(U'0') * (std::max)(digits, 2) + 12.0f;
		}
		const float scrollBarW = style.multiline ? 12.0f : 0.0f; // right strip for the scroll bar
		float contentLeft = rect.x + gutterW + style.padding;
		float contentTop = rect.y + style.padding;
		float contentRight = rect.x + rect.w - style.padding - scrollBarW;

		// Helper: caret X within its line (pixels from contentLeft, pre-scroll).
		//
		// A column is a byte offset -- which is what the rest of this field indexes with
		// -- but its width is the width of the characters those bytes spell, so this asks
		// the atlas to measure the range rather than summing byte by byte.
		auto caretXInLine = [&](int lineStart, int col) -> float {
			if (!mFont) return 0.0f;
			return mFont->textWidth(text.data() + lineStart, text.data() + lineStart + col);
		};

		// Map a pointer position to a (line, col) within the text. The column it returns
		// is always on a character boundary, because it only ever moves by whole ones.
		auto lineColAt = [&](glm::vec2 p, int& outLine, int& outCol) {
			int line = 0;
			if (style.multiline) {
				line = static_cast<int>((p.y - contentTop + st.scrollY) / lineH);
				line = std::clamp(line, 0, lineCount - 1);
			}
			int len = lineLength(starts, text, line);
			float targetX = p.x - contentLeft + st.scrollX;
			const char* lineBegin = text.data() + starts[line];
			const char* lineEnd = lineBegin + len;
			int col = 0; float acc = 0.0f; float best = std::fabs(targetX);
			int i = 0;
			while (i < len) {
				uint32_t cp = 0;
				i += static_cast<int>(Utf8::decode(lineBegin + i, lineEnd, cp));
				acc += mFont ? mFont->advance(cp) : 0.0f;
				if (std::fabs(acc - targetX) < best) { best = std::fabs(acc - targetX); col = i; }
			}
			outLine = line; outCol = col;
		};
		auto hasLinearSel = [&]() { return !st.boxMode && st.selectAnchor >= 0 && st.selectAnchor != st.caret; };

		// The current selection as text (linear span, or the box rectangle joined by \n).
		auto selectionText = [&]() -> std::string {
			if (st.boxMode) {
				int cl, cc; caretToLineCol(starts, st.caret, cl, cc);
				int lo = (std::min)(st.boxAnchorLine, cl), hi = (std::max)(st.boxAnchorLine, cl);
				int a = (std::min)(st.boxAnchorCol, cc), b = (std::max)(st.boxAnchorCol, cc);
				std::string out;
				for (int L = lo; L <= hi; ++L) {
					int ll = lineLength(starts, text, L);
					int x0 = (std::min)(a, ll), x1 = (std::min)(b, ll);
					out += text.substr(starts[L] + x0, x1 - x0);
					if (L < hi) out += '\n';
				}
				return out;
			}
			if (hasLinearSel()) {
				int a = (std::min)(st.selectAnchor, st.caret), b = (std::max)(st.selectAnchor, st.caret);
				return text.substr(a, b - a);
			}
			return {};
		};

		// Delete the current selection; leaves the caret at the deletion point.
		auto deleteSelection = [&]() -> bool {
			if (st.boxMode) {
				int cl, cc; caretToLineCol(starts, st.caret, cl, cc);
				int lo = (std::min)(st.boxAnchorLine, cl), hi = (std::max)(st.boxAnchorLine, cl);
				int a = (std::min)(st.boxAnchorCol, cc), b = (std::max)(st.boxAnchorCol, cc);
				st.boxMode = false; st.selectAnchor = -1;
				if (a == b) { st.caret = starts[lo] + (std::min)(a, lineLength(starts, text, lo)); return false; }
				for (int L = hi; L >= lo; --L) { // erase bottom-up so earlier line starts stay valid
					int ll = lineLength(starts, text, L);
					int x0 = starts[L] + (std::min)(a, ll), x1 = starts[L] + (std::min)(b, ll);
					if (x1 > x0) text.erase(text.begin() + x0, text.begin() + x1);
				}
				computeLineStarts(text, starts);
				lineCount = static_cast<int>(starts.size());
				int L = (std::min)(lo, lineCount - 1);
				st.caret = starts[L] + (std::min)(a, lineLength(starts, text, L));
				return true;
			}
			if (hasLinearSel()) {
				int a = (std::min)(st.selectAnchor, st.caret), b = (std::max)(st.selectAnchor, st.caret);
				text.erase(text.begin() + a, text.begin() + b);
				st.caret = a; st.selectAnchor = -1;
				computeLineStarts(text, starts);
				lineCount = static_cast<int>(starts.size());
				return true;
			}
			st.selectAnchor = -1;
			return false;
		};

		// --- focus + mouse selection ---
		bool inTextArea = mInput.pointer.x < rect.x + rect.w - scrollBarW;
		if (mInput.pressed && inside && inTextArea) {
			mFocused = wid; mFocusClaimed = true; mActive = wid; // begin a drag
			if (mFont) {
				int line, col; lineColAt(mInput.pointer, line, col);
				int idx = starts[line] + col;
				// A word- or line-selecting click must not be undone by the drag handler
				// below, which would collapse it on the slightest mouse movement.
				st.dragGranularity = (mInput.clickCount >= 2 && !mInput.alt && !mInput.shift)
					? mInput.clickCount : 1;

				if (mInput.alt && style.multiline) {
					st.boxMode = true; st.boxAnchorLine = line; st.boxAnchorCol = col; st.selectAnchor = -1;
					st.caret = idx;
				} else if (mInput.shift) {
					st.boxMode = false;
					if (st.selectAnchor < 0) st.selectAnchor = st.caret; // extend from the old caret
					st.caret = idx;
				} else if (mInput.clickCount == 2) {
					// Double click selects the word under the pointer. Clicking in the run
					// of spaces between words selects that run, so the gesture always
					// selects *something* rather than collapsing.
					st.boxMode = false;
					const int size = static_cast<int>(text.size());
					auto isSpace = [&](int i) {
						return std::isspace(static_cast<unsigned char>(text[i])) != 0;
					};
					if (idx < size && !isSpace(idx) && isWordChar(text[idx])) {
						int begin = idx, end = idx;
						while (begin > 0 && isWordChar(text[begin - 1])) --begin;
						while (end < size && isWordChar(text[end])) ++end;
						st.selectAnchor = begin; st.caret = end;
					} else if (idx < size && isSpace(idx) && text[idx] != '\n') {
						int begin = idx, end = idx;
						while (begin > 0 && isSpace(begin - 1) && text[begin - 1] != '\n') --begin;
						while (end < size && isSpace(end) && text[end] != '\n') ++end;
						st.selectAnchor = begin; st.caret = end;
					} else {
						// Punctuation, or the end of a line: fall back to a plain caret.
						st.selectAnchor = idx; st.caret = idx;
					}
				} else if (mInput.clickCount >= 3) {
					// Triple click takes the whole line, newline excluded.
					st.boxMode = false;
					st.selectAnchor = starts[line];
					st.caret = starts[line] + lineLength(starts, text, line);
				} else {
					st.boxMode = false; st.selectAnchor = idx; // collapses if the drag doesn't move
					st.caret = idx;
				}
			}
			st.blink = 0.0f;
			// Putting the caret somewhere by hand ends the run of edits: typing a word,
			// clicking elsewhere and typing another should be two undos, not one.
			st.undoKind = TextState::EditKind::None;
		} else if (mInput.pressed && inside) {
			mFocused = wid; mFocusClaimed = true; // press on the scroll-bar strip: keep focus
		}
		// Drag to extend the selection (linear or box, per how it began).
		if (mActive == wid) {
			mFocusClaimed = true;
			// Only a plain click drags the caret; a word or line selection stays as the
			// click made it until the button is released.
			if (st.dragGranularity == 1 && mInput.down && !mInput.pressed && mFont) {
				int line, col; lineColAt(mInput.pointer, line, col);
				st.caret = starts[line] + col;
				st.blink = 0.0f;
			}
			if (mInput.released) mActive = 0;
		}
		// In the Tab order like everything else that takes the keyboard. It draws its own
		// caret, so the ring is the only thing this adds -- and a field reached by Tab
		// with no ring would be a field nobody can see they are in.
		focusable(wid, rect);
		bool focused = (mFocused == wid);
		if (outFocused) *outFocused = focused;

		// --- editing ---
		// A reference to the frame-scoped flag, so what the edits below report is still
		// readable after this block ends -- which is where the index is brought back into
		// step with the text.
		bool& changed = textEdited;
		bool activity = false;

		// --- undo ---
		//
		// A step is taken before an edit, not after: what Ctrl+Z wants is the text as it
		// was, and the only moment that exists is just before something changes it.
		//
		// Runs of the same kind of edit collapse into one step while they keep coming, so
		// typing a word and pressing Ctrl+Z once leaves the word gone rather than its
		// last letter. A pause, a different kind of edit, or the caret being moved by
		// hand ends the run -- which is what puts the boundaries where a reader expects.
		st.undoAge += mInput.dt;
		auto noteForUndo = [&](TextState::EditKind kind) {
			const bool joinsRun = kind == st.undoKind &&
			                      kind != TextState::EditKind::Other &&
			                      st.undoAge <= kUndoCoalesceSeconds;
			st.undoKind = kind;
			st.undoAge = 0.0f;
			// An edit made after undoing is a new future; the old one is unreachable.
			st.redo.clear();
			if (joinsRun) return;
			st.undoBytes += text.size();
			st.undo.push_back({ text, st.caret, st.selectAnchor });
			while (!st.undo.empty() &&
			       (st.undo.size() > kUndoSteps || st.undoBytes > kUndoBytes)) {
				st.undoBytes -= st.undo.front().text.size();
				st.undo.erase(st.undo.begin());
			}
		};

		if (focused && !style.readOnly && (mInput.undo || mInput.redo)) {
			auto stepBack = [&](std::vector<TextState::Step>& from,
			                    std::vector<TextState::Step>& to) {
				if (from.empty()) { activity = true; return; }
				to.push_back({ text, st.caret, st.selectAnchor });
				TextState::Step& into = from.back();
				text = std::move(into.text);
				st.caret = into.caret;
				st.selectAnchor = into.anchor;
				from.pop_back();
				st.boxMode = false;
				// The other ring is bounded by this one -- every entry in it came from
				// here -- so only `undo`'s weight is tracked.
				st.undoBytes = 0;
				for (const TextState::Step& step : st.undo) st.undoBytes += step.text.size();
				// Whatever is typed next begins its own step rather than joining the run
				// this interrupted.
				st.undoKind = TextState::EditKind::None;
				changed = true;
				activity = true;
				computeLineStarts(text, starts);
				lineCount = static_cast<int>(starts.size());
			};
			if (mInput.undo) stepBack(st.undo, st.redo);
			else             stepBack(st.redo, st.undo);
		}

		// Copy and select-all, before the guard below shuts read-only out.
		//
		// Selecting with the mouse already works on any field -- drag, double-click for a
		// word, triple-click for a line -- because that is handled above. Leaving Ctrl+C
		// down with the edits meant a read-only field you could select and could not
		// copy, which is the one thing selecting it was for. Neither of these changes the
		// text, so neither belongs behind a flag that means "the text cannot change".
		if (focused) {
			if (mInput.selectAll) {
				st.boxMode = false;
				st.selectAnchor = 0;
				st.caret = static_cast<int>(text.size());
				activity = true;
			}
			if (mInput.copy) {
				const std::string sel = selectionText();
				if (!sel.empty() && mSetClipboard) mSetClipboard(sel.c_str());
				activity = true;
			}
		}

		if (focused && !style.readOnly) {
			// Typed text replaces any selection.
			if (!mInput.typed.empty()) {
				noteForUndo(TextState::EditKind::Typing);
				deleteSelection();
				for (char c : mInput.typed) {
					if (c == '\n' || c == '\r') continue;
					text.insert(text.begin() + st.caret, c); st.caret++; changed = true;
				}
				st.selectAnchor = -1; activity = true;
			}
			for (GuiEditKey key : mInput.editKeys) {
				computeLineStarts(text, starts);
				lineCount = static_cast<int>(starts.size());
				int line, col; caretToLineCol(starts, st.caret, line, col);
				bool shift = mInput.shift, ctrl = mInput.ctrl;
				int size = static_cast<int>(text.size());
				auto beginMove = [&]() {
					// Moving the caret by hand ends the run of edits, for the same reason
					// clicking does: what follows is a separate thing the reader did.
					st.undoKind = TextState::EditKind::None;
					if (shift) { if (st.selectAnchor < 0 && !st.boxMode) st.selectAnchor = st.caret; }
					else { st.selectAnchor = -1; st.boxMode = false; }
				};
				switch (key) {
				// Backspace, Delete, Left and Right move by a character rather than by a
				// byte. Typing 'ă' and pressing Backspace once has to remove the letter,
				// not half of it -- and half of it is a broken sequence the rest of this
				// field would then have to survive.
				case GuiEditKey::Backspace:
					noteForUndo(TextState::EditKind::Deleting);
					if (hasLinearSel() || st.boxMode) deleteSelection();
					else if (st.caret > 0) {
						const int from = static_cast<int>(Utf8::prev(text, static_cast<size_t>(st.caret)));
						text.erase(text.begin() + from, text.begin() + st.caret);
						st.caret = from;
					}
					changed = true; break;
				case GuiEditKey::Delete:
					noteForUndo(TextState::EditKind::Deleting);
					if (hasLinearSel() || st.boxMode) deleteSelection();
					else if (st.caret < size) {
						const int to = static_cast<int>(Utf8::next(text, static_cast<size_t>(st.caret)));
						text.erase(text.begin() + st.caret, text.begin() + to);
					}
					changed = true; break;
				case GuiEditKey::Left:
					if (!shift && !ctrl && hasLinearSel()) { st.caret = (std::min)(st.selectAnchor, st.caret); st.selectAnchor = -1; }
					else { beginMove(); st.caret = ctrl ? wordLeft(text, st.caret)
					                                   : static_cast<int>(Utf8::prev(text, static_cast<size_t>(st.caret))); }
					break;
				case GuiEditKey::Right:
					if (!shift && !ctrl && hasLinearSel()) { st.caret = (std::max)(st.selectAnchor, st.caret); st.selectAnchor = -1; }
					else { beginMove(); st.caret = ctrl ? wordRight(text, st.caret)
					                                   : static_cast<int>(Utf8::next(text, static_cast<size_t>(st.caret))); }
					break;
				case GuiEditKey::Home: beginMove(); st.caret = starts[line]; break;
				case GuiEditKey::End:  beginMove(); st.caret = starts[line] + lineLength(starts, text, line); break;
				case GuiEditKey::Up:
					if (style.multiline && line > 0) { beginMove(); int len = lineLength(starts, text, line - 1); st.caret = starts[line - 1] + (std::min)(col, len); }
					break;
				case GuiEditKey::Down:
					if (style.multiline && line + 1 < lineCount) { beginMove(); int len = lineLength(starts, text, line + 1); st.caret = starts[line + 1] + (std::min)(col, len); }
					break;
				case GuiEditKey::Enter: {
					// Which chord sends -- submitsOn in GuiTypes.h, where the rule is
					// stated once and checked against a table. A multi-line field's
					// Ctrl+Enter used to be swallowed in silence, so hearing about it is
					// new and takes nothing away from anybody.
					const bool sending = submitsOn(extras ? extras->submit : SubmitKey::Default,
					                               style.multiline, ctrl, shift);
					if (sending && extras && extras->submitted) *extras->submitted = true;

					if (!style.multiline) {
						// A single line has nowhere to put a newline, so Enter means
						// "done with this" whether or not anybody is listening.
						if (!ctrl) mFocused = 0;
						break;
					}
					// Anything that did not send is a line break -- except Ctrl+Enter,
					// which is a gesture rather than a character. Swallowed either way,
					// so a field that does not send on it does not grow a line from it.
					if (sending || ctrl) break;
					noteForUndo(TextState::EditKind::Other);
					deleteSelection(); text.insert(text.begin() + st.caret, '\n'); st.caret++; changed = true;
					break;
				}
				case GuiEditKey::Tab:
					// With a completion showing, Tab takes it. Before the indent, because
					// a code field is exactly where completions are offered and taking
					// one is what the reader meant.
					//
					// The insert is an ordinary edit, so undo and onChange see it the way
					// they see typing -- which is right: once it is taken it is text.
					if (offering) {
						noteForUndo(TextState::EditKind::Other);
						deleteSelection();
						const std::string_view add(extras->suggestion);
						text.insert(static_cast<size_t>(st.caret), add);
						st.caret += static_cast<int>(add.size());
						changed = true;
						if (extras->accepted) *extras->accepted = true;
						consumeFocusMove();
						break;
					}
					// A code field indents with it, and then it is not a focus move: the
					// walk is told so rather than both happening.
					if (style.mode == TextFieldMode::Code) {
						noteForUndo(TextState::EditKind::Other);
						deleteSelection();
						for (int i = 0; i < 4; ++i) { text.insert(text.begin() + st.caret, ' '); st.caret++; }
						changed = true;
						consumeFocusMove();
					}
					break;
				case GuiEditKey::Escape:
					// With a completion showing, Escape drops that rather than the focus.
					// One key, two meanings, in the order the reader expects: the nearest
					// thing goes first, and pressing it again leaves the field.
					if (offering) {
						if (extras->dismissed) *extras->dismissed = true;
						activity = true;
						break;
					}
					// Leaves the field rather than the application: a field is the one
					// thing on screen that swallows every other key, so it owes the
					// reader a way out that does not involve the mouse.
					mFocused = 0;
					break;
				case GuiEditKey::Space:
					break; // arrives as typed text too, and that is where it is inserted
				}
				activity = true;
			}

			// Cut is an edit, so it stays here. Copy and select-all are not, and have
			// moved above the guard -- see there.
			if (mInput.cut) {
				std::string sel = selectionText();
				if (!sel.empty()) {
					if (mSetClipboard) mSetClipboard(sel.c_str());
					noteForUndo(TextState::EditKind::Other);
					deleteSelection();
					changed = true;
				}
				activity = true;
			}
			if (mInput.paste && mGetClipboard) {
				std::string clip = mGetClipboard();
				noteForUndo(TextState::EditKind::Other);
				deleteSelection();
				for (char c : clip) {
					if (c == '\r') continue;
					if (c == '\n' && !style.multiline) continue;
					text.insert(text.begin() + st.caret, c); st.caret++;
				}
				st.selectAnchor = -1; changed = true; activity = true;
			}

			if (changed || activity) st.blink = 0.0f;
		}

		// Bring the line layout back into step, but only if something actually edited the
		// text: this is the second of the two full scans that used to run every frame.
		if (textEdited) {
			computeLineStarts(text, starts);
			st.indexedSize = text.size();
		}
		lineCount = static_cast<int>(starts.size());
		st.caret = std::clamp(st.caret, 0, static_cast<int>(text.size()));
		if (st.selectAnchor > static_cast<int>(text.size())) st.selectAnchor = static_cast<int>(text.size());
		// A caret is a byte offset, and the things that move it without decoding -- Up and
		// Down keeping a column, a box selection keeping a rectangle, the clamp above
		// against a string that just got shorter -- can land it inside a character.
		// Snapping here, in the one place they all pass through, is what lets everything
		// else go on treating it as a plain index.
		st.caret = static_cast<int>(Utf8::floorBoundary(text, static_cast<size_t>(st.caret)));
		if (st.selectAnchor > 0) {
			st.selectAnchor = static_cast<int>(Utf8::floorBoundary(text, static_cast<size_t>(st.selectAnchor)));
		}
		int caretLine, caretCol; caretToLineCol(starts, st.caret, caretLine, caretCol);
		float caretX = caretXInLine(starts[caretLine], caretCol);

		// Where the caret is, and where it is drawn on its way there.
		//
		// Eased in the *content*, not on the screen: scrolling the view moves the caret
		// across the window without moving it through the text, and easing the screen
		// position would have it drift out of the line it is in every time the view moved.
		// Subtracting the scroll afterwards keeps it welded to the character it is beside.
		//
		// Not while the pointer is doing it. A click puts the caret where you pointed and
		// a drag carries it with you; easing either means the caret trails the hand. Same
		// rule as a scroll bar's thumb -- and tracked rather than skipped, because a value
		// nobody asks for is dropped and the next keystroke would glide from nowhere.
		float drawnCaretX = caretX;
		float drawnCaretY = static_cast<float>(caretLine) * lineH;
		if (style.motion.seconds > 0.0f) {
			const uint32_t caretKey = wid ^ kMotionPlace;
			if (mActive == wid) {
				mMotion.reset(caretKey + 0u, drawnCaretX);
				mMotion.reset(caretKey + 1u, drawnCaretY);
			} else {
				drawnCaretX = mMotion.value(caretKey + 0u, drawnCaretX,
				                            style.motion.seconds, style.motion.curve);
				drawnCaretY = mMotion.value(caretKey + 1u, drawnCaretY,
				                            style.motion.seconds, style.motion.curve);
			}
		}

		// --- scrolling ---
		float viewW = contentRight - contentLeft;
		float viewH = rect.h - 2.0f * style.padding;
		float contentH = lineCount * lineH;
		float maxScrollY = style.multiline ? (std::max)(0.0f, contentH - viewH) : 0.0f;

		// Keep the caret visible, but only while it is actually being moved, so the wheel
		// and scroll bar aren't fought when the user is just reading.
		bool caretActive = focused && (!mInput.typed.empty() || !mInput.editKeys.empty() || (mInput.pressed && inside) || mActive == wid);
		if (caretActive) {
			if (caretX - st.scrollX > viewW) st.scrollX = caretX - viewW;
			if (caretX - st.scrollX < 0.0f)  st.scrollX = caretX;
			if (style.multiline) {
				float caretTop = caretLine * lineH;
				if (caretTop - st.scrollY > viewH - lineH) st.scrollY = caretTop - (viewH - lineH);
				if (caretTop - st.scrollY < 0.0f)          st.scrollY = caretTop;
			}
		}

		// Mouse wheel over the field scrolls it — but only while it has somewhere to go.
		// A field with no overflow, or one already at the end it is being pushed towards,
		// leaves the wheel alone so an enclosing Scroll picks it up instead. Without
		// that, a short cell inside a long notebook would swallow the gesture.
		if (style.multiline && inside && mInput.scroll != 0.0f && !mScrollConsumed &&
		    maxScrollY > 0.0f &&
		    !(mInput.scroll > 0.0f && st.scrollY <= 0.0f) &&
		    !(mInput.scroll < 0.0f && st.scrollY >= maxScrollY)) {
			st.scrollY -= mInput.scroll * lineH * 3.0f;
			mScrollConsumed = true;
		}

		// Scroll-bar thumb drag.
		uint32_t scrollId = wid ^ 0x5c011ba7u;
		Rect scrollTrack{ rect.x + rect.w - scrollBarW + 1.0f, contentTop, scrollBarW - 2.0f, viewH };
		float thumbH = 0.0f, trackRange = 0.0f;
		if (style.multiline && maxScrollY > 0.0f) {
			thumbH = (std::max)(24.0f, viewH * (viewH / contentH));
			trackRange = scrollTrack.h - thumbH;
			if (scrollTrack.contains(mInput.pointer) && mInput.pressed) {
				float thumbY = scrollTrack.y + (st.scrollY / maxScrollY) * trackRange;
				st.scrollGrab = (mInput.pointer.y < thumbY || mInput.pointer.y > thumbY + thumbH)
					? thumbH * 0.5f : mInput.pointer.y - thumbY;
				mActive = scrollId;
				mFocusClaimed = true;
			}
			if (mActive == scrollId) {
				float ny = std::clamp(mInput.pointer.y - st.scrollGrab, scrollTrack.y, scrollTrack.y + trackRange);
				st.scrollY = (trackRange > 0.0f) ? ((ny - scrollTrack.y) / trackRange) * maxScrollY : 0.0f;
				if (mInput.released) mActive = 0;
			}
		}

		st.scrollX = (std::max)(0.0f, st.scrollX);
		st.scrollY = std::clamp(st.scrollY, 0.0f, maxScrollY);

		// --- syntax highlighting ---
		// Re-lex only when the text or the language actually changed; an idle editor
		// reuses the cached spans.
		// The element's grammar wins over the variant's, so one `code` variant carries
		// the palette and each field says what it is holding.
		const bool ownLanguage = extras && extras->language && *extras->language;
		const Language* lang = ownLanguage ? mSyntax.forField(std::string(extras->language))
		                                   : mSyntax.forField(style.language);
		if (lang && versioned) {
			// Hashing the text to notice a change is itself a walk of the whole string,
			// so a caller that knows is asked rather than measured.
			if (textEdited || version != st.tokenVersion || lang != st.tokenLang) {
				SyntaxRegistry::tokenize(*lang, text, st.tokens);
				st.tokenVersion = version;
				st.tokenLang = lang;
			}
		} else if (lang) {
			size_t hash = std::hash<std::string>{}(text);
			if (hash != st.tokenHash || lang != st.tokenLang) {
				SyntaxRegistry::tokenize(*lang, text, st.tokens);
				st.tokenHash = hash;
				st.tokenLang = lang;
			}
		} else if (st.tokenLang) {
			st.tokens.clear();
			st.tokenLang = nullptr;
			st.tokenHash = 0;
		}

		// --- render ---
		const float fieldRadius = motion().value(wid ^ kMotionRadius, style.radius,
		                                         style.motion.seconds, style.motion.curve);
		addFrame(rect, style.background, style.border, style.borderWidth, fieldRadius);
		if (gutterW > 0.0f) addRect({ rect.x, rect.y, gutterW, rect.h }, style.gutter);
		if (style.highlightCurrentLine && focused) {
			float y = contentTop + caretLine * lineH - st.scrollY;
			addRect({ contentLeft - 4.0f, y, viewW + 8.0f, lineH }, style.currentLine);
		}

		// Text (and caret) clipped to the content area, right of the gutter.
		pushClip(glm::vec4(contentLeft, rect.y, contentRight, rect.y + rect.h));
		if (mFont) {
			if (text.empty() && !placeholder.empty()) {
				addText(contentLeft - st.scrollX, contentTop + baseline(), placeholder.c_str(), style.placeholder);
			}
			int firstLine = style.multiline ? (std::max)(0, static_cast<int>(st.scrollY / lineH)) : 0;
			int lastLine = style.multiline
				? (std::min)(lineCount - 1, firstLine + static_cast<int>((rect.h) / lineH) + 1)
				: 0;

			// Selection highlight, drawn under the text.
			//
			// How much of it is there. Asked for on every frame, selection or not: a
			// value nobody asks for is dropped, and one that had been dropped would put
			// the next selection on the screen at full strength instead of fading it in.
			//
			// Nothing fades while the pointer is drawing it out. A drag is the hand, and
			// a highlight that lagged the hand would trail the words being swept over.
			// The same reason a double-clicked word arrives solid: that is a pointer
			// gesture too. What fades is a selection asked for from the keyboard --
			// select-all, or shift and a movement key.
			//
			// There is no fade *out*. A selection going is the one moment you need to be
			// certain it has gone, because the next thing typed either replaces it or
			// does not; a highlight lingering over text about to be overwritten says the
			// opposite of what is true. The value still runs down to zero while nothing
			// is drawn from it, so the next selection starts from where this one left.
			float selShown = 1.0f;
			if (style.motion.seconds > 0.0f) {
				const uint32_t selKey = wid ^ kMotionFill;
				const float wanted = (st.boxMode || hasLinearSel()) ? 1.0f : 0.0f;
				if (mActive == wid) {
					mMotion.reset(selKey, wanted);
					selShown = wanted;
				} else {
					selShown = mMotion.value(selKey, wanted, style.motion.seconds,
					                         style.motion.curve);
				}
			}

			if ((st.boxMode || hasLinearSel()) && selShown > 0.004f) {
				int caretL, caretC; caretToLineCol(starts, st.caret, caretL, caretC);
				// The edge that moves is the caret's, and the caret is already being
				// eased -- so the highlight is drawn to the same value. Left to compute
				// its own, the caret glides to its new column while the block it bounds
				// jumps there, and for the length of the glide the caret sits inside its
				// own selection.
				//
				// Only along the line it is on: part-way through a move between lines the
				// drawn x belongs to a line it is passing over, not to either end. And
				// not in box mode, whose edges are a rectangle rather than a caret.
				const bool caretOnItsLine = std::fabs(drawnCaretY -
					static_cast<float>(caretL) * lineH) < 0.5f;
				const bool caretLeads = st.caret >= st.selectAnchor;
				int loLine, hiLine, colA, colB;
				if (st.boxMode) {
					loLine = (std::min)(st.boxAnchorLine, caretL); hiLine = (std::max)(st.boxAnchorLine, caretL);
					colA = (std::min)(st.boxAnchorCol, caretC);    colB = (std::max)(st.boxAnchorCol, caretC);
				} else {
					int selA = (std::min)(st.selectAnchor, st.caret), selB = (std::max)(st.selectAnchor, st.caret);
					caretToLineCol(starts, selA, loLine, colA);
					caretToLineCol(starts, selB, hiLine, colB);
				}
				for (int ln = (std::max)(firstLine, loLine); ln <= (std::min)(lastLine, hiLine); ++ln) {
					int ll = lineLength(starts, text, ln);
					int c0 = st.boxMode ? (std::min)(colA, ll) : ((ln == loLine) ? colA : 0);
					int c1 = st.boxMode ? (std::min)(colB, ll) : ((ln == hiLine) ? colB : ll);
					float x0 = caretXInLine(starts[ln], c0);
					float x1 = caretXInLine(starts[ln], c1);
					if (!st.boxMode && caretOnItsLine && ln == caretL) {
						if (caretLeads) x1 = drawnCaretX; else x0 = drawnCaretX;
					}
					float w = x1 - x0 + ((!st.boxMode && ln != hiLine) ? 4.0f : 0.0f); // newline sliver
					float y = contentTop + ln * lineH - st.scrollY;
					if (w > 0.0f) {
						addRect({ contentLeft + x0 - st.scrollX, y, w, lineH },
						        fadeTo(style.selection, selShown));
					}
				}
			}

			// One line of text: a single run in `style.text`, or — with a language set —
			// a chain of runs colored by the cached tokens. Tokens are sorted and
			// non-overlapping, so a line is walked by advancing through the ones that
			// intersect it; a token may start before the line (a multi-line string or
			// block comment), which the clamping below handles.
			const uint32_t plainColor = lang ? style.syntax.color(TokenKind::Plain) : style.text;
			auto drawLine = [&](int lineStart, int len, float x, float y) {
				const int lineEnd = lineStart + len;
				if (!lang) {
					addTextRange(x, y, text.data() + lineStart, len, plainColor);
					return;
				}
				auto it = std::lower_bound(st.tokens.begin(), st.tokens.end(), lineStart,
					[](const Token& t, int value) { return t.end <= value; });
				float penX = x;
				int i = lineStart;
				while (i < lineEnd) {
					if (it == st.tokens.end() || it->begin >= lineEnd) {
						penX = addTextRange(penX, y, text.data() + i, lineEnd - i, plainColor);
						break;
					}
					if (it->begin > i) { // plain gap before the next token
						penX = addTextRange(penX, y, text.data() + i, it->begin - i, plainColor);
						i = it->begin;
					}
					const int runEnd = (std::min)(lineEnd, it->end);
					penX = addTextRange(penX, y, text.data() + i, runEnd - i, style.syntax.color(it->kind));
					i = runEnd;
					if (it->end <= lineEnd) ++it; else break; // token continues on the next line
				}
			};

			for (int ln = firstLine; ln <= lastLine; ++ln) {
				float y = contentTop + ln * lineH - st.scrollY + mFont->ascent();
				drawLine(starts[ln], lineLength(starts, text, ln), contentLeft - st.scrollX, y);
			}
			// caret
			//
			// The blink was a square wave: solid for half a second, gone for half a
			// second, and the change between them instant. It still keeps that time; what
			// it no longer does is arrive and leave in one frame, which is the difference
			// between a cursor and something flashing at you.
			float caretAlpha = (std::fmod(st.blink, 1.0f) < 0.5f) ? 1.0f : 0.0f;
			if (style.motion.seconds > 0.0f) {
				// Clamped: the fade cannot be longer than the half-second it is a fade of,
				// or the caret would never reach either end and would only ever pulse.
				const float fade = (std::min)(style.motion.seconds, 0.2f);
				const float phase = std::fmod(st.blink, 1.0f);
				if (phase < 0.5f - fade)      caretAlpha = 1.0f;
				else if (phase < 0.5f)        caretAlpha = (0.5f - phase) / fade;
				else if (phase < 1.0f - fade) caretAlpha = 0.0f;
				else                          caretAlpha = (phase - (1.0f - fade)) / fade;
			}
			// The completion on offer, ahead of the caret.
			//
			// Drawn from the caret rather than from the end of the line, because that is
			// what it completes -- and one line only: a multi-line ghost would overdraw
			// whatever is below it, and reserving height for text that is not in the
			// value is a layout problem for a later day.
			if (focused && offering) {
				const float gx = contentLeft + drawnCaretX - st.scrollX;
				const float gy = contentTop + drawnCaretY - st.scrollY;
				std::string oneLine(extras->suggestion);
				if (const size_t brk = oneLine.find('\n'); brk != std::string::npos) {
					oneLine.resize(brk);
				}
				addText(gx, gy + mFont->ascent(), oneLine.c_str(), style.suggestion);
			}

			if (focused && caretAlpha > 0.004f) {
				float cx = contentLeft + drawnCaretX - st.scrollX;
				float cy = contentTop + drawnCaretY - st.scrollY;
				addRect({ cx, cy + 1.0f, style.caretWidth, lineH - 2.0f },
				        fadeTo(style.caret, caretAlpha));
			}
		}
		// Where the caret ended up, after everything that could have moved it. A
		// completion is a function of the prefix and the suffix, and this is the only
		// thing that says where the two meet.
		if (extras && extras->caret) *extras->caret = st.caret;

		popClip();

		// Line numbers in the gutter (clipped to it, scrolled vertically only).
		if (gutterW > 0.0f && mFont) {
			pushClip(glm::vec4(rect.x, rect.y, rect.x + gutterW, rect.y + rect.h));
			int firstLine = (std::max)(0, static_cast<int>(st.scrollY / lineH));
			int lastLine = (std::min)(lineCount - 1, firstLine + static_cast<int>(rect.h / lineH) + 1);
			for (int ln = firstLine; ln <= lastLine; ++ln) {
				// Formatted into a stack buffer: line numbers are redrawn every frame, and
				// std::to_string would construct a string per visible line.
				char number[16];
				int digits = std::snprintf(number, sizeof(number), "%d", ln + 1);
				float w = mFont->textWidth(number, number + digits);
				float y = contentTop + ln * lineH - st.scrollY + mFont->ascent();
				addTextRange(rect.x + gutterW - 6.0f - w, y, number, digits, style.lineNumber);
			}
			popClip();
		}

		// Scroll bar on the right strip. Asked for on every frame of a multiline field,
		// scrollable this frame or not, so that typing past the bottom fades one in
		// rather than making one appear.
		if (style.multiline) {
			const bool needed = maxScrollY > 0.0f;
			const float frac = needed ? (st.scrollY / maxScrollY) : 0.0f;
			const Rect real{ scrollTrack.x, scrollTrack.y + frac * trackRange,
			                 scrollTrack.w, thumbH };
			const bool thumbHot = (mActive == scrollId) ||
			                      (needed && real.contains(mInput.pointer));
			const ScrollBarLook bar = scrollBar(wid ^ kMotionKnob, needed, thumbHot,
			                                    thumbH, scrollTrack.h, style);
			if (bar.presence > 0.004f) {
				pushOpacity(bar.presence);
				addRect(scrollTrack, style.scrollTrack);
				const float span = (std::max)(0.0f, scrollTrack.h - bar.thumb);
				addRectRounded(Rect{ scrollTrack.x, scrollTrack.y + frac * span,
				                     scrollTrack.w, bar.thumb },
				               bar.colour, scrollTrack.w * 0.5f);
				popOpacity();
			}
		}

		st.blink += mInput.dt;
		return changed;
	}
}
