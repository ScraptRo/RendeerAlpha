#pragma once
#include <Layout/Blueprint.h>
#include <string>

namespace RDA {
	class Widget;
}

// Turning a compiled blueprint into live widgets.
//
// This is the half of the layout pipeline that ships. It contains no parser, no
// JavaScript engine and no knowledge of TypeScript — it walks a POD array and calls
// constructors. That is the whole reason the compiler and the runtime are separate
// tiers: an application links this, and links nothing that could produce a blueprint.
//
// The walk is a single forward loop rather than a recursion, because the blueprint
// stores nodes breadth-first and Blueprint::parse() has already established that every
// parent appears before its children.
namespace RDA::Layout {

	// Builds `blueprint` under `parent` and returns the subtree root, or nullptr if the
	// blueprint is empty or its root names a widget type this build does not have.
	//
	// `idPrefix` is prepended to every id in the blueprint, so instantiating the same
	// layout twice gives two sets of widgets that do not share interaction state — the
	// same reason GuiComponents::instantiate() takes an instance id. Leave it empty and
	// the ids are used as authored.
	//
	// Widgets whose type is not known are skipped along with their children, and the
	// rest of the tree is still built: a layout compiled against a newer engine loads
	// as much of itself as this one understands rather than nothing at all.
	Widget* instantiate(const Blueprint& blueprint, Widget& parent,
	                    const std::string& idPrefix = {});

	// Whether this build can make a widget of that type. Useful for reporting what a
	// blueprint would lose before instantiating it.
	bool knowsWidgetType(std::string_view type);
}
