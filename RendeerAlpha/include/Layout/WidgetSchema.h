#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// What a widget accepts, declared once.
//
// Three things need to agree about the name and type of every layout property: the
// loader that applies it, the type definitions an author writes against, and whatever
// tooling later wants to list what exists. Written out three times they drift, and the
// drift is invisible — a prop the loader quietly ignores looks exactly like a prop that
// works, right up until someone notices the button is the wrong size.
//
// So they are written once, here, as a constexpr table. It costs nothing at runtime:
// the whole thing is static data in the binary, and the loader only consults it to
// report a name it does not recognise.
//
// The declaration lives in App tier because the loader validates against it. The type
// emitter that reads it is Studio-only, which is the right way round — an application
// checks what it loads, and only an authoring build describes it.
namespace RDA::Layout {

	enum class PropType : uint8_t {
		String,   // "hello"
		Number,   // 24
		Bool,     // true
		Size,     // a number, or "content" / "fill" / "fill:2"
		Variant,  // a theme variant name; `values` says which theme element defines them
		Enum,     // one of `values`, which is a '|' separated list
	};

	struct PropDesc {
		const char* name;
		PropType    type;
		// Enum: the permitted words. Variant: the theme element whose variants apply,
		// which is not always the widget's own name — a scroll view is styled by a text
		// field variant, because that is where the scroll colours already live.
		const char* values;
		const char* doc;
	};

	struct WidgetDesc {
		const char*     name;
		const PropDesc* props;      // the widget's own, not counting the common ones
		size_t          propCount;
		const char*     doc;
	};

	// Accepted by every widget: identity, placement and sizing.
	const PropDesc* commonProps(size_t& count);

	// Every widget the loader can build, in the order they are declared.
	const WidgetDesc* widgetSchema(size_t& count);

	const WidgetDesc* findWidget(std::string_view name);

	// A prop of that widget, its own or a common one, or nullptr if it takes no such
	// thing. This is what turns a typo into a message instead of into silence.
	const PropDesc* findProp(const WidgetDesc& widget, std::string_view prop);

	// For diagnostics and for the type emitter.
	const char* propTypeName(PropType type);
}
