#include <Layout/WidgetSchema.h>
#include <iterator>

namespace RDA::Layout {

	namespace {
		// The declaration reads as a table rather than as code, which is the point: adding
		// a property is one line, and the line says everything about it that anyone needs.
		#define PROP(name, type, doc)          PropDesc{ #name, PropType::type, nullptr, doc }
		#define PROP_ENUM(name, values, doc)   PropDesc{ #name, PropType::Enum,    values, doc }
		// An alignment: one of `values`, optionally followed by "+12" or "-8".
		#define PROP_ALIGN(name, values, doc)  PropDesc{ #name, PropType::Align,   values, doc }
		#define PROP_VARIANT(themeElement)     PropDesc{ "variant", PropType::Variant, themeElement, \
		                                                 "theme variant to draw with" }
		#define WIDGET(name, props, doc)       WidgetDesc{ #name, props, std::size(props), doc }
		#define PROP_EVENT(name, doc)          PropDesc{ #name, PropType::Event, nullptr, doc }
		// An event that hands its handler a value. `type` is the TypeScript that value
		// has, so the generated .d.ts types the parameter instead of leaving it `any`.
		#define PROP_EVENT_OF(name, type, doc) PropDesc{ #name, PropType::Event, type, doc }

		constexpr PropDesc kCommon[] = {
			PROP(id,      String, "name for this node; the compiler turns it into a path, which is what hover and focus are keyed on"),
			PROP(visible, Bool,   "drawn and interactive when true"),
			PROP(dragWindow, Bool, "dragging this moves the window; for a window opened "
			                       "with frame={false}, where the layout draws its own "
			                       "title bar"),
			PROP(route, String, "the shape this travels along when it moves, as an SVG "
			                    "path; fitted to wherever it is going, so one route works "
			                    "between any two places"),
			PROP(pace, String, "how fast it travels that shape: linear, in, out, inOut, or "
			                   "cubic-bezier(x1,y1,x2,y2)"),
			PROP(animate, Number, "milliseconds this eases over when the layout moves it; inherited by its children, 0 to move at once"),

			PROP(width,   Size,   "how wide, when a layout container is deciding"),
			PROP(height,  Size,   "how tall, when a layout container is deciding"),
			PROP(minWidth,  Number, "lower bound on the resolved width"),
			PROP(maxWidth,  Number, "upper bound on the resolved width"),
			PROP(minHeight, Number, "lower bound on the resolved height"),
			PROP(maxHeight, Number, "upper bound on the resolved height"),
			PROP_ALIGN(hAlignSelf, "auto|stretch|start|center|end",
			          "this child's own answer to its container's hAlign; auto lets the container decide"),
			PROP_ALIGN(vAlignSelf, "auto|stretch|start|center|end",
			          "this child's own answer to its container's vAlign; auto lets the container decide"),

			// Absolute placement, used when no layout container is arranging this node.
			PROP(x, Number, "left edge, relative to the parent's content origin"),
			PROP(y, Number, "top edge, relative to the parent's content origin"),
			PROP(w, Number, "width, when placed absolutely rather than laid out"),
			PROP(h, Number, "height, when placed absolutely rather than laid out"),
			PROP(marginRight,  Number, "distance kept from the parent's right edge when anchored to it"),
			PROP(marginBottom, Number, "distance kept from the parent's bottom edge when anchored to it"),
			PROP_ENUM(anchor, "fill|stretchX|stretchY|bottomLeft|bottomRight",
			          "which parent edges this node follows"),
				PROP_EVENT_OF(onHover, "boolean", "runs when the pointer arrives or leaves, with which"),
	};

		constexpr PropDesc kContainer[] = {
			PROP(visible, Bool, "grouping node; draws nothing of its own"),
		};
		constexpr PropDesc kPanel[] = {
			PROP_VARIANT("panel"),
		};
		constexpr PropDesc kStack[] = {
			PROP_ENUM(arrange, "vertical|horizontal",
			          "which way its children are laid out"),
			PROP(spacing,  Number, "gap between children"),
			PROP(padding,  Number, "inset around the whole row or column"),
			PROP_ALIGN(hAlign, "stretch|start|center|end",
			          "where children sit horizontally, whichever way this stack runs"),
			PROP_ALIGN(vAlign, "stretch|start|center|end",
			          "where children sit vertically, whichever way this stack runs"),
			PROP_ENUM(spread, "spaceBetween|spaceAround",
			          "how room left over along the stacking axis is shared between children"),
		};
		constexpr PropDesc kScroll[] = {
			// Styled by a text field variant: that is where the scroll colours live, so a
			// scroll view matches the fields it usually contains.
			PROP_VARIANT("textfield"),
			PROP(spacing, Number, "gap between stacked children"),
			PROP(padding, Number, "inset around the content"),
			PROP_ALIGN(hAlign, "stretch|start|center|end",
			          "where children sit across the column"),
			PROP(vertical,   Bool, "scrolls down when its content is taller; on by default"),
			PROP(horizontal, Bool, "scrolls sideways when its content is wider"),
			PROP(barWidth,   Number, "thickness of the scroll bars"),
			PROP(wheelStep,  Number, "pixels per wheel notch"),
		};
		constexpr PropDesc kSplitter[] = {
			PROP_ENUM(arrange, "vertical|horizontal",
			          "which way the bar splits the area it sits in"),
			PROP(value,    Number, "the size this splitter controls"),
			PROP(min,      Number, "smallest it will drag to"),
			PROP(max,      Number, "largest it will drag to"),
		};
		constexpr PropDesc kViewport[] = {
			PROP(name,    String, "how the backend addresses this surface"),
			PROP(visible, Bool,   "draws it; hidden it takes no space of its own"),
		};
		constexpr PropDesc kLabel[] = {
			PROP(text, String, "the text drawn"),
			PROP(wrap, Bool,   "break the text to its width instead of letting it run past"),
			PROP(spans, String, "runs drawn in their own colour, as start:length:colour triples separated by ; -- offsets in bytes"),
			PROP_ALIGN(hAlign, "start|center|end", "where the text sits across its box"),
			PROP_ALIGN(vAlign, "start|center|end", "where the text sits down its box"),
			PROP_VARIANT("label"),
		};
		constexpr PropDesc kButton[] = {
			PROP(text, String, "the text drawn on it, when it holds nothing else"),
			PROP(padding, Number, "inset around its children, when it has any"),
			PROP_ALIGN(hAlign, "start|center|end",
			          "where the text sits across it; centred unless said otherwise"),
			PROP_VARIANT("button"),
			PROP_EVENT(onClick, "runs on each completed click"),
		};
		constexpr PropDesc kCheckbox[] = {
			PROP(label, String, "the text beside the box"),
			PROP(value, Bool,   "ticked when true"),
			PROP_VARIANT("checkbox"),
			PROP_EVENT_OF(onChange, "boolean", "runs when it is toggled, with the new state"),
		};
		constexpr PropDesc kSlider[] = {
			PROP(value, Number, "where the knob starts"),
			PROP(min,   Number, "value at the left end"),
			PROP(max,   Number, "value at the right end"),
			PROP_VARIANT("slider"),
			PROP_EVENT_OF(onChange, "number", "runs while it is dragged, with the new value"),
		};
		constexpr PropDesc kList[] = {
			PROP(of,        String, "which declared table its rows come from"),
			PROP(rowHeight, Number, "height of one row, and the height of any row the column below has no answer for"),
			PROP(rowHeights, String, "a number column holding each row's own height, for rows that are not all alike"),
			PROP(spacing,   Number, "gap between rows"),
			PROP(poolSize,  Number, "how many row widgets to keep; enough to fill the view"),
			PROP(barWidth,  Number, "thickness of the scroll bar"),
			PROP(wheelStep, Number, "pixels per wheel notch"),
			PROP(follow,    Bool,   "stay at the end when rows arrive, while already there"),
			PROP(revealRow, Number, "scroll until this row is in view; -1 asks for nothing"),
			PROP_VARIANT("textfield"),
		};

		constexpr PropDesc kImage[] = {
			PROP(src, String, "path to the picture, relative to the running program; an "
			                  ".svg is drawn at whatever size it is given"),
			PROP(tint, String, "multiplied into the picture; white leaves it alone, and a "
			                 "colour is how one white-drawn icon becomes any of them"),
			PROP_ENUM(fit, "contain|stretch",
			          "keep its shape inside the box, or fill the box and ignore its shape"),
		};

		constexpr PropDesc kStream[] = {
			PROP(name, String, "which stream to show, by the name the backend pushes to"),
			PROP_ENUM(fit, "contain|stretch",
			          "keep the frame's shape inside the box, or fill the box and ignore it"),
		};

		constexpr PropDesc kTabs[] = {
			PROP(value,     Number, "which page is showing, counting from zero"),
			PROP(barHeight, Number, "height of the row of titles; omitted, a line plus padding"),
			PROP_VARIANT("button"),
			PROP_EVENT_OF(onChange, "number", "runs when a title is clicked, with its index"),
		};
		constexpr PropDesc kTab[] = {
			PROP(title, String, "what its tab says"),
		};

		constexpr PropDesc kSelect[] = {
			PROP(value,       String, "the chosen option's value"),
			PROP(placeholder, String, "shown when the value matches no option"),
			PROP(of,          String, "a declared table to take the choices from, instead of <option> children"),
			PROP(textColumn,  String, "which column of `of` a row shows; default \"text\""),
			PROP(valueColumn, String, "which column of `of` a row means; default \"value\""),
			PROP_VARIANT("button"),
			PROP_EVENT_OF(onChange, "string", "runs when one is chosen, with its value"),
		};
		constexpr PropDesc kOption[] = {
			PROP(text,  String, "what the row says"),
			PROP(value, String, "what choosing it sets"),
		};

		constexpr PropDesc kDockSpace[] = {
			PROP(persist, String,
			     "file to remember the arrangement in; omitted, panels open where this says every time"),
			PROP_ENUM(arrange, "panes|tiles",
			          "panes cuts the area up with splitters and tabs, the way an editor "
			          "does; tiles gives each panel a rectangle of cells in a column "
			          "grid, the way a dashboard does"),
			PROP(columns,   Number, "tiles: how many columns the grid has (default 12)"),
			PROP(rowHeight, Number, "tiles: how tall one row is, in pixels (default 60)"),
			PROP(gap,       Number, "tiles: pixels between tiles, and around them"),
		};

		constexpr PropDesc kDock[] = {
			PROP(title, String, "what its tab and title bar say"),
			PROP(col,  Number, "tiles: which column it starts in; unset means wherever it fits"),
			PROP(row,  Number, "tiles: which row it starts in; unset means wherever it fits"),
			PROP(cols, Number, "tiles: how many columns wide it starts"),
			PROP(rows, Number, "tiles: how many rows tall it starts"),
			PROP_ENUM(side, "floating|left|right|top|bottom|center",
			          "where it starts, before anyone moves it"),
			PROP(size, Number, "how wide or tall its pane starts, in pixels"),
			PROP(closable, Bool, "give it a close button"),
			PROP_VARIANT("dock"),
		};

		constexpr PropDesc kTextField[] = {
			PROP(text, String, "the contents"),
			PROP(placeholder, String, "what is shown when there is nothing in text"),
			PROP_ENUM(mode, "line|document|code",
			          "single line, a text area, or a code editor with a gutter"),
			PROP(suggestion, String, "a completion offered ahead of the caret; Tab takes it, Escape drops it"),
			PROP(language, String, "the syntax grammar to colour with, overriding the "
			                       "variant's -- the variant keeps the palette"),
			PROP_ENUM(submitKey, "enter|ctrlEnter|both|none",
			          "which keystroke runs onSubmit; Shift+Enter is always a line "
			          "break. Unset means Enter on a single line and Ctrl+Enter on a "
			          "multi-line one"),
			PROP_VARIANT("textfield"),
			PROP_EVENT_OF(onChange, "string", "runs on each edit, with the new contents"),
			PROP_EVENT_OF(onCaret, "number", "runs when the caret moves, with where it is in bytes"),
			PROP_EVENT(onAccept, "runs when Tab took the suggestion"),
			PROP_EVENT(onDismiss, "runs when Escape dropped it"),
			PROP_EVENT(onSubmit, "runs on the send keystroke -- see submitKey"),
		};

		constexpr PropDesc kPopup[] = {
			PROP(open,   Bool,   "showing or not; the layout owns it"),
			PROP(anchor, String, "the full id path of the widget to hang off -- "
			                     "\"root/toolbar/model\", not \"model\""),
			PROP_ENUM(placement, "below|above|right|left|over",
			          "which side of the anchor to sit on; flipped when it would fall off "
			          "the edge it is growing towards"),
			PROP(gap,     Number, "pixels between the anchor and this"),
			PROP(padding, Number, "inset around its children"),
			PROP(blocking, Bool,  "while it is open, nothing underneath hovers or clicks; "
			                      "off for a tooltip"),
			PROP_EVENT(onClose, "runs on a press outside it, or Escape"),
			PROP_VARIANT("panel"),
		};

		constexpr WidgetDesc kWidgets[] = {
			WIDGET(container,  kContainer,  "groups children without drawing anything"),
			WIDGET(panel,      kPanel,      "a framed background that clips its children"),
			WIDGET(popup,      kPopup,
			       "a panel over everything, beside the widget it names, that closes when "
			       "the pointer goes elsewhere"),
			WIDGET(stack,      kStack,      "lays children out in a row or a column"),
			WIDGET(scroll,     kScroll,
			       "a clipped window onto content bigger than itself; stacks its children"),
			WIDGET(splitter,   kSplitter,   "a draggable bar that resizes what is above it"),
			WIDGET(viewport,   kViewport,   "a surface the application draws into itself"),
			WIDGET(label,      kLabel,      "a line of text"),
			WIDGET(button,     kButton,     "a clickable button; may hold children instead of text"),
			WIDGET(checkbox,   kCheckbox,   "a labelled boolean toggle"),
			WIDGET(slider,     kSlider,     "a draggable value between two bounds"),
			WIDGET(textfield,  kTextField,  "an editable text field"),
			WIDGET(list,       kList,
			       "rows from a table, showing only as many widgets as fit"),
			WIDGET(image,      kImage,      "a picture from a file, or one the backend registered"),
			WIDGET(stream,     kStream,
			       "a picture that keeps arriving: a camera, a decoded video, a filter's output"),
			WIDGET(tabs,       kTabs,
			       "a row of titles, and the one page whose title is selected"),
			WIDGET(tab,        kTab,        "one page of a <tabs>; its children are the page"),
			WIDGET(select,     kSelect,     "a box that opens a list of choices"),
			WIDGET(option,     kOption,     "one choice in a <select>"),
			WIDGET(dockspace,  kDockSpace,
			       "an area whose children are movable, dockable panels"),
			WIDGET(dock,       kDock,
			       "one dockable panel; its children are what the panel holds"),
		};

		#undef PROP
		#undef PROP_EVENT_OF
		#undef PROP_EVENT
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
}
