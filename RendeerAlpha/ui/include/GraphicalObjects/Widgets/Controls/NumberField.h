#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A number you drag.
	//
	// The widget a parametric tool is mostly made of, and the one this engine did not
	// have: a slider needs a range and spends its width on a track, and a text field
	// makes you select, type and commit to nudge a value by one. This is neither. It
	// reads as a field, it says what it is and what it holds, and dragging across it
	// changes the number -- which is how a value is *adjusted*, as opposed to entered.
	//
	// Click it without dragging and it becomes a real text field, because sometimes the
	// number you want is 1.6180339 and no amount of dragging will find it.
	class NumberField : public Widget {
	public:
		explicit NumberField(std::string id, std::string label = {})
			: Widget(std::move(id)), label(std::move(label)) {}

		std::string label;          // drawn on the left; empty draws only the value
		double value = 0.0;
		// How much one pixel of drag is worth. The default moves a plain 0..1 value
		// across its range in a couple of hundred pixels, which is the feel to match.
		double step = 0.01;
		// Bounds. Equal means unbounded, which is the ordinary case for a number that is
		// a position or an offset rather than a fraction.
		double minValue = 0.0;
		double maxValue = 0.0;
		int    decimals = 3;        // how many places the value is drawn to

		Variant variant = kDefaultVariant;   // a textfield variant, for the box
		std::function<void(double)> onChange;

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;

	private:
		// Dragging, and where from -- so the value moves by the whole distance travelled
		// rather than accumulating rounding a pixel at a time.
		bool      mDragging = false;
		double    mDragFrom = 0.0;
		glm::vec2 mDragStart{ 0.0f };
		// Typing instead. The buffer is the text being edited; it becomes the value when
		// the field is finished with, and is thrown away if Escape drops it.
		bool        mTyping = false;
		bool        mFocused = false;
		std::string mBuffer;
		uint64_t    mBufferVersion = 1;
	};
}
