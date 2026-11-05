#include <GraphicalObjects/Widgets/Controls/Label.h>
#include "../WidgetInternal.h"
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

	using detail::either;

	const std::vector<TextSpan>& Label::runs() const {
		if (mRunsFrom == spans && mRunsTextSize == text.size()) return mRuns;
		mRunsFrom = spans;
		mRunsTextSize = text.size();
		mRuns.clear();

		// "start:length:colour;start:length:colour" -- three fields, two separators, and
		// nothing that needs a tokenizer. Anything malformed is skipped rather than
		// refused: this arrives from a backend a character at a time while a model is
		// still writing, and half a triple should draw as plain text, not as nothing.
		const int size = static_cast<int>(text.size());
		size_t at = 0;
		while (at < spans.size()) {
			size_t semi = spans.find(';', at);
			if (semi == std::string::npos) semi = spans.size();
			const std::string_view one(spans.data() + at, semi - at);
			at = semi + 1;

			const size_t a = one.find(':');
			if (a == std::string_view::npos) continue;
			const size_t b = one.find(':', a + 1);
			if (b == std::string_view::npos) continue;

			TextSpan span;
			span.start  = std::atoi(std::string(one.substr(0, a)).c_str());
			span.length = std::atoi(std::string(one.substr(a + 1, b - a - 1)).c_str());
			if (!parseColor(std::string(one.substr(b + 1)).c_str(), span.color)) continue;

			// Clamped to the text it indexes. A span past the end is what a backend that
			// coloured one string and sent it with another looks like, and clipping it is
			// the only answer that does not read out of bounds.
			if (span.length <= 0) continue;
			if (span.start < 0) { span.length += span.start; span.start = 0; }
			if (span.start >= size || span.length <= 0) continue;
			span.length = (std::min)(span.length, size - span.start);
			mRuns.push_back(span);
		}

		// Sorted and made disjoint, which is what the draw below relies on. An overlap is
		// resolved in favour of whichever starts first -- a rule, rather than whatever
		// order the sender happened to write them in.
		std::sort(mRuns.begin(), mRuns.end(),
		          [](const TextSpan& l, const TextSpan& r) { return l.start < r.start; });
		int reach = 0;
		size_t kept = 0;
		for (TextSpan& span : mRuns) {
			if (span.start < reach) {
				span.length -= reach - span.start;
				span.start = reach;
				if (span.length <= 0) continue;
			}
			reach = span.start + span.length;
			mRuns[kept++] = span;
		}
		mRuns.resize(kept);
		return mRuns;
	}

	void Label::paint(Gui& gui, glm::vec2 origin) {
		// A named variant's color takes precedence; otherwise the per-instance `color`.
		const LabelStyle& style = gui.theme().label(variant);
		uint32_t c = (variant != kDefaultVariant) ? style.color : color;
		Rect abs = placement(gui, origin);
		const float line = gui.lineHeight(style.font);
		const std::vector<TextSpan>& coloured = runs();

		if (!wrap) {
			const float x = abs.x + alignOffset(hAlign, abs.w,
			                                    gui.measureText(text.c_str(), style.font), hAlignOffset);
			const glm::vec2 at(x, abs.y + alignOffset(vAlign, abs.h, line, vAlignOffset));
			if (coloured.empty()) {
				gui.label(text.c_str(), at, c, style.font);
			} else {
				gui.labelSpans(text.c_str(), 0, static_cast<int>(text.size()), at, c,
				               coloured.data(), coloured.size(), style.font);
			}
			return;
		}
		// Broken to the width it was actually given, not the width it asked for: a
		// wrapped label that measured itself against one width and drew against another
		// would leave a gap under it or run past the bottom of what it was allotted.
		//
		// Each line is aligned on its own, which is what alignment of a paragraph means;
		// the block as a whole is what vAlign moves.
		const auto lines = gui.wrapText(text, abs.w, style.font);
		float y = abs.y + alignOffset(vAlign, abs.h, static_cast<float>(lines.size()) * line, vAlignOffset);
		for (const auto& span : lines) {
			const std::string one = text.substr(span.first, span.second);
			const float x = abs.x + alignOffset(hAlign, abs.w,
			                                    gui.measureText(one.c_str(), style.font), hAlignOffset);
			if (coloured.empty()) {
				gui.label(one.c_str(), glm::vec2(x, y), c, style.font);
			} else {
				// The whole string with this line's slice named, rather than the copy
				// above: the spans are written against the string, so a run crossing a
				// line break is drawn in its colour on both of them without the caller
				// having to work out where the breaks fell.
				gui.labelSpans(text.c_str(), static_cast<int>(span.first),
				               static_cast<int>(span.second), glm::vec2(x, y), c,
				               coloured.data(), coloured.size(), style.font);
			}
			y += line;
		}
	}

	glm::vec2 Label::measureContent(Gui& gui, glm::vec2 available) const {
		const TextStyle& font = gui.theme().label(variant).font;
		const float line = gui.lineHeight(font);
		if (!wrap) return { gui.measureText(text.c_str(), font), line };
		// Wrapping turns a width into a height, which is the one case where measuring
		// has to look at the space it was offered rather than only at its own content.
		const size_t lines = gui.wrapText(text, available.x, font).size();
		return { available.x, static_cast<float>(lines) * line };
	}

	bool Label::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Text;
		out.name = either(alt, text);
		return !out.name.empty();
	}
}
