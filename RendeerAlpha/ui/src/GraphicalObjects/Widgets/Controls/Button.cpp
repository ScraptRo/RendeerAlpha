#include <GraphicalObjects/Widgets/Controls/Button.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

	void Button::offerShortcuts(std::vector<Widget*>& out) {
		if (shortcut.valid()) out.push_back(this);
	}

	void Button::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		// With children, the button draws itself around them and its own text is not
		// used -- the children are what it says. Drawn with an empty label rather than
		// through a second code path, so the background, the hover and the press are
		// the same ones every other button has.
		const bool held = !mChildren.empty();
		const bool clicked = gui.button(mId.c_str(), held ? "" : text.c_str(), abs, variant,
		                                hAlign, hAlignOffset, selected);
		if (held) {
			// Inside the padding, and clipped to it: a child that does not fit is cut
			// off at the button's edge rather than drawn over whatever is beside it.
			const Rect inner{ abs.x + padding, abs.y + padding,
			                  (std::max)(0.0f, abs.w - padding * 2.0f),
			                  (std::max)(0.0f, abs.h - padding * 2.0f) };
			gui.pushClipRect(inner);
			paintChildren(gui, glm::vec2(inner.x, inner.y));
			gui.popClipRect();
		}
		if (clicked && onClick) onClick();
	}

	glm::vec2 Button::measureContent(Gui& gui, glm::vec2 available) const {
		// Around its children when it has them: as big as the biggest, plus the padding
		// on both sides. A child that fills asks for nothing, so a button holding only
		// those falls back to the room it was offered rather than collapsing.
		if (!mChildren.empty()) {
			const glm::vec2 room{ (std::max)(0.0f, available.x - padding * 2.0f),
			                      (std::max)(0.0f, available.y - padding * 2.0f) };
			glm::vec2 content{ 0.0f, 0.0f };
			for (const auto& child : mChildren) {
				if (!child || !child->visible) continue;
				const glm::vec2 one = child->measureContent(gui, room);
				content.x = (std::max)(content.x, one.x);
				content.y = (std::max)(content.y, one.y);
			}
			return { content.x + padding * 2.0f, content.y + padding * 2.0f };
		}
		(void)available;
		const TextStyle& font = gui.theme().button(variant).font;
		return { gui.measureText(text.c_str(), font) + kButtonPadX * 2.0f,
		         gui.lineHeight(font) + kButtonPadY * 2.0f };
	}

	bool Button::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Button;
		out.name = !alt.empty() ? alt : (!text.empty() ? text : dragLabel());
		out.focusable = true;
		out.actions = kCanInvoke;
		return true;
	}
	void Button::accessibleAction(AccessibleAction action, const std::string& value) {
		(void)value;
		if (action == AccessibleAction::Invoke) pressShortcut();
	}
}
