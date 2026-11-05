#include <GraphicalObjects/Widgets/Containers/Popup.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

	// ---- Popup -----------------------------------------------------------------------
	glm::vec2 Popup::measureContent(Gui& gui, glm::vec2 available) const {
		// As big as what is inside it. A popup is not in anyone's flow, so nothing gives
		// it a size and this is what decides how big it comes out.
		//
		// A child that names a fixed size is taken at its word rather than measured. A
		// vertical <stack width={160}> measures its *content* width, which for a stack is
		// whatever room it was offered -- so measuring it here made every popup as wide as
		// the window. What the layout wrote is the better answer whenever it wrote one.
		glm::vec2 content{ 0.0f, 0.0f };
		for (const auto& child : mChildren) {
			if (!child || !child->visible) continue;
			const glm::vec2 measured = child->measureContent(gui, available);
			const float w = child->width.mode == SizeSpec::Mode::Fixed && child->width.value > 0.0f
				? child->width.value : measured.x;
			const float h = child->height.mode == SizeSpec::Mode::Fixed && child->height.value > 0.0f
				? child->height.value : measured.y;
			content.x = (std::max)(content.x, w);
			content.y = (std::max)(content.y, h);
		}
		return content;
	}

	void Popup::paint(Gui& gui, glm::vec2 origin) {
		(void)origin;
		// Asked for every frame, open or shut. The note is only taken for anchors
		// somebody wants, and "somebody wants it" has to be true before the frame the
		// popup opens on -- otherwise it would have nowhere to appear the first time.
		// "pointer" is not a widget, so nothing is asked to remember where it was.
		if (!anchor.empty() && anchor != "pointer") gui.wantAnchor(anchor);

		// The moment it opens, and only that moment: a menu is put where the pointer was
		// when it was asked for, and stays there. Read here rather than in paintAbove
		// because this runs whether or not it is open, which is what makes the change
		// visible at all.
		if (open && !mWasOpen) mOpenedAt = gui.input().pointer;
		mWasOpen = open;

		// Shut, it is a menu whose entries still have their shortcuts -- that is most of
		// what a shortcut beside a menu entry is for. Open, its children are painted in
		// paintAbove and offer them the ordinary way.
		if (!open) {
			gui.offerShortcutsIn(*this);
			return;
		}

		// Nothing is drawn here: a popup that drew in the tree would be clipped by
		// whatever panel it happens to live in, and covered by whatever comes after it.
		gui.drawAbove(this, glm::vec2(0.0f));

		// Asked for now rather than in paintAbove, because the claim is for the *next*
		// frame's walk and paintAbove runs after this frame's has finished either way.
		if (blocking) gui.claimPointer();
	}

	void Popup::paintAbove(Gui& gui, glm::vec2 origin) {
		(void)origin;
		const glm::vec2 viewport = gui.input().viewport;

		// How big it wants to be. A size written on the popup wins, the way it does
		// everywhere else; otherwise it is as big as what is inside it.
		const glm::vec2 wanted = measureContent(gui, viewport);
		float w = rect.w > 0.0f ? rect.w : wanted.x + padding * 2.0f;
		float h = rect.h > 0.0f ? rect.h : wanted.y + padding * 2.0f;
		w = (std::min)(w, viewport.x);
		h = (std::min)(h, viewport.y);

		// Where it goes. With no anchor it is placed like anything else -- its own x/y --
		// so a popup can be a plain floating panel without inventing a second mechanism.
		Rect anchorRect{ rect.x, rect.y, 0.0f, 0.0f };
		bool anchored = false;
		if (anchor == "pointer") {
			// A point, not a widget: the four placements then read as which corner of the
			// menu the pointer is at, and `over` is the ordinary context menu.
			anchorRect = Rect{ mOpenedAt.x, mOpenedAt.y, 0.0f, 0.0f };
			anchored = true;
		} else if (!anchor.empty()) {
			anchored = gui.anchorRect(anchor, anchorRect);
		}

		float x = anchorRect.x;
		float y = anchorRect.y;
		if (anchored) {
			switch (placement) {
			case Placement::Below: y = anchorRect.y + anchorRect.h + gap; break;
			case Placement::Above: y = anchorRect.y - gap - h;            break;
			case Placement::Right: x = anchorRect.x + anchorRect.w + gap; break;
			case Placement::Left:  x = anchorRect.x - gap - w;            break;
			case Placement::Over:  break;
			}

			// Flipped rather than pushed when it would fall off the edge it is growing
			// towards. A menu near the bottom of the window opening upwards is what every
			// reader expects; sliding it up so it covers its own button is not.
			if (placement == Placement::Below && y + h > viewport.y &&
			    anchorRect.y - gap - h >= 0.0f) {
				y = anchorRect.y - gap - h;
			} else if (placement == Placement::Above && y < 0.0f &&
			           anchorRect.y + anchorRect.h + gap + h <= viewport.y) {
				y = anchorRect.y + anchorRect.h + gap;
			} else if (placement == Placement::Right && x + w > viewport.x &&
			           anchorRect.x - gap - w >= 0.0f) {
				x = anchorRect.x - gap - w;
			} else if (placement == Placement::Left && x < 0.0f &&
			           anchorRect.x + anchorRect.w + gap + w <= viewport.x) {
				x = anchorRect.x + anchorRect.w + gap;
			}
		}

		// And kept on screen either way. A popup half outside the window is a popup with
		// half its options unreachable.
		x = std::clamp(x, 0.0f, (std::max)(0.0f, viewport.x - w));
		y = std::clamp(y, 0.0f, (std::max)(0.0f, viewport.y - h));
		mShown = Rect{ x, y, w, h };
		// Open, it is a node of its own and what is inside it hangs off it.
		if (gui.accessibilityWanted()) gui.noteAccessible(this, mShown);

		gui.beginPanel(mId.c_str(), mShown, variant);
		// Inside the padding, the way a button holds its children.
		paintChildren(gui, glm::vec2(mShown.x + padding, mShown.y + padding));
		gui.endPanel();

		// ---- and when it goes away ----
		//
		// After the children, so a press on one of them is not also a press outside.
		//
		// Whether the anchor counts as outside depends on who has the pointer. While this
		// is blocking, the anchor's own onClick cannot fire -- the walk ran without a
		// pointer -- so a press there is an ordinary outside press and closes the menu,
		// which is what clicking a menu's button a second time should do. When it is not
		// blocking, that same press *does* reach the button, and if this closed as well
		// the two would cancel: the menu would shut and the toggle would reopen it in the
		// same click. So it is excluded there, and the button's own handler is what
		// closes it.
		const GuiInput& in = gui.input();
		const bool anchorIsOutside = blocking;
		bool dismissed = false;
		if (in.lPressed && !mShown.contains(in.pointer) &&
		    !(anchored && !anchorIsOutside && anchorRect.contains(in.pointer))) {
			dismissed = true;
		}
		// Escape, which is the other way out of anything on this screen -- the same
		// meaning it has in a text field.
		for (const GuiEditKey key : in.editKeys) {
			if (key == GuiEditKey::Escape) dismissed = true;
		}
		if (dismissed) {
			// `open` is not written here. It is a binding, and a widget that wrote its own
			// bound property would be overwritten by the signal on the next frame and
			// flicker. The layout closes itself, which is what onClose is for.
			if (onClose) onClose();
		}
	}

	bool Popup::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Menu;
		out.name = alt;
		return true;
	}
}
