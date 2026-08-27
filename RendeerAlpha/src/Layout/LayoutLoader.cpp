#include <Layout/LayoutLoader.h>
#include <Layout/WidgetSchema.h>
#include <GraphicalObjects/Widget.h>
#include <Logger/Logger.h>
#include <cstdlib>
#include <memory>
#include <vector>

namespace RDA::Layout {

	namespace {
		// A size prop is either a number (fixed), or one of the words a layout container
		// understands. Same vocabulary as the XML components already accept, so the two
		// front ends describe sizing identically rather than nearly identically.
		//
		// `spec` comes in holding whatever the widget set for itself and is only written
		// where the blueprint actually says something, so a widget type that grows its
		// own default sizing keeps it instead of having it overwritten with zero.
		void applySize(const Blueprint& bp, const BlueprintNode& node, std::string_view key,
		               std::string_view minKey, std::string_view maxKey, SizeSpec& spec) {
			if (const BlueprintProp* p = bp.find(node, key)) {
				if (p->kind == static_cast<uint32_t>(PropKind::String)) {
					const std::string_view word = bp.string(p->text);
					if (word == "content") {
						spec = SizeSpec::content();
					} else if (word.rfind("fill", 0) == 0) {
						// "fill" or "fill:<weight>"
						float weight = 1.0f;
						if (word.size() > 5 && word[4] == ':') {
							weight = std::strtof(std::string(word.substr(5)).c_str(), nullptr);
							if (weight <= 0.0f) weight = 1.0f;
						}
						spec = SizeSpec::fill(weight);
					}
				} else {
					spec = SizeSpec::fixed(p->number);
				}
			}
			spec.min = bp.number(node, minKey, spec.min);
			spec.max = bp.number(node, maxKey, spec.max);
		}

		std::unique_ptr<Widget> makeWidget(const Blueprint& bp, const BlueprintNode& node,
		                                   const std::string& id) {
			const std::string_view type = bp.string(node.type);

			// Widgets whose text is a constructor argument read it here; everything else
			// is applied below, once, for every type.
			if (type == "container") return std::make_unique<Container>(id);
			if (type == "panel")     return std::make_unique<Panel>(id);
			if (type == "stack")     return std::make_unique<Stack>(id);
			if (type == "scrollview")return std::make_unique<ScrollView>(id);
			if (type == "splitter")  return std::make_unique<Splitter>(id);
			if (type == "viewport")  return std::make_unique<Viewport>(id);
			if (type == "label")     return std::make_unique<Label>(id, std::string(bp.text(node, "text")));
			if (type == "button")    return std::make_unique<Button>(id, std::string(bp.text(node, "text")));
			if (type == "checkbox")  return std::make_unique<Checkbox>(id, std::string(bp.text(node, "label")));
			if (type == "slider")    return std::make_unique<Slider>(id);
			if (type == "textfield") {
				const std::string_view mode = bp.text(node, "mode", "line");
				TextFieldMode m = TextFieldMode::Line;
				if (mode == "document") m = TextFieldMode::Document;
				else if (mode == "code") m = TextFieldMode::Code;
				return std::make_unique<TextField>(id, m);
			}
			return nullptr;
		}

		void applyCommon(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			widget.rect = {
				bp.number(node, "x", 0.0f), bp.number(node, "y", 0.0f),
				bp.number(node, "w", 0.0f), bp.number(node, "h", 0.0f),
			};
			widget.visible      = bp.boolean(node, "visible", true);
			widget.marginRight  = bp.number(node, "marginRight", 0.0f);
			widget.marginBottom = bp.number(node, "marginBottom", 0.0f);
			applySize(bp, node, "width",  "minWidth",  "maxWidth",  widget.width);
			applySize(bp, node, "height", "minHeight", "maxHeight", widget.height);

			if (const BlueprintProp* anchor = bp.find(node, "anchor")) {
				if (anchor->kind == static_cast<uint32_t>(PropKind::String)) {
					const std::string_view a = bp.string(anchor->text);
					if      (a == "fill")        widget.anchor = Anchor::Fill;
					else if (a == "stretchX")    widget.anchor = Anchor::StretchX;
					else if (a == "stretchY")    widget.anchor = Anchor::StretchY;
					else if (a == "bottomLeft")  widget.anchor = Anchor::BottomLeft;
					else if (a == "bottomRight") widget.anchor = Anchor::BottomRight;
				}
			}
		}

		// Per-type properties. Kept apart from applyCommon so adding a widget type means
		// touching two obvious places rather than hunting through one long function.
		void applyTyped(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			const std::string_view type = bp.string(node.type);
			const std::string_view variant = bp.text(node, "variant");

			if (auto* w = dynamic_cast<Panel*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
			} else if (auto* w = dynamic_cast<Label*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
			} else if (auto* w = dynamic_cast<Button*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
			} else if (auto* w = dynamic_cast<Checkbox*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->value = bp.boolean(node, "value", false);
			} else if (auto* w = dynamic_cast<Slider*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->minValue = bp.number(node, "min", 0.0f);
				w->maxValue = bp.number(node, "max", 1.0f);
				w->value    = bp.number(node, "value", 0.0f);
			} else if (auto* w = dynamic_cast<TextField*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->text = std::string(bp.text(node, "text"));
			} else if (auto* w = dynamic_cast<Stack*>(&widget)) {
				w->vertical = bp.boolean(node, "vertical", true);
				w->spacing  = bp.number(node, "spacing", 4.0f);
				w->padding  = bp.number(node, "padding", 0.0f);
			} else if (auto* w = dynamic_cast<ScrollView*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->spacing = bp.number(node, "spacing", 0.0f);
				w->padding = bp.number(node, "padding", 0.0f);
			} else if (auto* w = dynamic_cast<Splitter*>(&widget)) {
				w->vertical = bp.boolean(node, "vertical", false);
				w->minValue = bp.number(node, "min", 40.0f);
				w->maxValue = bp.number(node, "max", 4000.0f);
				w->value    = bp.number(node, "value", 120.0f);
			}
			(void)type;
		}

		// A prop nobody applies is the quietest kind of bug: a misspelled `varaint` looks
		// exactly like a widget that refused to be styled. The schema knows what each
		// widget takes, so the mismatch can be named instead of shrugged at.
		//
		// It is a warning rather than a failure on purpose — a layout compiled against a
		// newer engine should still load here, minus whatever this build has never heard
		// of. Type checking at authoring time is where a typo is meant to be caught; this
		// is the backstop for everything that skipped it.
		void reportUnknownProps(const Blueprint& bp, const BlueprintNode& node) {
			const std::string_view type = bp.string(node.type);
			const WidgetDesc* widget = findWidget(type);
			if (!widget) return;

			for (uint32_t i = 0; i < node.propCount; ++i) {
				const std::string_view name = bp.string(bp.prop(node.firstProp + i).key);
				if (findProp(*widget, name)) continue;
				RDA_LOG_WARNING("layout: <" << type << "> has no property '" << name
				                << "' (id '" << bp.string(node.id) << "') - ignored");
			}
		}
	}

	bool knowsWidgetType(std::string_view type) {
		return findWidget(type) != nullptr;
	}

	Widget* instantiate(const Blueprint& blueprint, Widget& parent, const std::string& idPrefix) {
		if (!blueprint.valid() || blueprint.nodeCount() == 0) return nullptr;

		// Where each node's widget ended up, indexed by node. Parents are always earlier
		// in the array (parse() checks it), so by the time a child is reached its parent
		// is already in here — which is what makes this one forward pass.
		std::vector<Widget*> made(blueprint.nodeCount(), nullptr);
		Widget* root = nullptr;

		for (size_t i = 0; i < blueprint.nodeCount(); ++i) {
			const BlueprintNode& node = blueprint.node(i);

			// A node whose parent was skipped is skipped too, which is how an unknown
			// widget takes its subtree with it instead of reparenting it somewhere odd.
			Widget* into = (node.parent == kNoParent) ? &parent : made[node.parent];
			if (!into) continue;

			auto widget = makeWidget(blueprint, node, idPrefix + std::string(blueprint.string(node.id)));
			if (!widget) {
				RDA_LOG_WARNING("layout: no widget type named '" << blueprint.string(node.type)
				                << "' (id '" << blueprint.string(node.id) << "'), skipping it and its children");
				continue;
			}

			applyCommon(blueprint, node, *widget);
			applyTyped(blueprint, node, *widget);
			reportUnknownProps(blueprint, node);

			made[i] = into->addChild(std::move(widget));
			if (!root) root = made[i];
		}

		return root;
	}
}
