#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// What a theme accepts, declared once.
//
// The same problem the widget schema solves, one layer over: a theme field the reader
// does not look for is silently ignored, and `borderWith: 1` looks exactly like a border
// that works right up until someone notices there is no border. A colour is not the kind
// of mistake that announces itself.
//
// So the names are written once, here, and the theme compiler refuses one it does not
// know -- with the nearest name it does know, because the mistake is nearly always a
// letter. The type emitter reads the same table, so an editor underlines it before the
// build gets a chance to.
//
// This table is the names and types; the readers in GuiTheme.cpp are what apply them. A
// field in one and not the other is a build failure the first time someone writes it,
// which is the failure mode to want -- the alternative, before this existed, was silence.
namespace RDA::Layout {

	enum class ThemeFieldType : uint8_t {
		Colour,  // "#RGB", "#RRGGBB", "#RRGGBBAA", or what the colour helpers return
		Number,
		Bool,
		Text,
		Enum,    // one of `values`, a '|' separated list
		Syntax,  // a nested object: one colour per token kind
	};

	struct ThemeFieldDesc {
		const char*    name;
		ThemeFieldType type;
		const char*    values; // Enum only
		const char*    doc;
	};

	struct ThemeElementDesc {
		const char*           name;   // the export a theme file writes: `export const button`
		const ThemeFieldDesc* fields;
		size_t                fieldCount;
		const char*           doc;
	};

	// Accepted by every element: which variant a partial entry starts from.
	const ThemeFieldDesc* commonThemeFields(size_t& count);

	// Every element a theme may declare, in the order they are declared here.
	const ThemeElementDesc* themeSchema(size_t& count);
	const ThemeElementDesc* findThemeElement(std::string_view name);

	// A field of that element, its own or a common one, or nullptr for a name it does not
	// take. This is what turns a typo into a message instead of into silence.
	const ThemeFieldDesc* findThemeField(const ThemeElementDesc& element, std::string_view field);

	// The token kinds a `syntax` object may colour.
	const ThemeFieldDesc* syntaxFields(size_t& count);

	// The declared name closest to `wrong`, for "did you mean". Empty when nothing is
	// close enough to be worth suggesting -- a wrong guess is worse than no guess.
	std::string_view nearestThemeField(const ThemeElementDesc& element, std::string_view wrong);
	std::string_view nearestThemeElement(std::string_view wrong);
}
