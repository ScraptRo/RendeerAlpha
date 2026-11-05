#include <GraphicalObjects/Gui.h>
#include "../TextEdit/TextEdit.h"
#include <cstdint>

// Gui::textField, as the order its steps run in. The steps are in TextEdit/.
namespace RDA {

	// ---- text field ---------------------------------------------------------------
	//
	// This used to be one function of about 880 lines. Its body is in
	// GraphicalObjects/TextEdit/ now, one file per concern, and what the steps share is a
	// TextEdit::Frame. What stays here is the order they run in -- which is the order ids,
	// focus and accessibility nodes are registered in and the draw list is filled in, so
	// it is not a thing to rearrange -- and the few lines that are about Gui itself.
	bool Gui::textField(const char* id, std::string& text, const std::string& placeholder, const Rect& rect, const TextFieldStyle& style,
	                    bool* outFocused, uint64_t version, const TextFieldExtras* extras) {
		TextEdit::Frame f(*this, id, text, placeholder, rect, style, version, extras);
		TextEdit::measure(f);

		// --- focus + mouse selection ---
		TextEdit::pointerPress(f);
		TextEdit::pointerDrag(f);
		// In the Tab order like everything else that takes the keyboard. It draws its own
		// caret, so the ring is the only thing this adds -- and a field reached by Tab
		// with no ring would be a field nobody can see they are in.
		focusable(f.wid, rect);
		f.focused = (mFocus.id == f.wid);
		if (outFocused) *outFocused = f.focused;
		// Read-only too: it still moves a caret and selects with the arrows, and still
		// copies with Ctrl+C.
		if (f.focused) mFocus.textFocus = true;

		// --- editing ---
		TextEdit::undoRedo(f);
		TextEdit::selectAndCopy(f);
		TextEdit::edit(f);
		TextEdit::settleCaret(f);

		// --- scrolling ---
		TextEdit::measureScroll(f);
		TextEdit::followCaret(f);
		TextEdit::wheel(f);
		TextEdit::scrollBarDrag(f);
		TextEdit::clampScroll(f);

		TextEdit::highlight(f);

		// --- render ---
		TextEdit::drawBackground(f);

		// Text (and caret) clipped to the content area, right of the gutter.
		mCanvas.pushClip(glm::vec4(f.contentLeft, rect.y, f.contentRight, rect.y + rect.h));
		if (mCanvas.font()) {
			TextEdit::drawPlaceholder(f);
			TextEdit::visibleLines(f);
			TextEdit::drawSelection(f);
			TextEdit::drawText(f);
			TextEdit::drawSuggestion(f);
			TextEdit::reportText(f);
			TextEdit::inputMethod(f);
			TextEdit::drawCaret(f);
		}
		// Where the caret ended up, after everything that could have moved it. A
		// completion is a function of the prefix and the suffix, and this is the only
		// thing that says where the two meet.
		if (extras && extras->caret) *extras->caret = f.st.caret;

		mCanvas.popClip();

		TextEdit::drawLineNumbers(f);
		TextEdit::drawScrollBar(f);

		f.st.blink += mInput.dt;
		return f.textEdited;
	}
}
