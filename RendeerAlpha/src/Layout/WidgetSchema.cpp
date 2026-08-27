#include <Layout/WidgetSchema.h>
#include <iterator>

namespace RDA::Layout {

	namespace {
		// The declaration reads as a table rather than as code, which is the point: adding
		// a property is one line, and the line says everything about it that anyone needs.
		#define PROP(name, type, doc)          PropDesc{ #name, PropType::type, nullptr, doc }
		#define PROP_ENUM(name, values, doc)   PropDesc{ #name, PropType::Enum,    values, doc }
		#define PROP_VARIANT(themeElement)     PropDesc{ "variant", PropType::Variant, themeElement, \
		                                                 "theme variant to draw with" }
		#define WIDGET(name, props, doc)       WidgetDesc{ #name, props, std::size(props), doc }

		constexpr PropDesc kCommon[] = {
			PROP(id,      String, "name for this node; the compiler turns it into a path, which is what hover and focus are keyed on"),
			PROP(visible, Bool,   "drawn and interactive when true"),

			PROP(width,   Size,   "how wide, when a layout container is deciding"),
			PROP(height,  Size,   "how tall, when a layout container is deciding"),
			PROP(minWidth,  Number, "lower bound on the resolved width"),
			PROP(maxWidth,  Number, "upper bound on the resolved width"),
			PROP(minHeight, Number, "lower bound on the resolved height"),
			PROP(maxHeight, Number, "upper bound on the resolved height"),

			// Absolute placement, used when no layout container is arranging this node.
			PROP(x, Number, "left edge, relative to the parent's content origin"),
			PROP(y, Number, "top edge, relative to the parent's content origin"),
			PROP(w, Number, "width, when placed absolutely rather than laid out"),
			PROP(h, Number, "height, when placed absolutely rather than laid out"),
			PROP(marginRight,  Number, "distance kept from the parent's right edge when anchored to it"),
			PROP(marginBottom, Number, "distance kept from the parent's bottom edge when anchored to it"),
			PROP_ENUM(anchor, "fill|stretchX|stretchY|bottomLeft|bottomRight",
			          "which parent edges this node follows"),
		};

		constexpr PropDesc kContainer[] = {
			PROP(visible, Bool, "grouping node; draws nothing of its own"),
		};
		constexpr PropDesc kPanel[] = {
			PROP_VARIANT("panel"),
		};
		constexpr PropDesc kStack[] = {
			PROP(vertical, Bool,   "top to bottom when true, left to right when false"),
			PROP(spacing,  Number, "gap between children"),
			PROP(padding,  Number, "inset around the whole row or column"),
		};
		constexpr PropDesc kScrollView[] = {
			// Styled by a text field variant: that is where the scroll colours live, so a
			// scroll view matches the fields it usually contains.
			PROP_VARIANT("textfield"),
			PROP(spacing, Number, "gap between stacked children"),
			PROP(padding, Number, "inset around the content"),
		};
		constexpr PropDesc kSplitter[] = {
			PROP(vertical, Bool,   "a vertical bar dragged left and right when true"),
			PROP(value,    Number, "the size this splitter controls"),
			PROP(min,      Number, "smallest it will drag to"),
			PROP(max,      Number, "largest it will drag to"),
		};
		constexpr PropDesc kViewport[] = {
			PROP(visible, Bool, "shows the scene; needs ViewportMode::Widget"),
		};
		constexpr PropDesc kLabel[] = {
			PROP(text, String, "the text drawn"),
			PROP_VARIANT("label"),
		};
		constexpr PropDesc kButton[] = {
			PROP(text, String, "the text drawn on it"),
			PROP_VARIANT("button"),
		};
		constexpr PropDesc kCheckbox[] = {
			PROP(label, String, "the text beside the box"),
			PROP(value, Bool,   "ticked when true"),
			PROP_VARIANT("checkbox"),
		};
		constexpr PropDesc kSlider[] = {
			PROP(value, Number, "where the knob starts"),
			PROP(min,   Number, "value at the left end"),
			PROP(max,   Number, "value at the right end"),
			PROP_VARIANT("slider"),
		};
		constexpr PropDesc kTextField[] = {
			PROP(text, String, "the contents"),
			PROP_ENUM(mode, "line|document|code",
			          "single line, a text area, or a code editor with a gutter"),
			PROP_VARIANT("textfield"),
		};

		constexpr WidgetDesc kWidgets[] = {
			WIDGET(container,  kContainer,  "groups children without drawing anything"),
			WIDGET(panel,      kPanel,      "a framed background that clips its children"),
			WIDGET(stack,      kStack,      "lays children out in a row or a column"),
			WIDGET(scrollview, kScrollView, "a clipped window onto content taller than itself"),
			WIDGET(splitter,   kSplitter,   "a draggable bar that resizes what is above it"),
			WIDGET(viewport,   kViewport,   "draws the 3D scene"),
			WIDGET(label,      kLabel,      "a line of text"),
			WIDGET(button,     kButton,     "a clickable button"),
			WIDGET(checkbox,   kCheckbox,   "a labelled boolean toggle"),
			WIDGET(slider,     kSlider,     "a draggable value between two bounds"),
			WIDGET(textfield,  kTextField,  "an editable text field"),
		};

		#undef PROP
		#undef PROP_ENUM
		#undef PROP_VARIANT
		#undef WIDGET
	}

	const PropDesc* commonProps(size_t& count) {
		count = std::size(kCommon);
		return kCommon;
	}

	const WidgetDesc* widgetSchema(size_t& count) {
		count = std::size(kWidgets);
		return kWidgets;
	}

	const WidgetDesc* findWidget(std::string_view name) {
		for (const WidgetDesc& widget : kWidgets) {
			if (name == widget.name) return &widget;
		}
		return nullptr;
	}

	const PropDesc* findProp(const WidgetDesc& widget, std::string_view prop) {
		for (size_t i = 0; i < widget.propCount; ++i) {
			if (prop == widget.props[i].name) return &widget.props[i];
		}
		for (const PropDesc& common : kCommon) {
			if (prop == common.name) return &common;
		}
		return nullptr;
	}

	const char* propTypeName(PropType type) {
		switch (type) {
		case PropType::String:  return "string";
		case PropType::Number:  return "number";
		case PropType::Bool:    return "boolean";
		case PropType::Size:    return "size";
		case PropType::Variant: return "variant";
		case PropType::Enum:    return "enum";
		}
		return "unknown";
	}
}
