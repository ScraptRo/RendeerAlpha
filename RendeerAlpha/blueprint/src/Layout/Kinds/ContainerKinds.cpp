#include "Kinds.h"
#include "KindSupport.h"
#include <iterator>

// The widgets that hold others: <container>, <panel>, <stack>, <scroll>, <popup>,
// <splitter>, <tabs> and <tab>.
namespace RDA::Layout::Kinds {

	namespace {
		// ---- container ---------------------------------------------------------------
		std::unique_ptr<Widget> makeContainer(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Container>(id);
		}

		// ---- panel -------------------------------------------------------------------
		std::unique_ptr<Widget> makePanel(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Panel>(id);
		}
		void constPanel(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			variantFrom(bp, node, as<Panel>(widget).variant);
		}
		constexpr BoundProp kPanelProps[] = {
			{ "variant", [](Widget& w, const Value& v) { as<Panel>(w).variant = Variant(std::string_view(v.text)); } },
		};

		// ---- stack -------------------------------------------------------------------
		std::unique_ptr<Widget> makeStack(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Stack>(id);
		}
		void constStack(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Stack& w = as<Stack>(widget);
			w.vertical = verticalFrom(bp.text(node, "arrange"), w.vertical);
			w.spacing  = bp.number(node, "spacing", 4.0f);
			w.padding  = bp.number(node, "padding", 0.0f);
			w.hAlign   = alignFrom(bp.text(node, "hAlign"), w.hAlign);
			w.vAlign   = alignFrom(bp.text(node, "vAlign"), w.vAlign);
			w.spread   = spreadFrom(bp.text(node, "spread"), w.spread);
		}
		constexpr BoundProp kStackProps[] = {
			{ "arrange", [](Widget& w, const Value& v) { Stack& s = as<Stack>(w); s.vertical = verticalFrom(v.text, s.vertical); } },
			{ "spacing", [](Widget& w, const Value& v) { as<Stack>(w).spacing = static_cast<float>(v.asNumber()); } },
			{ "padding", [](Widget& w, const Value& v) { as<Stack>(w).padding = static_cast<float>(v.asNumber()); } },
			{ "hAlign",  [](Widget& w, const Value& v) { Stack& s = as<Stack>(w); s.hAlign = alignFrom(v.text, s.hAlign); } },
			{ "vAlign",  [](Widget& w, const Value& v) { Stack& s = as<Stack>(w); s.vAlign = alignFrom(v.text, s.vAlign); } },
			{ "spread",  [](Widget& w, const Value& v) { Stack& s = as<Stack>(w); s.spread = spreadFrom(v.text, s.spread); } },
		};

		// ---- scroll ------------------------------------------------------------------
		std::unique_ptr<Widget> makeScroll(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Scroll>(id);
		}
		void constScroll(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Scroll& w = as<Scroll>(widget);
			variantFrom(bp, node, w.variant);
			w.vertical   = bp.boolean(node, "vertical", w.vertical);
			w.horizontal = bp.boolean(node, "horizontal", w.horizontal);
			w.spacing    = bp.number(node, "spacing", w.spacing);
			w.padding    = bp.number(node, "padding", w.padding);
			w.hAlign     = alignFrom(bp.text(node, "hAlign"), w.hAlign);
			w.barWidth   = bp.number(node, "barWidth", w.barWidth);
			w.wheelStep  = bp.number(node, "wheelStep", w.wheelStep);
		}
		constexpr BoundProp kScrollProps[] = {
			{ "variant",    [](Widget& w, const Value& v) { as<Scroll>(w).variant = Variant(std::string_view(v.text)); } },
			{ "vertical",   [](Widget& w, const Value& v) { as<Scroll>(w).vertical = v.truthy(); } },
			{ "horizontal", [](Widget& w, const Value& v) { as<Scroll>(w).horizontal = v.truthy(); } },
			{ "spacing",    [](Widget& w, const Value& v) { as<Scroll>(w).spacing = static_cast<float>(v.asNumber()); } },
			{ "padding",    [](Widget& w, const Value& v) { as<Scroll>(w).padding = static_cast<float>(v.asNumber()); } },
			{ "hAlign",     [](Widget& w, const Value& v) { Scroll& s = as<Scroll>(w); s.hAlign = alignFrom(v.text, s.hAlign); } },
			{ "barWidth",   [](Widget& w, const Value& v) { as<Scroll>(w).barWidth = static_cast<float>(v.asNumber()); } },
			{ "wheelStep",  [](Widget& w, const Value& v) { as<Scroll>(w).wheelStep = static_cast<float>(v.asNumber()); } },
		};

		// ---- popup -------------------------------------------------------------------
		std::unique_ptr<Widget> makePopup(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Popup>(id);
		}
		void constPopup(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Popup& w = as<Popup>(widget);
			variantFrom(bp, node, w.variant);
			w.open      = bp.boolean(node, "open", w.open);
			// Its own `anchor`, the id of what it hangs off, rather than the edges every
			// other widget follows.
			w.anchor    = std::string(bp.text(node, "anchor"));
			w.gap       = bp.number(node, "gap", w.gap);
			w.padding   = bp.number(node, "padding", w.padding);
			w.blocking  = bp.boolean(node, "blocking", w.blocking);
			w.placement = popupPlacementFrom(bp.text(node, "placement"), w.placement);
		}
		constexpr BoundProp kPopupProps[] = {
			{ "open",      [](Widget& w, const Value& v) { as<Popup>(w).open = v.truthy(); } },
			{ "anchor",    [](Widget& w, const Value& v) { as<Popup>(w).anchor = v.asText(); } },
			{ "blocking",  [](Widget& w, const Value& v) { as<Popup>(w).blocking = v.truthy(); } },
			{ "gap",       [](Widget& w, const Value& v) { as<Popup>(w).gap = static_cast<float>(v.asNumber()); } },
			{ "padding",   [](Widget& w, const Value& v) { as<Popup>(w).padding = static_cast<float>(v.asNumber()); } },
			{ "placement", [](Widget& w, const Value& v) { Popup& p = as<Popup>(w); p.placement = popupPlacementFrom(v.text, p.placement); } },
			{ "variant",   [](Widget& w, const Value& v) { as<Popup>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kPopupEvents[] = {
			{ "onClose", [](Widget& w, EventRunner run) { as<Popup>(w).onClose = plain(std::move(run)); } },
		};

		// ---- splitter ----------------------------------------------------------------
		std::unique_ptr<Widget> makeSplitter(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Splitter>(id);
		}
		void constSplitter(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Splitter& w = as<Splitter>(widget);
			w.vertical = verticalFrom(bp.text(node, "arrange"), w.vertical);
			w.minValue = bp.number(node, "min", 40.0f);
			w.maxValue = bp.number(node, "max", 4000.0f);
			w.value    = bp.number(node, "value", 120.0f);
		}
		constexpr BoundProp kSplitterProps[] = {
			{ "arrange", [](Widget& w, const Value& v) { Splitter& s = as<Splitter>(w); s.vertical = verticalFrom(v.text, s.vertical); } },
			{ "value",   [](Widget& w, const Value& v) { as<Splitter>(w).value = static_cast<float>(v.asNumber()); } },
			{ "min",     [](Widget& w, const Value& v) { as<Splitter>(w).minValue = static_cast<float>(v.asNumber()); } },
			{ "max",     [](Widget& w, const Value& v) { as<Splitter>(w).maxValue = static_cast<float>(v.asNumber()); } },
		};

		// ---- tabs and tab ------------------------------------------------------------
		std::unique_ptr<Widget> makeTabs(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Tabs>(id);
		}
		void constTabs(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Tabs& w = as<Tabs>(widget);
			variantFrom(bp, node, w.variant);
			w.value = static_cast<int>(bp.number(node, "value", 0.0f));
			w.barHeight = bp.number(node, "barHeight", 0.0f);
		}
		constexpr BoundProp kTabsProps[] = {
			{ "value",     [](Widget& w, const Value& v) { as<Tabs>(w).value = static_cast<int>(v.asNumber()); } },
			{ "barHeight", [](Widget& w, const Value& v) { as<Tabs>(w).barHeight = static_cast<float>(v.asNumber()); } },
			{ "variant",   [](Widget& w, const Value& v) { as<Tabs>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kTabsEvents[] = {
			{ "onChange", [](Widget& w, EventRunner run) { as<Tabs>(w).onChange = withNumber(std::move(run)); } },
		};

		std::unique_ptr<Widget> makeTab(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<Tab>(id, std::string(bp.text(node, "title")));
		}
		constexpr BoundProp kTabProps[] = {
			{ "title", [](Widget& w, const Value& v) { as<Tab>(w).title = v.asText(); } },
		};
	}

	const WidgetKind kContainerKind{ "container", nullptr, &makeContainer };
	const WidgetKind kPanelKind{ "panel", nullptr, &makePanel, &constPanel,
	                                    kPanelProps, std::size(kPanelProps) };
	const WidgetKind kStackKind{ "stack", nullptr, &makeStack, &constStack,
	                                    kStackProps, std::size(kStackProps) };
	const WidgetKind kScrollKind{ "scroll", nullptr, &makeScroll, &constScroll,
	                                     kScrollProps, std::size(kScrollProps) };
	const WidgetKind kPopupKind{ "popup", nullptr, &makePopup, &constPopup,
	                                    kPopupProps, std::size(kPopupProps),
	                                    kPopupEvents, std::size(kPopupEvents) };
	const WidgetKind kSplitterKind{ "splitter", nullptr, &makeSplitter, &constSplitter,
	                                       kSplitterProps, std::size(kSplitterProps) };
	const WidgetKind kTabsKind{ "tabs", nullptr, &makeTabs, &constTabs,
	                                   kTabsProps, std::size(kTabsProps),
	                                   kTabsEvents, std::size(kTabsEvents) };
	const WidgetKind kTabKind{ "tab", nullptr, &makeTab, nullptr,
	                                  kTabProps, std::size(kTabProps) };
}
