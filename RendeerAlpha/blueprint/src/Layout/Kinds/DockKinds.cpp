#include "Kinds.h"
#include "KindSupport.h"
#include <GraphicalObjects/Docking.h>
#include <iterator>

// The docking widgets: <dockspace>, the <dock> panels in it, and a panel's own <header>.
//
// Where a panel starts -- its side, its size, its tile -- is where it starts, and only
// until somebody moves it, so those take a constant. So does everything a dock space is
// arranged by, which a saved layout is read back against.
namespace RDA::Layout::Kinds {

	namespace {
		DockSide sideFrom(std::string_view word, DockSide fallback) {
			if (word == "floating") return DockSide::Floating;
			if (word == "left")     return DockSide::Left;
			if (word == "right")    return DockSide::Right;
			if (word == "top")      return DockSide::Top;
			if (word == "bottom")   return DockSide::Bottom;
			if (word == "center")   return DockSide::Center;
			return fallback;
		}

		// ---- dockspace ---------------------------------------------------------------
		std::unique_ptr<Widget> makeDockSpace(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<DockHost>(id);
		}
		void constDockSpace(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			DockHost& w = as<DockHost>(widget);
			w.persist = std::string(bp.text(node, "persist"));
			if (bp.text(node, "arrange") == "tiles") {
				w.space().arrange = DockArrange::Tiles;
			}
			TileGrid& grid = w.space().grid();
			grid.columns   = static_cast<int>(bp.number(node, "columns", static_cast<float>(grid.columns)));
			grid.rowHeight = bp.number(node, "rowHeight", grid.rowHeight);
			grid.gap       = bp.number(node, "gap", grid.gap);
		}

		// ---- dock --------------------------------------------------------------------
		std::unique_ptr<Widget> makeDock(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<DockContainer>(id, std::string(bp.text(node, "title")));
		}
		void constDock(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			// `title` was a constructor argument; the rest is where the panel starts.
			DockContainer& w = as<DockContainer>(widget);
			variantFrom(bp, node, w.variant);
			w.dock     = sideFrom(bp.text(node, "side"), w.dock);
			w.dockSize = bp.number(node, "size", w.dockSize);
			w.closable = bp.boolean(node, "closable", w.closable);
			// Where it starts in a tiles arrangement. A col or row nobody wrote stays
			// negative, which is what "wherever it fits" is spelled as.
			w.tileCol  = static_cast<int>(bp.number(node, "col", -1.0f));
			w.tileRow  = static_cast<int>(bp.number(node, "row", -1.0f));
			w.tileCols = static_cast<int>(bp.number(node, "cols", static_cast<float>(w.tileCols)));
			w.tileRows = static_cast<int>(bp.number(node, "rows", static_cast<float>(w.tileRows)));
			// Where a floating one starts, in the x, y, w and h every element takes. Each
			// only if it was written, so `w={320}` alone keeps the default place.
			if (bp.find(node, "x")) w.floatingRect.x = w.rect.x;
			if (bp.find(node, "y")) w.floatingRect.y = w.rect.y;
			if (bp.find(node, "w") && w.rect.w > 0.0f) w.floatingRect.w = w.rect.w;
			if (bp.find(node, "h") && w.rect.h > 0.0f) w.floatingRect.h = w.rect.h;
		}
		constexpr BoundProp kDockProps[] = {
			{ "title",    [](Widget& w, const Value& v) { as<DockContainer>(w).title = v.asText(); } },
			{ "closable", [](Widget& w, const Value& v) { as<DockContainer>(w).closable = v.truthy(); } },
			{ "variant",  [](Widget& w, const Value& v) { as<DockContainer>(w).variant = Variant(std::string_view(v.text)); } },
		};

		// ---- header ------------------------------------------------------------------
		// Nothing of its own: its height is the one every widget has.
		std::unique_ptr<Widget> makeHeader(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<DockHeader>(id);
		}
	}

	const WidgetKind kDockSpaceKind{ "dockspace", nullptr, &makeDockSpace, &constDockSpace };
	const WidgetKind kDockKind{ "dock", nullptr, &makeDock, &constDock,
	                            kDockProps, std::size(kDockProps) };
	const WidgetKind kHeaderKind{ "header", nullptr, &makeHeader };
}
