#include "TextEdit.h"
#include <algorithm>
#include <cctype>

// What the mouse does to a text field: pressing, dragging a selection, the wheel and the
// scroll bar's thumb.
namespace RDA::TextEdit {

	using TextState = Frame::TextState;

	namespace {
		// Double click selects the word under the pointer. Clicking in the run
		// of spaces between words selects that run, so the gesture always
		// selects *something* rather than collapsing.
		void selectWordAt(Frame& f, int idx) {
			f.st.boxMode = false;
			const int size = static_cast<int>(f.text.size());
			auto isSpace = [&](int i) {
				return std::isspace(static_cast<unsigned char>(f.text[i])) != 0;
			};
			if (idx < size && !isSpace(idx) && isWordChar(f.text[idx])) {
				int begin = idx, end = idx;
				while (begin > 0 && isWordChar(f.text[begin - 1])) --begin;
				while (end < size && isWordChar(f.text[end])) ++end;
				f.st.selectAnchor = begin; f.st.caret = end;
			} else if (idx < size && isSpace(idx) && f.text[idx] != '\n') {
				int begin = idx, end = idx;
				while (begin > 0 && isSpace(begin - 1) && f.text[begin - 1] != '\n') --begin;
				while (end < size && isSpace(end) && f.text[end] != '\n') ++end;
				f.st.selectAnchor = begin; f.st.caret = end;
			} else {
				// Punctuation, or the end of a line: fall back to a plain caret.
				f.st.selectAnchor = idx; f.st.caret = idx;
			}
		}
	}

	void pointerPress(Frame& f) {
		bool inTextArea = f.in.pointer.x < f.rect.x + f.rect.w - f.scrollBarW;
		if (f.in.lPressed && f.inside && inTextArea) {
			f.focusId = f.wid; f.focusClaimed = true; f.active = f.wid; // begin a drag
			if (f.font) {
				int line, col; lineColAt(f, f.in.pointer, line, col);
				int idx = f.starts[line] + col;
				// A word- or line-selecting click must not be undone by the drag handler
				// below, which would collapse it on the slightest mouse movement.
				f.st.dragGranularity = (f.in.clickCount >= 2 && !f.in.alt && !f.in.shift)
					? f.in.clickCount : 1;

				if (f.in.alt && f.style.multiline) {
					f.st.boxMode = true; f.st.boxAnchorLine = line; f.st.boxAnchorCol = col; f.st.selectAnchor = -1;
					f.st.caret = idx;
				} else if (f.in.shift) {
					f.st.boxMode = false;
					if (f.st.selectAnchor < 0) f.st.selectAnchor = f.st.caret; // extend from the old caret
					f.st.caret = idx;
				} else if (f.in.clickCount == 2) {
					selectWordAt(f, idx);
				} else if (f.in.clickCount >= 3) {
					// Triple click takes the whole line, newline excluded.
					f.st.boxMode = false;
					f.st.selectAnchor = f.starts[line];
					f.st.caret = f.starts[line] + lineLength(f.starts, f.text, line);
				} else {
					f.st.boxMode = false; f.st.selectAnchor = idx; // collapses if the drag doesn't move
					f.st.caret = idx;
				}
			}
			f.st.blink = 0.0f;
			// Putting the caret somewhere by hand ends the run of edits: typing a word,
			// clicking elsewhere and typing another should be two undos, not one.
			f.st.undoKind = TextState::EditKind::None;
		} else if (f.in.lPressed && f.inside) {
			f.focusId = f.wid; f.focusClaimed = true; // press on the scroll-bar strip: keep focus
		}
	}

	void pointerDrag(Frame& f) {
		// Drag to extend the selection (linear or box, per how it began).
		if (f.active == f.wid) {
			f.focusClaimed = true;
			// Only a plain click drags the caret; a word or line selection stays as the
			// click made it until the button is released.
			if (f.st.dragGranularity == 1 && f.in.lDown && !f.in.lPressed && f.font) {
				int line, col; lineColAt(f, f.in.pointer, line, col);
				f.st.caret = f.starts[line] + col;
				f.st.blink = 0.0f;
			}
			if (f.in.lReleased) f.active = 0;
		}
	}

	void wheel(Frame& f) {
		// Mouse wheel over the field scrolls it — but only while it has somewhere to go.
		// A field with no overflow, or one already at the end it is being pushed towards,
		// leaves the wheel alone so an enclosing Scroll picks it up instead. Without
		// that, a short cell inside a long notebook would swallow the gesture.
		if (f.style.multiline && f.inside && f.in.scroll != 0.0f && !f.gui.scrollConsumed() &&
		    f.maxScrollY > 0.0f &&
		    !(f.in.scroll > 0.0f && f.st.scrollY <= 0.0f) &&
		    !(f.in.scroll < 0.0f && f.st.scrollY >= f.maxScrollY)) {
			f.st.scrollY -= f.in.scroll * f.lineH * 3.0f;
			f.gui.consumeScroll();
		}
	}

	void scrollBarDrag(Frame& f) {
		// Scroll-bar thumb drag.
		f.scrollId = f.wid ^ 0x5c011ba7u;
		f.scrollTrack = Rect{ f.rect.x + f.rect.w - f.scrollBarW + 1.0f, f.contentTop, f.scrollBarW - 2.0f, f.viewH };
		f.thumbH = 0.0f; f.trackRange = 0.0f;
		if (f.style.multiline && f.maxScrollY > 0.0f) {
			f.thumbH = (std::max)(24.0f, f.viewH * (f.viewH / f.contentH));
			f.trackRange = f.scrollTrack.h - f.thumbH;
			if (f.scrollTrack.contains(f.in.pointer) && f.in.lPressed) {
				float thumbY = f.scrollTrack.y + (f.st.scrollY / f.maxScrollY) * f.trackRange;
				f.st.scrollGrab = (f.in.pointer.y < thumbY || f.in.pointer.y > thumbY + f.thumbH)
					? f.thumbH * 0.5f : f.in.pointer.y - thumbY;
				f.active = f.scrollId;
				f.focusClaimed = true;
			}
			if (f.active == f.scrollId) {
				float ny = std::clamp(f.in.pointer.y - f.st.scrollGrab, f.scrollTrack.y, f.scrollTrack.y + f.trackRange);
				f.st.scrollY = (f.trackRange > 0.0f) ? ((ny - f.scrollTrack.y) / f.trackRange) * f.maxScrollY : 0.0f;
				if (f.in.lReleased) f.active = 0;
			}
		}
	}
}
