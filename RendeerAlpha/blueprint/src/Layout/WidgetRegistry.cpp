#include <Layout/WidgetRegistry.h>
#include "Kinds/Kinds.h"
#include "Kinds/KindSupport.h"
#include <GraphicalObjects/Motion.h>
#include <iterator>

namespace RDA::Layout {

	using namespace Kinds;

	namespace {
		const WidgetKind* const kKinds[] = {
			&kContainerKind, &kPanelKind, &kStackKind, &kScrollKind, &kPopupKind,
			&kSplitterKind, &kTabsKind, &kTabKind,
			&kLabelKind, &kButtonKind, &kCheckboxKind, &kShortcutKind, &kSliderKind,
			&kNumberKind, &kTextFieldKind, &kSelectKind, &kOptionKind,
			&kImageKind, &kStreamKind, &kViewportKind,
			&kListKind, &kTreeKind, &kGridKind, &kColumnKind,
			&kDockSpaceKind, &kDockKind, &kHeaderKind,
		};

		// ---- what every widget takes ---------------------------------------------------

		void applyCommon(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			widget.rect = {
				bp.number(node, "x", 0.0f), bp.number(node, "y", 0.0f),
				bp.number(node, "w", 0.0f), bp.number(node, "h", 0.0f),
			};
			widget.visible      = bp.boolean(node, "visible", true);
			widget.animateMs    = bp.number(node, "animate", 0.0f);
			widget.dragWindow   = bp.boolean(node, "dragWindow", widget.dragWindow);
			widget.maximizeButton = bp.boolean(node, "maximizeButton", widget.maximizeButton);
			widget.alt          = std::string(bp.text(node, "alt"));
			widget.live         = liveFrom(bp.text(node, "live"));
			widget.drag         = std::string(bp.text(node, "drag"));
			widget.dragKind     = std::string(bp.text(node, "dragKind"));
			widget.accepts      = std::string(bp.text(node, "accepts"));
			widget.route        = std::string(bp.text(node, "route"));
			widget.pace         = paceFrom(std::string(bp.text(node, "pace")), widget.pace);
			widget.marginRight  = bp.number(node, "marginRight", 0.0f);
			widget.marginBottom = bp.number(node, "marginBottom", 0.0f);
			widget.hAlignSelf   = alignFrom(bp.text(node, "hAlignSelf"), Alignment{ Align::Auto, 0.0f });
			widget.vAlignSelf   = alignFrom(bp.text(node, "vAlignSelf"), Alignment{ Align::Auto, 0.0f });
			applySize(bp, node, "width",  "minWidth",  "maxWidth",  widget.width);
			applySize(bp, node, "height", "minHeight", "maxHeight", widget.height);

			if (const BlueprintProp* anchor = bp.find(node, "anchor")) {
				if (anchor->kind == static_cast<uint32_t>(PropKind::String)) {
					widget.anchor = anchorFrom(bp.string(anchor->text), widget.anchor);
				}
			}
		}

		float numberOf(const Value& v) { return static_cast<float>(v.asNumber()); }

		constexpr BoundProp kCommonProps[] = {
			{ "visible",        [](Widget& w, const Value& v) { w.visible = v.truthy(); } },
			{ "animate",        [](Widget& w, const Value& v) { w.animateMs = numberOf(v); } },
			{ "x",              [](Widget& w, const Value& v) { w.rect.x = numberOf(v); } },
			{ "y",              [](Widget& w, const Value& v) { w.rect.y = numberOf(v); } },
			{ "w",              [](Widget& w, const Value& v) { w.rect.w = numberOf(v); } },
			{ "h",              [](Widget& w, const Value& v) { w.rect.h = numberOf(v); } },
			{ "marginRight",    [](Widget& w, const Value& v) { w.marginRight = numberOf(v); } },
			{ "marginBottom",   [](Widget& w, const Value& v) { w.marginBottom = numberOf(v); } },
			{ "width",          [](Widget& w, const Value& v) { sizeFrom(v, w.width); } },
			{ "height",         [](Widget& w, const Value& v) { sizeFrom(v, w.height); } },
			{ "minWidth",       [](Widget& w, const Value& v) { w.width.min = numberOf(v); } },
			{ "maxWidth",       [](Widget& w, const Value& v) { w.width.max = numberOf(v); } },
			{ "minHeight",      [](Widget& w, const Value& v) { w.height.min = numberOf(v); } },
			{ "maxHeight",      [](Widget& w, const Value& v) { w.height.max = numberOf(v); } },
			// A child's own answer for an axis, which any of them may compute:
			// `hAlignSelf={() => state.narrow ? "center" : "start"}`.
			{ "hAlignSelf",     [](Widget& w, const Value& v) { w.hAlignSelf = alignFrom(v.text, w.hAlignSelf); } },
			{ "vAlignSelf",     [](Widget& w, const Value& v) { w.vAlignSelf = alignFrom(v.text, w.vAlignSelf); } },
			{ "anchor",         [](Widget& w, const Value& v) { w.anchor = anchorFrom(v.text, w.anchor); } },
			{ "dragWindow",     [](Widget& w, const Value& v) { w.dragWindow = v.truthy(); } },
			{ "maximizeButton", [](Widget& w, const Value& v) { w.maximizeButton = v.truthy(); } },
			{ "alt",            [](Widget& w, const Value& v) { w.alt = v.asText(); } },
			{ "live",           [](Widget& w, const Value& v) { w.live = liveFrom(v.text); } },
			{ "drag",           [](Widget& w, const Value& v) { w.drag = v.asText(); } },
			{ "dragKind",       [](Widget& w, const Value& v) { w.dragKind = v.asText(); } },
			{ "accepts",        [](Widget& w, const Value& v) { w.accepts = v.asText(); } },
			{ "route",          [](Widget& w, const Value& v) { w.route = v.asText(); } },
			{ "pace",           [](Widget& w, const Value& v) { w.pace = paceFrom(v.asText(), w.pace); } },
		};

		// On the base class, so on every widget. A drop target is a place, and anything that
		// takes up room can be one.
		constexpr EventSlot kCommonEvents[] = {
			{ "onDrop",     [](Widget& w, EventRunner run) { w.onDrop = withText(std::move(run)); } },
			{ "onFiles",    [](Widget& w, EventRunner run) { w.onFiles = withText(std::move(run)); } },
			{ "onDragOver", [](Widget& w, EventRunner run) { w.onDragOver = withBool(std::move(run)); } },
			// Claimed during the walk rather than called there -- see Gui::noteContext.
			{ "onContext",  [](Widget& w, EventRunner run) { w.onContext = plain(std::move(run)); } },
			// Reported from placement(), for the pointer arriving and leaving.
			{ "onHover",    [](Widget& w, EventRunner run) { w.onHover = withBool(std::move(run)); } },
		};

		PropSetter setterIn(const BoundProp* props, size_t count, std::string_view name) {
			for (size_t i = 0; i < count; ++i) {
				if (props[i].name == name) return props[i].set;
			}
			return nullptr;
		}

		const EventSlot* slotIn(const EventSlot* events, size_t count, std::string_view name) {
			for (size_t i = 0; i < count; ++i) {
				if (events[i].name == name) return &events[i];
			}
			return nullptr;
		}

		// The kind's own first, so a kind can answer a name every widget has in its own
		// way -- a popup's `anchor` is what it hangs off, not which edges it follows.
		const EventSlot* findSlot(const WidgetKind& kind, std::string_view event) {
			for (const WidgetKind* k = &kind; k; k = k->base) {
				if (const EventSlot* slot = slotIn(k->events, k->eventCount, event)) return slot;
			}
			return slotIn(kCommonEvents, std::size(kCommonEvents), event);
		}
	}

	const WidgetKind* findKind(std::string_view element) {
		for (const WidgetKind* kind : kKinds) {
			if (kind->element == element) return kind;
		}
		return nullptr;
	}

	const WidgetKind* const* widgetKinds(size_t& count) {
		count = std::size(kKinds);
		return kKinds;
	}

	std::unique_ptr<Widget> buildWidget(const WidgetKind& kind, const Blueprint& bp,
	                                    const BlueprintNode& node, const std::string& id) {
		if (!kind.make) return nullptr;
		std::unique_ptr<Widget> widget = kind.make(bp, node, id);
		if (!widget) return nullptr;
		applyCommon(bp, node, *widget);
		if (kind.applyConst) kind.applyConst(bp, node, *widget);
		return widget;
	}

	PropSetter findSetter(const WidgetKind& kind, std::string_view property) {
		for (const WidgetKind* k = &kind; k; k = k->base) {
			if (const PropSetter set = setterIn(k->props, k->propCount, property)) return set;
		}
		return setterIn(kCommonProps, std::size(kCommonProps), property);
	}

	bool hasEvent(const WidgetKind& kind, std::string_view event) {
		return findSlot(kind, event) != nullptr;
	}

	bool attachEvent(const WidgetKind& kind, Widget& widget, std::string_view event, EventRunner run) {
		const EventSlot* slot = findSlot(kind, event);
		if (!slot) return false;
		slot->attach(widget, std::move(run));
		return true;
	}
}
