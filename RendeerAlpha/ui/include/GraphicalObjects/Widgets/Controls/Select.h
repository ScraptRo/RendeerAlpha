#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// One choice in a Select. Carries no appearance of its own: the select draws it.
	class Option : public Widget {
	public:
		Option(std::string id, std::string text, std::string value)
			: Widget(std::move(id)), text(std::move(text)), value(std::move(value)) {}
		std::string text;
		std::string value;
		void paint(Gui&, glm::vec2) override {} // drawn by the Select that owns it
	};

	// A closed box showing the chosen option, and a list when it is open.
	//
	// The list is drawn by paintAbove, so it escapes whatever clips this widget and sits
	// over everything -- that is what Gui::drawAbove exists for. onChange fires with the
	// chosen option's `value`, which is a string, so it binds to a text signal the same
	// way a text field does.
	class Select : public Widget {
	public:
		explicit Select(std::string id) : Widget(std::move(id)) {}
		std::string                      value;      // the chosen option's value
		std::string                      placeholder = "Choose...";
		Variant                          variant = kDefaultVariant;
		std::function<void(std::string)> onChange;

		// A declared table to take the choices from, instead of <option> children.
		//
		// <option> is written in the layout and compiled ahead of time, which is right
		// for a fixed set and impossible for one the machine discovers -- the models
		// installed, a recent-files list, the tables in a database. Naming a table here
		// is the same answer <list> already gives, and there is then no maximum to
		// declare and nothing to hide.
		std::string of;
		std::string textColumn  = "text";   // the column a row shows
		std::string valueColumn = "value";  // the column a row means

		// How many choices there are, and what the i-th one says and means.
		//
		// One pair of accessors rather than five loops, because the choices now come from
		// two places and every one of those loops had to agree about which. The <option>
		// path skips a hidden child, which is what makes "declare a maximum and bind
		// visible" work as well -- it silently did not, and drew blank rows.
		size_t choiceCount() const;
		bool   choiceAt(size_t index, std::string_view& text, std::string_view& value) const;

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;
		void paintAbove(Gui& gui, glm::vec2 origin) override;
		void accessibleItemAction(int item, AccessibleAction action, const std::string& value) override;

	protected:
		// Which row the keyboard is on while the list is open. -1 is "none yet", which is
		// what the pointer leaves it at: a list opened with the mouse highlights nothing
		// until an arrow key says where to start.
		int mHighlight = -1;
		void chooseHighlighted();

		// Type-to-find: the letters typed so far, and how long since the last one. A pause
		// starts a new search, which is what lets "s" then, a second later, "t" find the
		// first S and then the first T rather than looking for "st".
		std::string mFind;
		float       mFindAge = 0.0f;
		void findTyped(const std::string& typed);

	private:
		// The choices are the Option children, read where they are rather than copied
		// into a list of their own -- so a binding on an option's text is an ordinary
		// binding on an ordinary widget, and nothing has to be kept in step.
		bool  mOpen = false;
		// How far open the list is drawn, 0 to 1. Not the same question as `mOpen`, which
		// is the answer it is on its way to: the list goes on being drawn after the press
		// that shut it, or it would have nowhere to close from.
		float mOpenAmount = 0.0f;
		Rect  mClosedRect{};     // where the box was drawn, so the list lands under it
	};
}
