#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

namespace RDA {

	// A key chord and what it does, with nothing on screen.
	//
	// For the shortcut that belongs to no button: Ctrl+K opening a palette, F5 refreshing,
	// Escape closing a panel the reader got to by other means. Where a button does the same
	// thing, its own `shortcut` is the better place, because then the two cannot drift.
	//
	// Lives where it is written, and that is its scope: one inside a screen works while the
	// screen is showing, one inside a <tab> while that page is. Takes no room.
	class ShortcutKey : public Widget {
	public:
		explicit ShortcutKey(std::string id) : Widget(std::move(id)) {}
		std::function<void()> onPress;

		bool inFlow() const override { return false; }
		void pressShortcut() override { if (onPress) onPress(); }
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}
