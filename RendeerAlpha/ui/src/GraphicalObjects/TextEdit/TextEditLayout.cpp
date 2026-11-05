#include "TextEdit.h"
#include <Core/Utf8.h>
#include <algorithm>
#include <cmath>

// Where things are in a text field: its lines, its boxes, the caret and how far it is
// scrolled. Nothing here draws or reads a key.
namespace RDA::TextEdit {

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

	void measure(Frame& f) {
		// Rebuilding this walks the whole string, and it used to happen twice per frame
		// regardless of whether anything had changed -- on a large document that dwarfs
		// the cost of drawing the forty lines actually on screen. A caller that can say
		// its text is unchanged keeps the index instead.
		if (!f.versioned || f.version != f.st.indexedVersion || f.text.size() != f.st.indexedSize) {
			computeLineStarts(f.text, f.starts);
			f.st.indexedVersion = f.version;
			f.st.indexedSize = f.text.size();
		}
		f.lineCount = static_cast<int>(f.starts.size());

		f.gutterW = 0.0f;
		if (f.style.showLineNumbers && f.font) {
			int digits = 1; for (int n = f.lineCount; n >= 10; n /= 10) ++digits;
			f.gutterW = f.font->advance(U'0') * (std::max)(digits, 2) + 12.0f;
		}
		f.scrollBarW = f.style.multiline ? 12.0f : 0.0f; // right strip for the scroll bar
		f.contentLeft = f.rect.x + f.gutterW + f.style.padding;
		f.contentTop = f.rect.y + f.style.padding;
		f.contentRight = f.rect.x + f.rect.w - f.style.padding - f.scrollBarW;
	}

	// Helper: caret X within its line (pixels from contentLeft, pre-scroll).
	//
	// A column is a byte offset -- which is what the rest of this field indexes with
	// -- but its width is the width of the characters those bytes spell, so this asks
	// the atlas to measure the range rather than summing byte by byte.
	float caretXInLine(Frame& f, int lineStart, int col) {
		if (!f.font) return 0.0f;
		return f.font->textWidth(f.text.data() + lineStart, f.text.data() + lineStart + col);
	}

	// Map a pointer position to a (line, col) within the text. The column it returns
	// is always on a character boundary, because it only ever moves by whole ones.
	void lineColAt(Frame& f, glm::vec2 p, int& outLine, int& outCol) {
		int line = 0;
		if (f.style.multiline) {
			line = static_cast<int>((p.y - f.contentTop + f.st.scrollY) / f.lineH);
			line = std::clamp(line, 0, f.lineCount - 1);
		}
		int len = lineLength(f.starts, f.text, line);
		float targetX = p.x - f.contentLeft + f.st.scrollX;
		const char* lineBegin = f.text.data() + f.starts[line];
		const char* lineEnd = lineBegin + len;
		int col = 0; float acc = 0.0f; float best = std::fabs(targetX);
		int i = 0;
		while (i < len) {
			uint32_t cp = 0;
			i += static_cast<int>(Utf8::decode(lineBegin + i, lineEnd, cp));
			acc += f.font ? f.font->advance(cp) : 0.0f;
			if (std::fabs(acc - targetX) < best) { best = std::fabs(acc - targetX); col = i; }
		}
		outLine = line; outCol = col;
	}

	bool hasLinearSel(const Frame& f) {
		return !f.st.boxMode && f.st.selectAnchor >= 0 && f.st.selectAnchor != f.st.caret;
	}

	void settleCaret(Frame& f) {
		// Bring the line layout back into step, but only if something actually edited the
		// text: this is the second of the two full scans that used to run every frame.
		if (f.textEdited) {
			computeLineStarts(f.text, f.starts);
			f.st.indexedSize = f.text.size();
		}
		f.lineCount = static_cast<int>(f.starts.size());
		f.st.caret = std::clamp(f.st.caret, 0, static_cast<int>(f.text.size()));
		if (f.st.selectAnchor > static_cast<int>(f.text.size())) f.st.selectAnchor = static_cast<int>(f.text.size());
		// A caret is a byte offset, and the things that move it without decoding -- Up and
		// Down keeping a column, a box selection keeping a rectangle, the clamp above
		// against a string that just got shorter -- can land it inside a character.
		// Snapping here, in the one place they all pass through, is what lets everything
		// else go on treating it as a plain index.
		f.st.caret = static_cast<int>(Utf8::floorBoundary(f.text, static_cast<size_t>(f.st.caret)));
		if (f.st.selectAnchor > 0) {
			f.st.selectAnchor = static_cast<int>(Utf8::floorBoundary(f.text, static_cast<size_t>(f.st.selectAnchor)));
		}
		caretToLineCol(f.starts, f.st.caret, f.caretLine, f.caretCol);
		f.caretX = caretXInLine(f, f.starts[f.caretLine], f.caretCol);

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
		f.drawnCaretX = f.caretX;
		f.drawnCaretY = static_cast<float>(f.caretLine) * f.lineH;
		if (f.style.motion.seconds > 0.0f) {
			const uint32_t caretKey = f.wid ^ kMotionPlace;
			if (f.active == f.wid) {
				f.gui.motion().reset(caretKey + 0u, f.drawnCaretX);
				f.gui.motion().reset(caretKey + 1u, f.drawnCaretY);
			} else {
				f.drawnCaretX = f.gui.motion().value(caretKey + 0u, f.drawnCaretX,
				                                     f.style.motion.seconds, f.style.motion.curve);
				f.drawnCaretY = f.gui.motion().value(caretKey + 1u, f.drawnCaretY,
				                                     f.style.motion.seconds, f.style.motion.curve);
			}
		}
	}

	void measureScroll(Frame& f) {
		f.viewW = f.contentRight - f.contentLeft;
		f.viewH = f.rect.h - 2.0f * f.style.padding;
		f.contentH = f.lineCount * f.lineH;
		f.maxScrollY = f.style.multiline ? (std::max)(0.0f, f.contentH - f.viewH) : 0.0f;
	}

	void followCaret(Frame& f) {
		// Keep the caret visible, but only while it is actually being moved, so the wheel
		// and scroll bar aren't fought when the user is just reading.
		bool caretActive = f.focused && (!f.in.typed.empty() || !f.in.editKeys.empty() || (f.in.lPressed && f.inside) || f.active == f.wid);
		if (caretActive) {
			if (f.caretX - f.st.scrollX > f.viewW) f.st.scrollX = f.caretX - f.viewW;
			if (f.caretX - f.st.scrollX < 0.0f)  f.st.scrollX = f.caretX;
			if (f.style.multiline) {
				float caretTop = f.caretLine * f.lineH;
				if (caretTop - f.st.scrollY > f.viewH - f.lineH) f.st.scrollY = caretTop - (f.viewH - f.lineH);
				if (caretTop - f.st.scrollY < 0.0f)          f.st.scrollY = caretTop;
			}
		}
	}

	void clampScroll(Frame& f) {
		f.st.scrollX = (std::max)(0.0f, f.st.scrollX);
		f.st.scrollY = std::clamp(f.st.scrollY, 0.0f, f.maxScrollY);
	}

	void visibleLines(Frame& f) {
		f.firstLine = f.style.multiline ? (std::max)(0, static_cast<int>(f.st.scrollY / f.lineH)) : 0;
		f.lastLine = f.style.multiline
			? (std::min)(f.lineCount - 1, f.firstLine + static_cast<int>((f.rect.h) / f.lineH) + 1)
			: 0;
	}
}
