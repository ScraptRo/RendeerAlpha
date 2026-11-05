#include "Kinds.h"
#include "KindSupport.h"
#include <Layout/GridView.h>
#include <Layout/ListView.h>
#include <Layout/TreeView.h>
#include <iterator>

// The widgets that show a table: <list>, <tree>, <grid> and the <column>s of a grid.
//
// What each reads once rather than every frame -- the table it shows, the columns a row
// is known by, how many rows to build ahead -- takes a constant only, and the schema says
// so: a binding there compiles to an error instead of loading and then being ignored.
namespace RDA::Layout::Kinds {

	namespace {
		// ---- list --------------------------------------------------------------------
		std::unique_ptr<Widget> makeList(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<ListView>(id);
		}
		void constList(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			ListView& w = as<ListView>(widget);
			variantFrom(bp, node, w.variant);
			w.of             = std::string(bp.text(node, "of"));
			w.rowHeight      = bp.number(node, "rowHeight", w.rowHeight);
			w.rowHeights     = std::string(bp.text(node, "rowHeights"));
			w.spacing        = bp.number(node, "spacing", w.spacing);
			w.barWidth       = bp.number(node, "barWidth", w.barWidth);
			w.wheelStep      = bp.number(node, "wheelStep", w.wheelStep);
			w.follow         = bp.boolean(node, "follow", w.follow);
			w.revealRow      = static_cast<int>(bp.number(node, "revealRow", -1.0f));
			w.sortBy         = std::string(bp.text(node, "sortBy"));
			w.sortDescending = bp.boolean(node, "sortDescending", false);
			w.filter         = std::string(bp.text(node, "filter"));
			w.filterColumns  = std::string(bp.text(node, "filterColumns"));
		}
		constexpr BoundProp kListProps[] = {
			{ "rowHeight",      [](Widget& w, const Value& v) { as<ListView>(w).rowHeight = static_cast<float>(v.asNumber()); } },
			{ "spacing",        [](Widget& w, const Value& v) { as<ListView>(w).spacing = static_cast<float>(v.asNumber()); } },
			{ "barWidth",       [](Widget& w, const Value& v) { as<ListView>(w).barWidth = static_cast<float>(v.asNumber()); } },
			{ "wheelStep",      [](Widget& w, const Value& v) { as<ListView>(w).wheelStep = static_cast<float>(v.asNumber()); } },
			{ "follow",         [](Widget& w, const Value& v) { as<ListView>(w).follow = v.truthy(); } },
			{ "revealRow",      [](Widget& w, const Value& v) { as<ListView>(w).revealRow = static_cast<int>(v.asNumber()); } },
			{ "sortBy",         [](Widget& w, const Value& v) { as<ListView>(w).sortBy = v.asText(); } },
			{ "sortDescending", [](Widget& w, const Value& v) { as<ListView>(w).sortDescending = v.truthy(); } },
			{ "filter",         [](Widget& w, const Value& v) { as<ListView>(w).filter = v.asText(); } },
			{ "filterColumns",  [](Widget& w, const Value& v) { as<ListView>(w).filterColumns = v.asText(); } },
			{ "variant",        [](Widget& w, const Value& v) { as<ListView>(w).variant = Variant(std::string_view(v.text)); } },
		};

		// ---- tree --------------------------------------------------------------------
		std::unique_ptr<Widget> makeTree(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<TreeView>(id);
		}
		// A tree is a list, so it takes all of a list's properties as well, and then its own.
		void constTree(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			TreeView& tree = as<TreeView>(widget);
			const std::string_view depth = bp.text(node, "depthColumn");
			const std::string_view key = bp.text(node, "keyColumn");
			const std::string_view kids = bp.text(node, "childrenColumn");
			if (!depth.empty()) tree.depthColumn = std::string(depth);
			if (!key.empty())   tree.keyColumn = std::string(key);
			if (!kids.empty())  tree.childrenColumn = std::string(kids);
			tree.indent    = bp.number(node, "indent", tree.indent);
			tree.openDepth = static_cast<int>(bp.number(node, "openDepth", static_cast<float>(tree.openDepth)));
			tree.selected  = std::string(bp.text(node, "selected"));
			tree.multiSelect = bp.boolean(node, "multiSelect", false);
			tree.setSelectedKeys(bp.text(node, "selectedKeys"));
			constList(bp, node, widget);
		}
		constexpr BoundProp kTreeProps[] = {
			{ "selected",     [](Widget& w, const Value& v) { as<TreeView>(w).selected = v.asText(); } },
			{ "selectedKeys", [](Widget& w, const Value& v) { as<TreeView>(w).setSelectedKeys(v.asText()); } },
			{ "multiSelect",  [](Widget& w, const Value& v) { as<TreeView>(w).multiSelect = v.truthy(); } },
			{ "openDepth",    [](Widget& w, const Value& v) { as<TreeView>(w).openDepth = static_cast<int>(v.asNumber()); } },
			{ "indent",       [](Widget& w, const Value& v) { as<TreeView>(w).indent = static_cast<float>(v.asNumber()); } },
		};
		constexpr EventSlot kTreeEvents[] = {
			{ "onSelect",          [](Widget& w, EventRunner run) { as<TreeView>(w).onSelect = withText(std::move(run)); } },
			{ "onActivate",        [](Widget& w, EventRunner run) { as<TreeView>(w).onActivate = withText(std::move(run)); } },
			{ "onExpand",          [](Widget& w, EventRunner run) { as<TreeView>(w).onExpand = withText(std::move(run)); } },
			{ "onCollapse",        [](Widget& w, EventRunner run) { as<TreeView>(w).onCollapse = withText(std::move(run)); } },
			{ "onSelectionChange", [](Widget& w, EventRunner run) { as<TreeView>(w).onSelectionChange = withText(std::move(run)); } },
		};

		// ---- grid --------------------------------------------------------------------
		std::unique_ptr<Widget> makeGrid(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<GridView>(id);
		}
		void constGrid(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			GridView& w = as<GridView>(widget);
			variantFrom(bp, node, w.variant);
			w.of = std::string(bp.text(node, "of"));
			const std::string_view key = bp.text(node, "keyColumn");
			if (!key.empty()) w.keyColumn = std::string(key);
			w.selected       = std::string(bp.text(node, "selected"));
			w.multiSelect    = bp.boolean(node, "multiSelect", false);
			w.setSelectedKeys(bp.text(node, "selectedKeys"));
			w.sortBy         = std::string(bp.text(node, "sortBy"));
			w.sortDescending = bp.boolean(node, "sortDescending", false);
			w.filter         = std::string(bp.text(node, "filter"));
			w.filterColumns  = std::string(bp.text(node, "filterColumns"));
			w.rowHeight      = bp.number(node, "rowHeight", w.rowHeight);
			w.headerHeight   = bp.number(node, "headerHeight", w.headerHeight);
			w.barWidth       = bp.number(node, "barWidth", w.barWidth);
			w.wheelStep      = bp.number(node, "wheelStep", w.wheelStep);
		}
		constexpr BoundProp kGridProps[] = {
			{ "selected",       [](Widget& w, const Value& v) { as<GridView>(w).selected = v.asText(); } },
			{ "selectedKeys",   [](Widget& w, const Value& v) { as<GridView>(w).setSelectedKeys(v.asText()); } },
			{ "multiSelect",    [](Widget& w, const Value& v) { as<GridView>(w).multiSelect = v.truthy(); } },
			{ "sortBy",         [](Widget& w, const Value& v) { as<GridView>(w).sortBy = v.asText(); } },
			{ "sortDescending", [](Widget& w, const Value& v) { as<GridView>(w).sortDescending = v.truthy(); } },
			{ "filter",         [](Widget& w, const Value& v) { as<GridView>(w).filter = v.asText(); } },
			{ "filterColumns",  [](Widget& w, const Value& v) { as<GridView>(w).filterColumns = v.asText(); } },
			{ "rowHeight",      [](Widget& w, const Value& v) { as<GridView>(w).rowHeight = static_cast<float>(v.asNumber()); } },
			{ "headerHeight",   [](Widget& w, const Value& v) { as<GridView>(w).headerHeight = static_cast<float>(v.asNumber()); } },
			{ "barWidth",       [](Widget& w, const Value& v) { as<GridView>(w).barWidth = static_cast<float>(v.asNumber()); } },
			{ "wheelStep",      [](Widget& w, const Value& v) { as<GridView>(w).wheelStep = static_cast<float>(v.asNumber()); } },
			{ "variant",        [](Widget& w, const Value& v) { as<GridView>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kGridEvents[] = {
			{ "onSelect",          [](Widget& w, EventRunner run) { as<GridView>(w).onSelect = withText(std::move(run)); } },
			{ "onActivate",        [](Widget& w, EventRunner run) { as<GridView>(w).onActivate = withText(std::move(run)); } },
			{ "onSort",            [](Widget& w, EventRunner run) { as<GridView>(w).onSort = withText(std::move(run)); } },
			{ "onSelectionChange", [](Widget& w, EventRunner run) { as<GridView>(w).onSelectionChange = withText(std::move(run)); } },
		};

		// ---- column ------------------------------------------------------------------
		std::unique_ptr<Widget> makeColumn(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<GridColumn>(id);
		}
		void constColumn(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			GridColumn& w = as<GridColumn>(widget);
			w.field    = std::string(bp.text(node, "field"));
			w.title    = std::string(bp.text(node, "title"));
			w.hAlign   = textAlignFrom(bp.text(node, "hAlign"), w.hAlign);
			w.sortable = bp.boolean(node, "sortable", true);
			w.decimals = static_cast<int>(bp.number(node, "decimals", -1.0f));
		}
		// The grid looks its columns up by field every time it places them, so even the
		// field can follow state.
		constexpr BoundProp kColumnProps[] = {
			{ "field",    [](Widget& w, const Value& v) { as<GridColumn>(w).field = v.asText(); } },
			{ "title",    [](Widget& w, const Value& v) { as<GridColumn>(w).title = v.asText(); } },
			{ "hAlign",   [](Widget& w, const Value& v) { GridColumn& c = as<GridColumn>(w); c.hAlign = textAlignFrom(v.text, c.hAlign); } },
			{ "decimals", [](Widget& w, const Value& v) { as<GridColumn>(w).decimals = static_cast<int>(v.asNumber()); } },
			{ "sortable", [](Widget& w, const Value& v) { as<GridColumn>(w).sortable = v.truthy(); } },
		};
	}

	const WidgetKind kListKind{ "list", nullptr, &makeList, &constList,
	                            kListProps, std::size(kListProps), nullptr, 0,
	                            /*rowTemplate*/ true };
	const WidgetKind kTreeKind{ "tree", &kListKind, &makeTree, &constTree,
	                            kTreeProps, std::size(kTreeProps),
	                            kTreeEvents, std::size(kTreeEvents),
	                            /*rowTemplate*/ true };
	const WidgetKind kGridKind{ "grid", nullptr, &makeGrid, &constGrid,
	                            kGridProps, std::size(kGridProps),
	                            kGridEvents, std::size(kGridEvents) };
	const WidgetKind kColumnKind{ "column", nullptr, &makeColumn, &constColumn,
	                              kColumnProps, std::size(kColumnProps) };
}
