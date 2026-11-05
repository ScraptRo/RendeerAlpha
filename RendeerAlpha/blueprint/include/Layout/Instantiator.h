#pragma once
#include <Layout/Blueprint.h>
#include <string>
#include <utility>
#include <vector>

namespace RDA {
	class Widget;
	class ListView;
}

// A blueprint's nodes, turned into widgets.
//
// The blueprint layer's half of loading a layout: each node built by the record for its
// element, its constant properties applied, and what is wrong with a node said once. What
// it does not do is bind anything. Bindings, handlers and a list's rows are the layout
// runtime's -- Layout/LayoutLoader.h -- which walks a blueprint through this and then
// attaches what it is to follow.
namespace RDA::Layout {

	struct WidgetKind;

	// What a walk over a blueprint made, indexed by node.
	struct Walk {
		std::vector<Widget*>           made;
		// Which record built each one, which is how a binding finds its setter and a
		// handler its slot without asking the widget what it is.
		std::vector<const WidgetKind*> kinds;
		Widget*                        root = nullptr;
		// Lists met, and where each one's template starts. Only a layout's walk meets
		// them: a row is built from a template rather than holding one.
		std::vector<std::pair<ListView*, size_t>> templates;
	};

	// The one walk over a blueprint's nodes, for a whole layout and for one pooled row.
	// A layout's walk starts at the first node and puts each node with no parent into
	// `top`; a row's starts at its template's root, which goes into the list, and takes only
	// what descends from it.
	Walk walkNodes(const Blueprint& bp, size_t first, RDA::Widget& top, const std::string& idPrefix,
	               bool row);
}
