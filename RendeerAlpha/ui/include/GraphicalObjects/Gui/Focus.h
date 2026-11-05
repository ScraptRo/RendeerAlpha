#pragma once
#include <cstdint>
#include <vector>
#include <GraphicalObjects/GuiTheme.h>
#include <GraphicalObjects/GuiTypes.h>

namespace RDA {

	class Canvas;

	// Where the keyboard is: the widget that has it, the order Tab walks, and the ring drawn
	// around the one that has it.
	//
	// A widget that does something when pressed registers as it draws, and the order it does
	// that in is the Tab order -- so nothing declares a tab index and nothing can disagree with
	// what the reader sees. This class keeps the list for the frame, resolves a Tab at the end of
	// it (once everything has had its turn to register), and draws the ring on top.
	//
	// Owned by Gui. Public so that Gui.h can hold one by value. What widgets read and write as
	// they draw is public; the list and the Tab bookkeeping are not.
	class Focus {
	public:
		// ---- who has the keyboard ----------------------------------------------------------
		uint32_t id = 0;        // id of the keyboard-focused widget, 0 = none
		// A widget took *this frame's press*. Not "something is focused" -- that is
		// `id`, and conflating the two is what made a click on empty space unable to
		// take the keyboard away: every focused widget re-registers every frame, so a
		// claim that meant "I still have it" was always true by the time the press was
		// resolved, and the drop below it could never run.
		bool claimed = false;
		// Whether the keyboard is what put the focus where it is -- Tab, and nothing
		// else. It is the whole of the default rule for drawing a focus ring: somebody
		// navigating by Tab has nothing else telling them where they are, and somebody
		// who just clicked is looking straight at the answer. Survives across frames,
		// because focus does.
		bool byKeyboard = false;
		// The widget that drew a ring in its own shape this frame, if any. A mesh-drawn
		// widget can do that and a rectangle around it would be the wrong shape, so
		// drawRing leaves this one alone.
		uint32_t shapedRing = 0;

		// Puts the keyboard somewhere, or nowhere with 0, the way a press does: no ring.
		void set(uint32_t wid) {
			id = wid;
			claimed = true;
			byKeyboard = false;
		}
		// The way Tab does: ring and all.
		void setByKeyboard(uint32_t wid) {
			id = wid;
			claimed = true;
			byKeyboard = true;
		}

		// ---- what the focused widget is -------------------------------------------------------
		// A focused text field drew this frame, so plain keys are typing rather than shortcuts.
		bool textFocus = false;
		bool textEntry = false;   // a field, specifically, had the keyboard
		Rect textCaret{};

		// Said by a focused widget that pages -- a grid, a tree -- so Home, End, Page Up and
		// Page Down are its keys this frame rather than a shortcut's. `selects` is a widget
		// that chooses several things. Cleared once shortcuts have been dispatched.
		void focusPages(bool selects) { mPages = true; mSelects = selects; }
		bool pagesKept() const { return mPages; }
		bool selectsKept() const { return mSelects; }
		void releasePages() { mPages = false; mSelects = false; }

		// ---- the Tab order -----------------------------------------------------------------------
		// Everything that offered to take the keyboard this frame, in draw order, and where each
		// of them was. Cleared every frame: what is focusable is whatever is on screen, so a
		// widget that scrolled out of view leaves the order with it.
		struct Focusable { uint32_t id; Rect rect; Variant ring; };

		// Joins the Tab order and gets a ring; says nothing about the pointer. Returns whether
		// this is the one that has the keyboard.
		bool focusable(uint32_t wid, const Rect& rect, Variant ringVariant);
		// Whether the focused widget was asked to act this frame: Enter or Space.
		bool activated(uint32_t wid, const GuiInput& input) const;
		// An arrow key, for a widget that has somewhere to move: -1 or +1, 0 for neither.
		int step(uint32_t wid, const GuiInput& input) const;
		// Escape, for something that can be dismissed.
		bool escaped(uint32_t wid, const GuiInput& input) const;
		// Said by a widget that used Tab for something of its own -- a code field indenting --
		// so the walk leaves it alone this frame.
		void consumeMove() { mMoveConsumed = true; }

		// ---- the frame -------------------------------------------------------------------------
		// Every frame, walked or not: the list starts empty, and a Tab in `input` is noted.
		void beginFrame(const GuiInput& input);
		// The ring, as geometry like any other: drawn after the last widget, before the draw
		// list's last command is closed.
		void drawRing(Canvas& canvas, const Theme& theme) const;
		// Once everything has had its turn: a press that landed on nothing focusable drops the
		// keyboard, and Tab moves it on.
		void endFrame(bool pressed);

	private:
		std::vector<Focusable> mFocusables;
		// Set when Tab is seen and cleared when it is spent. Resolved at the end of the
		// frame rather than where it is noticed, because the widget after this one has
		// not drawn yet and the order is the whole answer.
		int  mMove = 0;
		bool mMoveConsumed = false;
		bool mPages = false;
		bool mSelects = false;
	};
}
