#include "TestHarness.h"

#include <GraphicalObjects/Widget.h>
#include <Layout/WidgetRegistry.h>
#include <Layout/WidgetSchema.h>

#include <string>
#include <string_view>

// Layout/WidgetRegistry.h against Layout/WidgetSchema.h.
//
// The schema is what an author is told: the type definitions are generated from it, and
// the compiler checks a layout against it. The registry is what the engine does. Each
// widget's properties were once handled in several separate places, and those copies
// drifted apart: a property typed as bindable could compile and then be dropped at run
// time with a warning. These cases make that drift a failed build instead.

using namespace RDA;
using namespace RDA::Layout;

namespace {
	// Every gap, named, so a failure says which property and not just that one exists.
	std::string listed(const std::string& so_far, std::string_view widget, std::string_view prop) {
		return so_far + "\n          <" + std::string(widget) + "> " + std::string(prop);
	}

	template <class F>
	std::string everyProp(F&& check) {
		std::string gaps;
		size_t widgetCount = 0;
		const WidgetDesc* widgets = widgetSchema(widgetCount);
		size_t commonCount = 0;
		const PropDesc* common = commonProps(commonCount);
		for (size_t w = 0; w < widgetCount; ++w) {
			const WidgetDesc& widget = widgets[w];
			const WidgetKind* kind = findKind(widget.name);
			if (!kind) continue;   // the first case reports that
			for (size_t p = 0; p < widget.propCount; ++p) {
				if (!check(*kind, widget.props[p])) gaps = listed(gaps, widget.name, widget.props[p].name);
			}
			for (size_t p = 0; p < commonCount; ++p) {
				// A widget's own property of the same name is the one it means.
				if (findProp(widget, common[p].name) != &common[p]) continue;
				if (!check(*kind, common[p])) gaps = listed(gaps, widget.name, common[p].name);
			}
		}
		return gaps;
	}
}

TEST(every_element_the_schema_declares_has_a_record_and_the_other_way_round) {
	size_t widgetCount = 0;
	const WidgetDesc* widgets = widgetSchema(widgetCount);
	std::string missing;
	for (size_t i = 0; i < widgetCount; ++i) {
		if (!findKind(widgets[i].name)) missing += std::string(" <") + widgets[i].name + ">";
	}
	CHECK_STR(missing, "");

	size_t kindCount = 0;
	const WidgetKind* const* kinds = widgetKinds(kindCount);
	std::string undeclared;
	for (size_t i = 0; i < kindCount; ++i) {
		if (!findWidget(kinds[i]->element)) undeclared += " <" + std::string(kinds[i]->element) + ">";
		CHECK(kinds[i]->make != nullptr);
	}
	CHECK_STR(undeclared, "");
	CHECK_EQ(kindCount, widgetCount);
}

TEST(every_property_the_schema_calls_bindable_has_a_setter) {
	// The drift this exists for: typed as bindable, compiled, then dropped at run time.
	const std::string gaps = everyProp([](const WidgetKind& kind, const PropDesc& prop) {
		if (prop.type == PropType::Event || prop.constant) return true;
		return findSetter(kind, prop.name) != nullptr;
	});
	CHECK_STR(gaps, "");
}

TEST(a_property_the_schema_calls_constant_has_no_setter) {
	// The other half: a constant with a setter is a schema that undersells what works,
	// and the compiler would refuse a binding the engine could have honoured.
	const std::string gaps = everyProp([](const WidgetKind& kind, const PropDesc& prop) {
		if (!prop.constant) return true;
		return findSetter(kind, prop.name) == nullptr;
	});
	CHECK_STR(gaps, "");
}

TEST(every_event_the_schema_declares_can_be_listened_to) {
	const std::string gaps = everyProp([](const WidgetKind& kind, const PropDesc& prop) {
		if (prop.type != PropType::Event) return true;
		return hasEvent(kind, prop.name);
	});
	CHECK_STR(gaps, "");
}

TEST(a_record_answers_nothing_its_schema_does_not_declare) {
	// A setter for a name the schema never mentions is one an author cannot reach: the
	// compiler reports the name as unknown first. Only a kind's own entries are checked --
	// a tree inherits a list's, and declares the ones it means.
	size_t kindCount = 0;
	const WidgetKind* const* kinds = widgetKinds(kindCount);
	std::string extra;
	for (size_t i = 0; i < kindCount; ++i) {
		const WidgetKind& kind = *kinds[i];
		const WidgetDesc* widget = findWidget(kind.element);
		if (!widget) continue;
		for (size_t p = 0; p < kind.propCount; ++p) {
			if (!findProp(*widget, kind.props[p].name)) extra = listed(extra, kind.element, kind.props[p].name);
		}
		for (size_t e = 0; e < kind.eventCount; ++e) {
			if (!findProp(*widget, kind.events[e].name)) extra = listed(extra, kind.element, kind.events[e].name);
		}
	}
	CHECK_STR(extra, "");
}

TEST(a_tree_is_a_list_by_record_rather_than_by_cast) {
	const WidgetKind* tree = findKind("tree");
	const WidgetKind* list = findKind("list");
	CHECK(tree && list);
	if (!tree || !list) return;
	CHECK(tree->base == list);
	CHECK(tree->rowTemplate && list->rowTemplate);
	// Its own, and its list's, through the same lookup.
	CHECK(findSetter(*tree, "openDepth") != nullptr);
	CHECK(findSetter(*tree, "follow") == findSetter(*list, "follow"));
	CHECK(findSetter(*list, "openDepth") == nullptr);
}

TEST(a_bound_alignment_reads_the_words_a_constant_does) {
	// The loader and the binding runtime each had a parser, and they disagreed: "auto" was
	// an alignment to one and not the other, and a label's alignment was not bindable.
	Label label("l", "text");
	const PropSetter hAlign = findSetter(*findKind("label"), "hAlign");
	CHECK(hAlign != nullptr);
	if (!hAlign) return;
	hAlign(label, Value::fromText("center+20"));
	CHECK(label.hAlign == TextAlign::Center);
	CHECK_EQ(label.hAlignOffset, 20.0f);
	// A word this build does not know leaves it as it was, nudge and all.
	hAlign(label, Value::fromText("sideways"));
	CHECK(label.hAlign == TextAlign::Center);
	CHECK_EQ(label.hAlignOffset, 20.0f);

	Stack stack("s");
	const PropSetter self = findSetter(*findKind("stack"), "hAlignSelf");
	CHECK(self != nullptr);
	if (!self) return;
	stack.hAlignSelf = Alignment{ Align::Center, 0.0f };
	self(stack, Value::fromText("auto"));
	CHECK(stack.hAlignSelf.word == Align::Auto);
}

TEST(a_bound_arrange_the_stack_does_not_know_leaves_it_alone) {
	// The binding used to read anything but "horizontal" as vertical; the constant left an
	// unknown word alone. One parser now, and it is the constant's.
	Stack stack("s");
	const PropSetter arrange = findSetter(*findKind("stack"), "arrange");
	CHECK(arrange != nullptr);
	if (!arrange) return;
	arrange(stack, Value::fromText("horizontal"));
	CHECK(!stack.vertical);
	arrange(stack, Value::fromText("diagonal"));
	CHECK(!stack.vertical);
	arrange(stack, Value::fromText("vertical"));
	CHECK(stack.vertical);
}

TEST(a_handler_hangs_on_the_slot_its_record_names) {
	Button button("b", "go");
	int clicks = 0;
	CHECK(attachEvent(*findKind("button"), button, "onClick",
	                  [&clicks](const Value* passed) { if (!passed) ++clicks; }));
	CHECK(static_cast<bool>(button.onClick));
	if (button.onClick) button.onClick();
	CHECK_EQ(clicks, 1);

	// One every widget has, on a widget that has nothing else.
	bool hovered = false;
	CHECK(attachEvent(*findKind("button"), button, "onHover",
	                  [&hovered](const Value* passed) { hovered = passed && passed->truthy(); }));
	if (button.onHover) button.onHover(true);
	CHECK(hovered);

	// And none it does not.
	CHECK(!attachEvent(*findKind("label"), button, "onClick", [](const Value*) {}));
}
