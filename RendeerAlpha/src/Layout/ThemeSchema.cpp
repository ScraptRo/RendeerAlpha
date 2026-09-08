#include <Layout/ThemeSchema.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace RDA::Layout {

	namespace {
		// As in the widget schema: the declaration reads as a table, and adding a field is
		// one line that says everything about it anyone needs.
		#define COLOUR(name, doc) ThemeFieldDesc{ #name, ThemeFieldType::Colour, nullptr, doc }
		#define NUMBER(name, doc) ThemeFieldDesc{ #name, ThemeFieldType::Number, nullptr, doc }
		#define BOOL(name, doc)   ThemeFieldDesc{ #name, ThemeFieldType::Bool,   nullptr, doc }
		#define TEXT(name, doc)   ThemeFieldDesc{ #name, ThemeFieldType::Text,   nullptr, doc }
		#define ENUM(name, values, doc) ThemeFieldDesc{ #name, ThemeFieldType::Enum, values, doc }
		#define SYNTAX(name, doc) ThemeFieldDesc{ #name, ThemeFieldType::Syntax, nullptr, doc }
		#define ELEMENT(name, fields, doc) \
			ThemeElementDesc{ #name, fields, std::size(fields), doc }

		// How a widget's colours and corners move when what it is doing changes. On
		// everything that reacts to a pointer, and on a panel, whose variant may be a
		// binding; a label has nothing to transition between.
		#define MOTION_FIELDS 			NUMBER(transitionMs, "how long its colours take to follow hover and press; 0 is instant"), 			ENUM(easing, "out|in|inOut|linear", "the shape of that transition; out is the default")

		// Text on anything that draws it. Repeated rather than shared, because a struct
		// that only some elements have is a footnote every reader has to carry.
		#define FONT_FIELDS \
			NUMBER(fontSize, "one of the sizes baked at startup; omitted, the base size"), \
			ENUM(weight, "regular|bold", "bold is synthesised, and does not change metrics")

		constexpr ThemeFieldDesc kCommon[] = {
			TEXT(base, "the variant this one starts from; omitted, the same name's default"),
		};

		constexpr ThemeFieldDesc kButton[] = {
			COLOUR(normal,  "the fill when nothing is happening to it"),
			COLOUR(hovered, "the fill under the pointer"),
			COLOUR(pressed, "the fill while held"),
			COLOUR(text,    "the label on it"),
			COLOUR(border,  "the outline, drawn when borderWidth is above zero"),
			NUMBER(borderWidth, "0 for no border"),
			NUMBER(radius,      "corner radius in pixels; 0 is square"),
			FONT_FIELDS,
			MOTION_FIELDS,
		};

		constexpr ThemeFieldDesc kCheckbox[] = {
			COLOUR(box,      "the empty box"),
			COLOUR(boxHover, "the box under the pointer"),
			COLOUR(check,    "the mark when it is ticked"),
			COLOUR(label,    "the text beside it"),
			COLOUR(border,   "the box outline"),
			NUMBER(borderWidth, "0 for no border"),
			NUMBER(radius,      "corner radius of the box"),
			NUMBER(checkInset,  "the mark's size as a fraction of the box"),
			FONT_FIELDS,
			MOTION_FIELDS,
		};

		constexpr ThemeFieldDesc kSlider[] = {
			COLOUR(track,      "the groove"),
			COLOUR(fill,       "the part of the groove behind the knob"),
			COLOUR(knob,       "the handle"),
			COLOUR(knobActive, "the handle while it is being dragged"),
			NUMBER(knobWidth,  "how wide the handle is, in pixels"),
			NUMBER(radius,     "corner radius of the groove and handle"),
			MOTION_FIELDS,
		};

		constexpr ThemeFieldDesc kFocus[] = {
			COLOUR(color,  "the ring around whatever has the keyboard"),
			NUMBER(width,  "how thick it is"),
			NUMBER(inset,  "negative to sit outside the widget's own edge"),
		};

		constexpr ThemeFieldDesc kBackground[] = {
			COLOUR(color, "what is behind the whole interface"),
			MOTION_FIELDS,
		};

		constexpr ThemeFieldDesc kPanel[] = {
			COLOUR(body,   "the background"),
			COLOUR(accent, "the strip along the top"),
			NUMBER(accentHeight, "how tall that strip is; 0 for none"),
			COLOUR(border, "the outline"),
			NUMBER(borderWidth, "0 for no border"),
			NUMBER(radius,      "corner radius in pixels"),
			MOTION_FIELDS,
		};

		constexpr ThemeFieldDesc kLabel[] = {
			COLOUR(color, "the text"),
			FONT_FIELDS,
		};

		constexpr ThemeFieldDesc kTextField[] = {
			ENUM(mode, "line|document|code",
			     "starts from that mode's defaults instead of inheriting a variant"),
			COLOUR(background,  "behind the text"),
			COLOUR(text,        "the text itself, and unhighlighted code"),
			COLOUR(caret,       "the insertion point"),
			COLOUR(selection,   "behind selected text; usually part-transparent"),
			COLOUR(gutter,      "behind the line numbers, in code mode"),
			COLOUR(lineNumber,  "the line numbers themselves"),
			COLOUR(currentLine, "behind the line the caret is on"),
			COLOUR(border,      "the outline"),
			COLOUR(scrollTrack,      "the scroll bar's groove"),
			COLOUR(scrollThumb,      "the scroll bar's handle"),
			COLOUR(scrollThumbHover, "the handle under the pointer"),
			NUMBER(padding,     "space between the border and the text"),
			NUMBER(borderWidth, "0 for no border"),
			NUMBER(radius,      "corner radius in pixels"),
			NUMBER(caretWidth,  "how wide the insertion point is, in pixels"),
			BOOL(readOnly,      "refuse edits, while still allowing selection"),
			BOOL(multiline,     "let it hold more than one line"),
			BOOL(showLineNumbers,      "draw the gutter"),
			BOOL(highlightCurrentLine, "tint the line the caret is on"),
			TEXT(language, "which syntax definition to highlight with"),
			SYNTAX(syntax, "one colour per token kind"),
			MOTION_FIELDS,
		};

		constexpr ThemeFieldDesc kDock[] = {
			COLOUR(pane,      "behind a docked panel's contents"),
			COLOUR(tabStrip,  "behind the row of tabs"),
			COLOUR(tab,       "an inactive tab"),
			COLOUR(tabActive, "the tab whose panel is showing"),
			COLOUR(tabText,   "the text on a tab"),
			COLOUR(titleBar,       "a floating panel's title bar"),
			COLOUR(titleBarActive, "the same, while it is the focused window"),
			COLOUR(close,      "the close button"),
			COLOUR(closeHover, "the close button under the pointer"),
			COLOUR(grip,       "the drag handle"),
			COLOUR(splitter,      "the bar between two panes"),
			COLOUR(splitterHover, "that bar under the pointer"),
			COLOUR(dropBand,    "the edge target shown while dragging a panel"),
			COLOUR(dropBandHot, "that target under the pointer"),
			COLOUR(dropPane,    "the area a drop would land in"),
			COLOUR(dropPreview, "the outline of where the panel would go"),
			NUMBER(tabHeight,  "how tall the tab strip is"),
			NUMBER(tabPadding, "space either side of a tab's text"),
			NUMBER(closeWidth, "how wide the close button is"),
			NUMBER(radius,     "corner radius of a pane"),
			MOTION_FIELDS,
		};

		// A `syntax` object's keys: one per token kind the highlighter produces.
		constexpr ThemeFieldDesc kSyntax[] = {
			COLOUR(plain,        "anything the highlighter did not classify"),
			COLOUR(keyword,      "language keywords"),
			COLOUR(type,         "type names"),
			COLOUR(string,       "string literals"),
			COLOUR(number,       "numeric literals"),
			COLOUR(comment,      "comments"),
			COLOUR(operator,     "operators and punctuation"),
			COLOUR(function,     "names being called"),
			COLOUR(preprocessor, "preprocessor directives"),
		};

		constexpr ThemeElementDesc kElements[] = {
			ELEMENT(button,    kButton,    "clickable buttons"),
			ELEMENT(checkbox,  kCheckbox,  "labelled boolean toggles"),
			ELEMENT(slider,    kSlider,    "draggable values"),
			ELEMENT(panel,     kPanel,     "framed backgrounds, and the body of a dropdown"),
			ELEMENT(focus,     kFocus,     "the ring around whatever has the keyboard"),
			ELEMENT(background, kBackground, "what is behind the whole interface"),
			ELEMENT(label,     kLabel,     "lines of text"),
			ELEMENT(textfield, kTextField, "editable text, from one line to a code editor"),
			ELEMENT(dock,      kDock,      "the chrome around movable panels"),
		};

		#undef COLOUR
		#undef NUMBER
		#undef BOOL
		#undef TEXT
		#undef ENUM
		#undef SYNTAX
		#undef ELEMENT
		#undef FONT_FIELDS
		#undef MOTION_FIELDS

		// Levenshtein, capped: it is only ever run on a name that is already wrong, and
		// only to decide whether a suggestion is worth making.
		size_t distance(std::string_view a, std::string_view b) {
			std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);
			for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;
			for (size_t i = 1; i <= a.size(); ++i) {
				current[0] = i;
				for (size_t j = 1; j <= b.size(); ++j) {
					const size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
					current[j] = (std::min)({ previous[j] + 1, current[j - 1] + 1, substitution });
				}
				previous.swap(current);
			}
			return previous[b.size()];
		}

		// Close enough to be a typo rather than a different word. A third of the name,
		// which lets `borderWith` find `borderWidth` and stops `radius` finding `padding`.
		std::string_view nearestOf(const ThemeFieldDesc* fields, size_t count,
		                           std::string_view wrong) {
			std::string_view best;
			size_t bestDistance = (std::max<size_t>)(1, wrong.size() / 3) + 1;
			for (size_t i = 0; i < count; ++i) {
				const size_t d = distance(wrong, fields[i].name);
				if (d < bestDistance) { bestDistance = d; best = fields[i].name; }
			}
			return best;
		}
	}

	const ThemeFieldDesc* commonThemeFields(size_t& count) {
		count = std::size(kCommon);
		return kCommon;
	}

	const ThemeElementDesc* themeSchema(size_t& count) {
		count = std::size(kElements);
		return kElements;
	}

	const ThemeFieldDesc* syntaxFields(size_t& count) {
		count = std::size(kSyntax);
		return kSyntax;
	}

	const ThemeElementDesc* findThemeElement(std::string_view name) {
		for (const ThemeElementDesc& element : kElements) {
			if (name == element.name) return &element;
		}
		return nullptr;
	}

	const ThemeFieldDesc* findThemeField(const ThemeElementDesc& element, std::string_view field) {
		for (size_t i = 0; i < element.fieldCount; ++i) {
			if (field == element.fields[i].name) return &element.fields[i];
		}
		for (const ThemeFieldDesc& common : kCommon) {
			if (field == common.name) return &common;
		}
		return nullptr;
	}

	std::string_view nearestThemeField(const ThemeElementDesc& element, std::string_view wrong) {
		const std::string_view own = nearestOf(element.fields, element.fieldCount, wrong);
		if (!own.empty()) return own;
		// The common fields only apply to a real element. A caller checking a nested list
		// passes a made-up descriptor with no name, and `base` is not a misspelling of a
		// token kind.
		if (element.name && *element.name) return nearestOf(kCommon, std::size(kCommon), wrong);
		return {};
	}

	std::string_view nearestThemeElement(std::string_view wrong) {
		std::string_view best;
		size_t bestDistance = (std::max<size_t>)(1, wrong.size() / 3) + 1;
		for (const ThemeElementDesc& element : kElements) {
			const size_t d = distance(wrong, element.name);
			if (d < bestDistance) { bestDistance = d; best = element.name; }
		}
		return best;
	}
}
