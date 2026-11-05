#pragma once
#include <Layout/Blueprint.h>
#include <Layout/Expression.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace RDA {
	class Widget;
}

// How the blueprint layer meets each kind of widget, written once per kind.
//
// An element in a layout -- <button>, <list> -- becomes a widget in four steps. It is built,
// its constant properties are applied, its bound ones are written whenever their value
// moves, and its handlers are attached. Each step used to be its own chain of
// dynamic_casts, in three files, and the chains drifted. A property could be typed as
// bindable, compile, and then be dropped at run time with a warning, because one chain had
// a branch the other lacked. Whether a subclass took its parent's branch depended on which
// came first.
//
// Here each kind is one record, and the element name finds it. Nothing is cast to find out
// what a widget is. The loader knows which element made each widget, so it looks the
// record up then and keeps what it found. A binding holds the setter for its property,
// resolved once when the layout loads, rather than comparing strings every time a value
// moves. A property with no setter is known at load time, and a test checks that every
// property the schema calls bindable has one.
namespace RDA::Layout {

	// Writes a computed value into one property of one kind of widget. Found once, when the
	// layout loads, from the record for the element that made the widget, so applying a
	// value compares no names.
	using PropSetter = void (*)(RDA::Widget& widget, const Value& value);

	// What a handler runs: the value the widget passed, or nullptr for an event that
	// carries none.
	using EventRunner = std::function<void(const Value* passed)>;

	// A property a binding can drive.
	struct BoundProp {
		std::string_view name;
		PropSetter       set;
	};

	// An event a handler can be attached to.
	struct EventSlot {
		std::string_view name;
		void (*attach)(Widget& widget, EventRunner run);
	};

	struct WidgetKind {
		std::string_view element;
		// The kind this one extends, asked for whatever it does not answer itself: a tree
		// is a list. Spelled out rather than discovered, so a new kind cannot inherit
		// another's properties by accident.
		const WidgetKind* base = nullptr;

		// Builds one. Widgets whose text is a constructor argument read it here.
		std::unique_ptr<Widget> (*make)(const Blueprint& bp, const BlueprintNode& node,
		                                const std::string& id) = nullptr;
		// The kind's own constant properties, after the ones every widget takes. A kind
		// with a base calls the base's itself, where it wants it.
		void (*applyConst)(const Blueprint& bp, const BlueprintNode& node, Widget& widget) = nullptr;

		const BoundProp* props = nullptr;
		size_t           propCount = 0;
		const EventSlot* events = nullptr;
		size_t           eventCount = 0;

		// Its child is not a child but the template every pooled row is built from.
		bool rowTemplate = false;
	};

	// The record for an element, or nullptr when this build has no such widget.
	const WidgetKind* findKind(std::string_view element);

	// Every kind, in no particular order. For the check that holds this and the schema
	// together.
	const WidgetKind* const* widgetKinds(size_t& count);

	// Built, with every constant property applied: what the loader does for each node.
	// Unknown properties are not reported here; the loader does that against the schema.
	std::unique_ptr<Widget> buildWidget(const WidgetKind& kind, const Blueprint& bp,
	                                    const BlueprintNode& node, const std::string& id);

	// The setter for `property` on a widget `kind` built: its own, then its base's, then
	// the ones every widget has. nullptr when nothing there can be driven by a binding.
	PropSetter findSetter(const WidgetKind& kind, std::string_view property);

	// Whether `kind` has `event`: its own, its base's, or one every widget has.
	bool hasEvent(const WidgetKind& kind, std::string_view event);

	// Attaches `run` to `event` on `widget`, which `kind` built. False when it has no such
	// event.
	bool attachEvent(const WidgetKind& kind, Widget& widget, std::string_view event, EventRunner run);
}
