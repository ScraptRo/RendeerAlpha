#include <GraphicalObjects/Gui/Focus.h>
#include <GraphicalObjects/Gui/Canvas.h>
#include <algorithm>
#include <cstddef>

// Focus: where the keyboard is, who may be next, and the ring drawn around the one that has it.
namespace RDA {

	bool Focus::focusable(uint32_t wid, const Rect& rect, Variant ringVariant) {
		// Registering is how a widget joins the Tab order and gets a ring; it says
		// nothing about the pointer. Claiming the press here would mean a widget that
		// merely *has* focus keeps it no matter where the click landed, which is the
		// opposite of what a click on empty space should do.
		mFocusables.push_back(Focusable{ wid, rect, ringVariant });
		return id == wid;
	}

	bool Focus::activated(uint32_t wid, const GuiInput& input) const {
		if (id != wid) return false;
		for (const GuiEditKey key : input.editKeys) {
			if (key == GuiEditKey::Enter || key == GuiEditKey::Space) return true;
		}
		return false;
	}

	int Focus::step(uint32_t wid, const GuiInput& input) const {
		if (id != wid) return 0;
		for (const GuiEditKey key : input.editKeys) {
			if (key == GuiEditKey::Left || key == GuiEditKey::Up) return -1;
			if (key == GuiEditKey::Right || key == GuiEditKey::Down) return 1;
		}
		return 0;
	}

	bool Focus::escaped(uint32_t wid, const GuiInput& input) const {
		if (id != wid) return false;
		for (const GuiEditKey key : input.editKeys) {
			if (key == GuiEditKey::Escape) return true;
		}
		return false;
	}

	// ---- the frame ----------------------------------------------------------------
	void Focus::beginFrame(const GuiInput& input) {
		claimed = false;
		mFocusables.clear();
		shapedRing = 0;
		textFocus = false;
		textEntry = false;
		// Tab moves the keyboard on. Read here, spent at the end of the frame: a text
		// field in code mode inserts spaces instead and says so by consuming it.
		mMove = 0;
		mMoveConsumed = false;
		for (const GuiEditKey key : input.editKeys) {
			if (key == GuiEditKey::Tab) mMove = input.shift ? -1 : 1;
		}
	}

	void Focus::drawRing(Canvas& canvas, const Theme& theme) const {
		for (const Focusable& f : mFocusables) {
			if (f.id != id) continue;
			// Unless it drew one of its own, in its own shape. A mesh-drawn widget does
			// that while it paints -- underneath itself, which is the only place an
			// outline that follows a silhouette can go. See Gui::button.
			if (f.id == shapedRing) break;
			const FocusStyle& ring = theme.focus(f.ring);
			if (ring.width <= 0.0f) break;
			// Only where the keyboard is what put it there, unless the theme says
			// otherwise. A ring around the thing you just clicked is a second answer to
			// a question you had already answered.
			if (ring.show == FocusShow::Keyboard && !byKeyboard) break;
			const Rect r{ f.rect.x + ring.inset, f.rect.y + ring.inset,
			              f.rect.w - ring.inset * 2.0f, f.rect.h - ring.inset * 2.0f };
			// Four edges rather than addFrame: that draws the border as a full rect and
			// lays the fill on top, so a ring with nothing to fill would come out solid.
			// This has to leave the widget underneath visible -- it is the thing being
			// pointed at.
			const float t = ring.width;
			canvas.addRect({ r.x, r.y, r.w, t }, ring.color);                         // top
			canvas.addRect({ r.x, r.y + r.h - t, r.w, t }, ring.color);               // bottom
			canvas.addRect({ r.x, r.y + t, t, (std::max)(0.0f, r.h - t * 2.0f) }, ring.color);
			canvas.addRect({ r.x + r.w - t, r.y + t, t, (std::max)(0.0f, r.h - t * 2.0f) }, ring.color);
			break;
		}
	}

	void Focus::endFrame(bool pressed) {
		// A press that landed on nothing focusable drops the keyboard.
		if (pressed && !claimed) id = 0;

		// And Tab, once everything has had its turn to register.
		if (mMove != 0 && !mMoveConsumed && !mFocusables.empty()) {
			size_t at = mFocusables.size(); // "nowhere yet"
			for (size_t i = 0; i < mFocusables.size(); ++i) {
				if (mFocusables[i].id == id) { at = i; break; }
			}
			const size_t count = mFocusables.size();
			size_t next;
			if (at == count) {
				// Nothing had it: forwards starts at the first, backwards at the last.
				next = (mMove > 0) ? 0 : count - 1;
			} else {
				next = (mMove > 0) ? (at + 1) % count : (at + count - 1) % count;
			}
			id = mFocusables[next].id;
			// And this is the one move that earns a ring: nothing else on screen says
			// where Tab has just taken the keyboard.
			byKeyboard = true;
		}
	}
}
