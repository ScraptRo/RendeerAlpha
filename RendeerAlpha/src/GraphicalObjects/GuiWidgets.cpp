#include <GraphicalObjects/Gui.h>
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
		if (clicked) value = !value;

		float box = rect.h;
		Rect boxRect{ rect.x, rect.y, box, box };
		addFrame(boxRect, (mHot == wid) ? s.boxHover : s.box, s.border, s.borderWidth, s.radius);
		if (value) {
			float inset = box * s.checkInset;
			addRectRounded({ rect.x + inset, rect.y + inset, box - 2 * inset, box - 2 * inset },
			               s.check, (std::max)(0.0f, s.radius - inset));
		}
		if (mFont && label && label[0]) {
			float baseline = rect.y + rect.h * 0.5f + mFont->ascent() * 0.35f;
			addText(rect.x + box + 8.0f, baseline, label, s.label);
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

		float t = (maxValue > minValue) ? (value - minValue) / (maxValue - minValue) : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		addRectRounded(rect, s.track, s.radius);                                  // track
		addRectRounded({ rect.x, rect.y, rect.w * t, rect.h }, s.fill, s.radius); // fill
		float knob = s.knobWidth;
		addRectRounded({ rect.x + rect.w * t - knob * 0.5f, rect.y - 2.0f, knob, rect.h + 4.0f },
		               (mActive == wid) ? s.knobActive : s.knob, s.radius);
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
		bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
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

	bool Gui::textField(const char* id, std::string& text, const Rect& rect, const TextFieldStyle& style,
	                    bool* outFocused) {
		uint32_t wid = scopedId(id);
		TextState& st = mTextStates[wid];

		const float lineH = mFont ? mFont->lineAdvance() : 16.0f;
		bool inside = rect.contains(mInput.pointer);

		// Gutter (Code line numbers) sits inside the field on the left. `starts` aliases
		// the per-field scratch buffer, so the repeated recomputes below reuse capacity.
		std::vector<int>& starts = st.lineStarts;
		computeLineStarts(text, starts);
		int lineCount = static_cast<int>(starts.size());

		float gutterW = 0.0f;
		if (style.showLineNumbers && mFont) {
			int digits = 1; for (int n = lineCount; n >= 10; n /= 10) ++digits;
			gutterW = mFont->advance('0') * (std::max)(digits, 2) + 12.0f;
		}
		const float scrollBarW = style.multiline ? 12.0f : 0.0f; // right strip for the scroll bar
		float contentLeft = rect.x + gutterW + style.padding;
		float contentTop = rect.y + style.padding;
		float contentRight = rect.x + rect.w - style.padding - scrollBarW;

		// Helper: caret X within its line (pixels from contentLeft, pre-scroll).
		auto caretXInLine = [&](int lineStart, int col) {
			float x = 0.0f;
			for (int i = 0; i < col; ++i) x += mFont ? mFont->advance(text[lineStart + i]) : 0.0f;
			return x;
		};

		// Map a pointer position to a (line, col) within the text.
		auto lineColAt = [&](glm::vec2 p, int& outLine, int& outCol) {
			int line = 0;
			if (style.multiline) {
				line = static_cast<int>((p.y - contentTop + st.scrollY) / lineH);
				line = std::clamp(line, 0, lineCount - 1);
			}
			int len = lineLength(starts, text, line);
			float targetX = p.x - contentLeft + st.scrollX;
			int col = 0; float acc = 0.0f; float best = std::fabs(targetX);
			for (int i = 0; i < len; ++i) {
				acc += mFont ? mFont->advance(text[starts[line] + i]) : 0.0f;
				if (std::fabs(acc - targetX) < best) { best = std::fabs(acc - targetX); col = i + 1; }
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
		} else if (mInput.pressed && inside) {
			mFocused = wid; mFocusClaimed = true; // press on the scroll-bar strip: keep focus
		} else if (mFocused == wid) {
			mFocusClaimed = true;
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
		bool focused = (mFocused == wid);
		if (outFocused) *outFocused = focused;

		// --- editing ---
		bool changed = false;
		bool activity = false;
		if (focused && !style.readOnly) {
			// Typed text replaces any selection.
			if (!mInput.typed.empty()) {
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
					if (shift) { if (st.selectAnchor < 0 && !st.boxMode) st.selectAnchor = st.caret; }
					else { st.selectAnchor = -1; st.boxMode = false; }
				};
				switch (key) {
				case GuiEditKey::Backspace:
					if (hasLinearSel() || st.boxMode) deleteSelection();
					else if (st.caret > 0) { text.erase(text.begin() + st.caret - 1); st.caret--; }
					changed = true; break;
				case GuiEditKey::Delete:
					if (hasLinearSel() || st.boxMode) deleteSelection();
					else if (st.caret < size) text.erase(text.begin() + st.caret);
					changed = true; break;
				case GuiEditKey::Left:
					if (!shift && !ctrl && hasLinearSel()) { st.caret = (std::min)(st.selectAnchor, st.caret); st.selectAnchor = -1; }
					else { beginMove(); st.caret = ctrl ? wordLeft(text, st.caret) : (std::max)(0, st.caret - 1); }
					break;
				case GuiEditKey::Right:
					if (!shift && !ctrl && hasLinearSel()) { st.caret = (std::max)(st.selectAnchor, st.caret); st.selectAnchor = -1; }
					else { beginMove(); st.caret = ctrl ? wordRight(text, st.caret) : (std::min)(size, st.caret + 1); }
					break;
				case GuiEditKey::Home: beginMove(); st.caret = starts[line]; break;
				case GuiEditKey::End:  beginMove(); st.caret = starts[line] + lineLength(starts, text, line); break;
				case GuiEditKey::Up:
					if (style.multiline && line > 0) { beginMove(); int len = lineLength(starts, text, line - 1); st.caret = starts[line - 1] + (std::min)(col, len); }
					break;
				case GuiEditKey::Down:
					if (style.multiline && line + 1 < lineCount) { beginMove(); int len = lineLength(starts, text, line + 1); st.caret = starts[line + 1] + (std::min)(col, len); }
					break;
				case GuiEditKey::Enter:
					// Ctrl+Enter is a submit gesture, not a newline: the application reads
					// GuiInput::submit and decides what it means (run the cell, commit the
					// value). Without this the field would swallow it as a line break.
					if (ctrl) break;
					if (style.multiline) { deleteSelection(); text.insert(text.begin() + st.caret, '\n'); st.caret++; changed = true; }
					else { mFocused = 0; }
					break;
				case GuiEditKey::Tab:
					if (style.mode == TextFieldMode::Code) { deleteSelection(); for (int i = 0; i < 4; ++i) { text.insert(text.begin() + st.caret, ' '); st.caret++; } changed = true; }
					break;
				}
				activity = true;
			}

			// Clipboard + select-all.
			if (mInput.selectAll) { st.boxMode = false; st.selectAnchor = 0; st.caret = static_cast<int>(text.size()); activity = true; }
			if (mInput.copy || mInput.cut) {
				std::string sel = selectionText();
				if (!sel.empty() && mSetClipboard) mSetClipboard(sel.c_str());
				if (mInput.cut && !sel.empty()) { deleteSelection(); changed = true; }
				activity = true;
			}
			if (mInput.paste && mGetClipboard) {
				std::string clip = mGetClipboard();
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

		// Recompute line layout after edits and clamp caret + selection anchor.
		computeLineStarts(text, starts);
		lineCount = static_cast<int>(starts.size());
		st.caret = std::clamp(st.caret, 0, static_cast<int>(text.size()));
		if (st.selectAnchor > static_cast<int>(text.size())) st.selectAnchor = static_cast<int>(text.size());
		int caretLine, caretCol; caretToLineCol(starts, st.caret, caretLine, caretCol);
		float caretX = caretXInLine(starts[caretLine], caretCol);

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
		// leaves the wheel alone so an enclosing ScrollView picks it up instead. Without
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
		const Language* lang = style.language.empty() ? nullptr : mSyntax.find(style.language);
		if (lang) {
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
		addFrame(rect, style.background, style.border, style.borderWidth, style.radius);
		if (gutterW > 0.0f) addRect({ rect.x, rect.y, gutterW, rect.h }, style.gutter);
		if (style.highlightCurrentLine && focused) {
			float y = contentTop + caretLine * lineH - st.scrollY;
			addRect({ contentLeft - 4.0f, y, viewW + 8.0f, lineH }, style.currentLine);
		}

		// Text (and caret) clipped to the content area, right of the gutter.
		pushClip(glm::vec4(contentLeft, rect.y, contentRight, rect.y + rect.h));
		if (mFont) {
			int firstLine = style.multiline ? (std::max)(0, static_cast<int>(st.scrollY / lineH)) : 0;
			int lastLine = style.multiline
				? (std::min)(lineCount - 1, firstLine + static_cast<int>((rect.h) / lineH) + 1)
				: 0;

			// Selection highlight, drawn under the text.
			if (st.boxMode || hasLinearSel()) {
				int caretL, caretC; caretToLineCol(starts, st.caret, caretL, caretC);
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
					float w = x1 - x0 + ((!st.boxMode && ln != hiLine) ? 4.0f : 0.0f); // newline sliver
					float y = contentTop + ln * lineH - st.scrollY;
					if (w > 0.0f) addRect({ contentLeft + x0 - st.scrollX, y, w, lineH }, style.selection);
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
			bool caretOn = focused && std::fmod(st.blink, 1.0f) < 0.5f;
			if (caretOn) {
				float cx = contentLeft + caretX - st.scrollX;
				float cy = contentTop + caretLine * lineH - st.scrollY;
				addRect({ cx, cy + 1.0f, style.caretWidth, lineH - 2.0f }, style.caret);
			}
		}
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
				float w = 0.0f;
				for (int i = 0; i < digits; ++i) w += mFont->advance(number[i]);
				float y = contentTop + ln * lineH - st.scrollY + mFont->ascent();
				addTextRange(rect.x + gutterW - 6.0f - w, y, number, digits, style.lineNumber);
			}
			popClip();
		}

		// Scroll bar on the right strip.
		if (style.multiline && maxScrollY > 0.0f) {
			addRect(scrollTrack, style.scrollTrack);
			float thumbY = scrollTrack.y + (st.scrollY / maxScrollY) * trackRange;
			Rect thumb{ scrollTrack.x, thumbY, scrollTrack.w, thumbH };
			bool thumbHot = (mActive == scrollId) || thumb.contains(mInput.pointer);
			addRectRounded(thumb, thumbHot ? style.scrollThumbHover : style.scrollThumb,
			               scrollTrack.w * 0.5f);
		}

		st.blink += mInput.dt;
		return changed;
	}
}
