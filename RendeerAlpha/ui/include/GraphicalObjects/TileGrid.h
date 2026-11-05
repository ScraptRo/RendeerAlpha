#pragma once
#include <cstdint>
#include <GraphicalObjects/GuiTypes.h>
#include <string>
#include <vector>

// The other way to arrange docked panels: a grid of tiles rather than a tree of splits.
//
// DockTree.h cuts the area up. Every pane there is somebody's half, the whole area is
// always covered, and moving a panel re-divides the space around it. That is what an
// editor wants -- an explorer beside a viewport above a console, with splitters between
// them and no gaps.
//
// A dashboard wants the opposite. Its panels have a size of their own, they sit in a
// column grid with room between them, and dragging one pushes the ones it lands on out
// of the way instead of re-cutting anything. Nothing has to be anybody's half, and a row
// may be half empty. This is that arrangement, and it is the same model the web's tile
// libraries settled on.
//
//   columns = 12, and a tile is a rectangle of whole cells
//
//   +------+------+---------------+
//   |  a (4x2)    |    b (5x2)    |
//   +-------------+---------------+
//   |  c (3x3)  |                 |
//   +-----------+                 |
//
// Positions are in *cells*, not pixels, so the same arrangement is the same arrangement
// at any window size: a tile three columns wide is a quarter of the width whether the
// window is 900 pixels across or 2400. Only the row height is a pixel measurement,
// because rows do not divide anything -- they stack.
namespace RDA {

	// One panel's place in the grid. `id` names a DockContainer.
	struct Tile {
		std::string id;
		int col = 0, row = 0;    // top-left cell
		int cols = 3, rows = 3;  // size, in cells

		bool overlaps(const Tile& other) const {
			return col < other.col + other.cols && other.col < col + cols &&
			       row < other.row + other.rows && other.row < row + rows;
		}
	};

	class TileGrid {
	public:
		// How the cells are measured. Columns divide the width, so they are a count;
		// rows stack, so a row is a number of pixels.
		int   columns = 12;
		float rowHeight = 60.0f;
		float gap = 8.0f;

		const std::vector<Tile>& tiles() const { return mTiles; }
		Tile* find(const std::string& id);
		const Tile* find(const std::string& id) const;
		bool  empty() const { return mTiles.empty(); }
		size_t count() const { return mTiles.size(); }

		// Puts `id` in the first place it fits, at the size asked for, and returns it.
		// A tile that is already here is returned unchanged -- placing is what happens
		// to a panel the grid has not seen, and doing it twice must not move anything.
		Tile& place(const std::string& id, int cols, int rows);
		// Puts `id` exactly where it says, which is what a layout's col/row mean, and
		// pushes whatever was there. For a panel whose place was written down rather
		// than found.
		Tile& placeAt(const std::string& id, int col, int row, int cols, int rows);
		void  remove(const std::string& id);
		void  clear() { mTiles.clear(); }

		// Where a tile is on screen, inside `area`.
		Rect rectFor(const Tile& tile, const Rect& area) const;
		// The cell a point falls in. May be outside the grid; moveTo clamps.
		void cellAt(const Rect& area, glm::vec2 point, int& col, int& row) const;
		// How many rows the tallest tile reaches, which is how tall the grid is.
		int  rowsUsed() const;

		// Move or resize a tile, pushing what it lands on downward and then pulling
		// everything back up as far as it will go.
		//
		// `settling` is the tile the hand is on. It keeps the row it was given -- letting
		// it be pulled up mid-drag would take it out from under the pointer -- while
		// everything else closes the gaps around it. Empty means nobody is holding
		// anything, which is what a drop does: then everything compacts, the tile
		// included.
		void moveTo(const std::string& id, int col, int row, int cols, int rows,
		            const std::string& settling = {});

		// One tile per line: "id col row cols rows", after a header naming the grid. The
		// same shape the tree's save has, and read by the same kind of loop.
		std::string save() const;
		bool        load(const std::string& text);

		// A tile smaller than this is not a panel, it is a sliver.
		static constexpr int kMinCols = 1;
		static constexpr int kMinRows = 1;

	private:
		// Pushes everything `moving` overlaps downward, and whatever those then overlap,
		// until nothing overlaps anything.
		void pushOthers(const Tile& moving);
		// Pulls every tile up while the space above it is free, in reading order, so the
		// grid has no holes that something could have filled.
		void compact(const std::string& except);
		bool collidesWithAny(const Tile& tile, const std::string& ignore) const;

		std::vector<Tile> mTiles;
	};
}
