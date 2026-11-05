#pragma once
#include "TextEditState.h"
#include <cctype>

// The steps of Gui::textField, in the order it runs them. See TextEditState.h for what they
// share, and Gui::textField (Gui/GuiTextField.cpp) for why the order is the order it is: the
// immediate-mode GUI keys on the order ids, focus and accessibility nodes are registered
// in, and the draw list on the order things are drawn, so neither is a thing to reorder.
//
// One file per concern, in ui/src/GraphicalObjects/TextEdit/.
namespace RDA::TextEdit {

	// ---- text geometry (TextEditLayout.cpp) ----------------------------------------------

	// Start index of each line in `s` (split on '\n'); always at least one line.
	void computeLineStarts(const std::string& s, std::vector<int>& starts);
	void caretToLineCol(const std::vector<int>& starts, int caret, int& line, int& col);
	inline int lineLength(const std::vector<int>& starts, const std::string& s, int line) {
		int start = starts[line];
		int end = (line + 1 < static_cast<int>(starts.size())) ? starts[line + 1] - 1
		                                                       : static_cast<int>(s.size());
		return end - start;
	}
	// A byte at 0x80 or above is part of a UTF-8 sequence, and every codepoint this
	// engine draws above ASCII is a letter or a mark on one. Counting them as word
	// characters is what stops Ctrl+Left from halting in the middle of "café", and it
	// needs no decoding: a continuation byte is as much part of the word as its lead.
	inline bool isWordChar(char c) {
		const unsigned char b = static_cast<unsigned char>(c);
		return b >= 0x80 || std::isalnum(b) != 0 || b == '_';
	}

	// The line index, the gutter and the field's boxes: the first thing a call does.
	void measure(Frame& f);
	// Caret X within its line (pixels from contentLeft, pre-scroll).
	float caretXInLine(Frame& f, int lineStart, int col);
	// Map a pointer position to a (line, col) within the text.
	void  lineColAt(Frame& f, glm::vec2 p, int& outLine, int& outCol);
	bool  hasLinearSel(const Frame& f);
	// After the edits: the line index back in step, the caret on a character boundary, and
	// where it is and where it is drawn.
	void settleCaret(Frame& f);
	// Scrolling: how much there is to scroll, following the caret, and the limits.
	void measureScroll(Frame& f);
	void followCaret(Frame& f);
	void clampScroll(Frame& f);
	// The lines the field shows.
	void visibleLines(Frame& f);

	// ---- the pointer (TextEditPointer.cpp) -----------------------------------------------

	void pointerPress(Frame& f);
	void pointerDrag(Frame& f);
	void wheel(Frame& f);
	void scrollBarDrag(Frame& f);

	// ---- the keyboard and the input method (TextEditInput.cpp) ---------------------------

	void undoRedo(Frame& f);
	void selectAndCopy(Frame& f);
	void edit(Frame& f);
	void inputMethod(Frame& f);

	// ---- drawing (TextEditRender.cpp) ----------------------------------------------------

	void highlight(Frame& f);
	void drawBackground(Frame& f);
	void drawPlaceholder(Frame& f);
	void drawSelection(Frame& f);
	void drawText(Frame& f);
	void drawSuggestion(Frame& f);
	void drawCaret(Frame& f);
	void drawLineNumbers(Frame& f);
	void drawScrollBar(Frame& f);

	// ---- the accessibility collector (TextEditAccess.cpp) --------------------------------

	void reportText(Frame& f);
}
