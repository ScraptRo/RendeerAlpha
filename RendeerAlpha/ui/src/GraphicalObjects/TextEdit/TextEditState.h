#pragma once
#include <GraphicalObjects/Gui.h>
#include <functional>
#include <string>
#include <vector>

// What one call of Gui::textField works out and its steps share.
//
// Gui::textField was one function of about 880 lines: the field's state, the keyboard, the
// pointer, the line layout, the drawing and what it tells a screen reader, all in one body
// over a few dozen locals and a handful of lambdas that closed over them. Those locals are
// this struct, and the steps (TextEdit.h) are free functions that take it. Private to
// src/GraphicalObjects/TextEdit/, which is why it is not under include/.
//
// The persistent half of a field -- caret, selection, scroll, undo -- is Gui::TextState,
// kept across frames by id; `st` is the one for this field. Everything else here lives for
// one call and is rebuilt every frame.
namespace RDA::TextEdit {

	// The one friend of Gui (see Gui.h). The steps are plain functions and cannot be befriended
	// one by one without listing every one of them in Gui.h, so what they need that Gui does
	// not already offer is reached here: this struct binds those few private members by
	// reference when the call begins, and forwards the draw-list primitives. A step touches
	// nothing of Gui's that is not named in this struct, which is the whole of the coupling.
	struct Frame {
		// Gui::TextState is private to Gui, and a friend may name it; the steps name it here.
		using TextState = Gui::TextState;

		Frame(Gui& gui, const char* id, std::string& text, const std::string& placeholder,
		      const Rect& rect, const TextFieldStyle& style, uint64_t version,
		      const Gui::TextFieldExtras* extras)
			: gui(gui), text(text), placeholder(placeholder), rect(rect), style(style),
			  version(version), extras(extras),
			  font(gui.mCanvas.font()), in(gui.mInput), active(gui.mInteraction.active), focusId(gui.mFocus.id),
			  focusClaimed(gui.mFocus.claimed), textEntry(gui.mFocus.textEntry),
			  textCaret(gui.mFocus.textCaret), getClipboard(gui.mGetClipboard),
			  setClipboard(gui.mSetClipboard),
			  wid(gui.scopedId(id)), st(gui.mTextStates[wid]),
			  offering(extras && extras->suggestion && *extras->suggestion),
			  lineH(font ? font->lineAdvance() : 16.0f),
			  inside(rect.contains(in.pointer)),
			  starts(st.lineStarts),
			  versioned(version != 0) {}

		Frame(const Frame&) = delete;
		Frame& operator=(const Frame&) = delete;

		// ---- what the caller handed in ------------------------------------------------
		Gui&                         gui;
		std::string&                 text;
		const std::string&           placeholder;
		const Rect&                  rect;
		const TextFieldStyle&        style;
		const uint64_t               version;
		const Gui::TextFieldExtras*  extras;

		// ---- Gui's private state, bound by reference ----------------------------------
		const FontAtlas*                         font;          // Gui::mCanvas.font()
		const GuiInput&                          in;            // Gui::mInput
		uint32_t&                                active;        // Gui::mInteraction.active
		uint32_t&                                focusId;       // Gui::mFocus.id
		bool&                                    focusClaimed;  // Gui::mFocus.claimed
		bool&                                    textEntry;     // Gui::mFocus.textEntry
		Rect&                                    textCaret;     // Gui::mFocus.textCaret
		const std::function<std::string()>&      getClipboard;  // Gui::mGetClipboard
		const std::function<void(const char*)>& setClipboard;  // Gui::mSetClipboard

		// ---- worked out when the call begins ------------------------------------------
		const uint32_t wid;
		TextState&     st;

		// A suggestion only exists while there is one to show. Everything below asks this
		// rather than the pointer, so a field handed an empty string behaves as a field
		// that was handed nothing.
		const bool offering;

		const float lineH;
		const bool  inside;

		// Gutter (Code line numbers) sits inside the field on the left. `starts` aliases
		// the per-field scratch buffer, so the repeated recomputes below reuse capacity.
		std::vector<int>& starts;

		const bool versioned;

		// ---- the text and its lines ---------------------------------------------------
		// Whether anything edited the text this call. Frame-scoped, so what the edits
		// report is still readable after they are done -- which is where the index is
		// brought back into step with the text, and what the field returns.
		bool textEdited = false;
		// Whether the keyboard did anything at all, edit or not: it keeps the caret solid.
		bool activity = false;
		int  lineCount = 0;

		// ---- the field's boxes ---------------------------------------------------------
		float gutterW = 0.0f;
		float scrollBarW = 0.0f; // right strip for the scroll bar
		float contentLeft = 0.0f;
		float contentTop = 0.0f;
		float contentRight = 0.0f;

		// ---- the keyboard focus --------------------------------------------------------
		bool focused = false;

		// ---- the caret: where it is, and where it is drawn on its way there -----------
		int   caretLine = 0;
		int   caretCol = 0;
		float caretX = 0.0f;
		float drawnCaretX = 0.0f;
		float drawnCaretY = 0.0f;

		// ---- scrolling -----------------------------------------------------------------
		float viewW = 0.0f;
		float viewH = 0.0f;
		float contentH = 0.0f;
		float maxScrollY = 0.0f;
		uint32_t scrollId = 0;
		Rect     scrollTrack{};
		float    thumbH = 0.0f;
		float    trackRange = 0.0f;

		// ---- drawing -------------------------------------------------------------------
		const Language* lang = nullptr;   // the grammar in force, or none
		int  firstLine = 0;               // the lines the field shows, first and last
		int  lastLine = 0;
		bool composing = false;           // an input method is part way through something

		// ---- the draw list, through Gui -------------------------------------------------
		void addRect(const Rect& r, uint32_t color) { gui.mCanvas.addRect(r, color); }
		void addRectRounded(const Rect& r, uint32_t color, float radius) {
			gui.mCanvas.addRectRounded(r, color, radius);
		}
		void addFrame(const Rect& r, uint32_t fill, uint32_t border, float borderWidth, float radius) {
			gui.mCanvas.addFrame(r, fill, border, borderWidth, radius);
		}
		void addText(float penX, float baselineY, const char* chars, uint32_t color,
		             TextStyle fontStyle = {}) {
			gui.mCanvas.addText(penX, baselineY, chars, color, fontStyle);
		}
		float addTextRange(float penX, float baselineY, const char* chars, int count,
		                   uint32_t color, TextStyle fontStyle = {}) {
			return gui.mCanvas.addTextRange(penX, baselineY, chars, count, color, fontStyle);
		}
		void pushClip(const glm::vec4& clip) { gui.mCanvas.pushClip(clip); }
		void popClip() { gui.mCanvas.popClip(); }
	};
}
