#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A clickable button. onClick fires once on each completed click.
	//
	// It may hold children, and then it is a button around them rather than around a
	// string: an icon beside a label, two lines of text, a row of anything. They are
	// laid out inside its rect exactly as a panel's are, so a stack inside one arranges
	// them and the button itself stays a button rather than growing a second layout
	// engine of its own.
	//
	// The children are drawn, and the button takes the input. A widget inside one that
	// would otherwise be clickable is not: the press belongs to the button, which is
	// the same answer HTML gives and the only one that makes "the whole thing is one
	// control" true.
	class Button : public Widget {
	public:
		Button(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string           text;
		Variant               variant = kDefaultVariant; // theme variant (see Gui::theme())
		// Where the label sits across the button. Centred is what a button looks like; a
		// column of them used as a menu wants Start, so every entry begins in the same
		// place instead of each one beginning somewhere else.
		TextAlign             hAlign = TextAlign::Center;
		float                 hAlignOffset = 0.0f;
		// Inset around the children, when it has any. The room its own text would have
		// had, so a button around an icon is the size a button around a word is.
		float                 padding = 6.0f;
		// Chosen, and staying chosen: one of a row of tabs, a segmented control, the
		// tool that is currently the tool. Bindable, and the layout owns it -- this is
		// never written by the button, the same rule every other two-way property here
		// follows.
		bool                  selected = false;
		std::function<void()> onClick;
		void pressShortcut() override { if (onClick) onClick(); }
		std::string dragLabel() const override { return text; }
		// Its children are drawn but take no input, so nothing in them has a shortcut
		// that could run either.
		void offerShortcuts(std::vector<Widget*>& out) override;
		// Its text plus room to breathe around it.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;
	};
}
