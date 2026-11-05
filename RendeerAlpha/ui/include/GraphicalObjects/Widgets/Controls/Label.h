#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A line of text.
	class Label : public Widget {
	public:
		Label(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string text;
		std::string dragLabel() const override { return text; }
		uint32_t    color = rgba(230, 230, 235); // used when variant == "default"
		Variant     variant = kDefaultVariant;   // a named variant's color wins over `color`
		// Break the text to the width it is given rather than letting it run off the
		// edge. Off by default: a caption that silently became three lines tall would
		// move everything under it, and most labels are one line on purpose.
		bool        wrap = false;
		// Runs of the text drawn in their own colour, as `start:length:colour` triples
		// separated by `;` -- "0:3:#E06C75;4:6:#61AFEF". Offsets are bytes.
		//
		// A string rather than a shape, because that is what makes it bindable: it is a
		// column like any other, so a row can carry its own colouring and a list gets
		// syntax colour, a diff or a search highlight without a new mechanism. Anything
		// no span covers is drawn in the label's own colour.
		//
		// Parsed once per distinct value -- see runs().
		std::string spans;

		// `spans`, parsed, clamped to the text and put in order. Rebuilt only when the
		// string or the text it indexes actually changes, so a label re-bound to the same
		// value every frame parses nothing.
		const std::vector<TextSpan>& runs() const;
		// Where the text sits in the box the layout gave this label, across and down.
		// Named for the axes rather than for the container, which is the same pair a
		// stack answers and means the same thing here.
		//
		// Worth having both because they are not the same question: a label in a column
		// is usually as wide as the column and as tall as one line, so hAlign moves the
		// text within a box that is wider than it, and vAlign matters the moment a row
		// is taller than a line -- which is any row holding a button beside it.
		TextAlign   hAlign = TextAlign::Start;
		TextAlign   vAlign = TextAlign::Start;
		// Past where the word puts it, the same nudge a container's alignment takes.
		float       hAlignOffset = 0.0f;
		float       vAlignOffset = 0.0f;
		// As wide as its text and one line tall -- or, when wrapping, as tall as the
		// lines it breaks into -- so height="content" in a layout means what it looks
		// like it means.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;

	private:
		// What runs() built, and what it built them from. Two keys rather than one: the
		// spans are clamped to the text, so the same string over a shorter text is a
		// different answer -- which is exactly what a list row re-bound to a new row is.
		mutable std::vector<TextSpan> mRuns;
		mutable std::string           mRunsFrom;
		mutable size_t                mRunsTextSize = static_cast<size_t>(-1);
	};
}
