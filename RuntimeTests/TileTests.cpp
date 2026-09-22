#include "TestHarness.h"

#include <GraphicalObjects/TileGrid.h>

#include <string>
#include <algorithm>
#include <vector>

// The grid arrangement: where a tile goes, what it pushes, and what closes behind it.
//
// All of it is arithmetic on whole cells, which is why it is worth testing here rather
// than by dragging something: the rules are "nothing overlaps" and "nothing floats above
// a hole it could fall into", and those are claims about numbers.

using namespace RDA;

namespace {
	// Every tile, as "id:col,row,cols,rows", in the order the grid holds them -- sorted
	// by where they ended up so a check reads like the picture it is describing.
	std::string shape(const TileGrid& grid) {
		std::vector<const Tile*> order;
		for (const Tile& tile : grid.tiles()) order.push_back(&tile);
		std::sort(order.begin(), order.end(), [](const Tile* a, const Tile* b) {
			if (a->row != b->row) return a->row < b->row;
			return a->col < b->col;
		});
		std::string out;
		for (const Tile* tile : order) {
			if (!out.empty()) out += " ";
			out += tile->id + ":" + std::to_string(tile->col) + "," + std::to_string(tile->row)
			     + "," + std::to_string(tile->cols) + "," + std::to_string(tile->rows);
		}
		return out;
	}

	bool nothingOverlaps(const TileGrid& grid) {
		const std::vector<Tile>& tiles = grid.tiles();
		for (size_t i = 0; i < tiles.size(); ++i) {
			for (size_t j = i + 1; j < tiles.size(); ++j) {
				if (tiles[i].overlaps(tiles[j])) return false;
			}
		}
		return true;
	}
}

TEST(a_tile_takes_the_first_place_it_fits) {
	TileGrid grid;
	grid.columns = 12;
	grid.place("a", 4, 2);
	grid.place("b", 4, 2);
	grid.place("c", 4, 2);
	// Three four-wide tiles fill a twelve-column row exactly, so they sit beside each
	// other rather than starting a second row.
	CHECK_STR(shape(grid), "a:0,0,4,2 b:4,0,4,2 c:8,0,4,2");

	// The fourth has nowhere left on that row and drops to the next one.
	grid.place("d", 4, 2);
	CHECK_STR(shape(grid), "a:0,0,4,2 b:4,0,4,2 c:8,0,4,2 d:0,2,4,2");
	CHECK(nothingOverlaps(grid));
}

TEST(placing_a_tile_twice_does_not_move_it) {
	TileGrid grid;
	grid.place("a", 4, 2);
	grid.moveTo("a", 6, 3, 4, 2);
	// place() is what happens to a panel the grid has not seen. Asking again about one
	// it has must not drag it back to where it would have put it.
	grid.place("a", 4, 2);
	CHECK_STR(shape(grid), "a:6,0,4,2");
}

TEST(a_tile_pushes_what_it_lands_on_downward) {
	TileGrid grid;
	grid.columns = 12;
	grid.place("a", 6, 2);   // 0,0
	grid.place("b", 6, 2);   // 6,0

	// Dropped squarely onto b, so b goes below it.
	//
	// b stays in its own columns on the way down. Tiles are pulled *up* into free space
	// and never sideways, which is the rule every grid of this kind settles on: a tile
	// that slid left to fill a hole would move because of something that happened
	// somewhere else on the screen, and nobody can follow that.
	grid.moveTo("a", 6, 0, 6, 2);
	CHECK(nothingOverlaps(grid));
	CHECK_STR(shape(grid), "a:6,0,6,2 b:6,2,6,2");

	// The empty half of row 0 is left empty -- and is the first place the next tile
	// goes, because placing does look sideways.
	grid.place("c", 6, 2);
	CHECK_STR(shape(grid), "c:0,0,6,2 a:6,0,6,2 b:6,2,6,2");
}

TEST(a_push_carries_through_a_chain) {
	TileGrid grid;
	grid.columns = 4;
	grid.place("a", 4, 1);   // 0,0
	grid.place("b", 4, 1);   // 0,1
	grid.place("c", 4, 1);   // 0,2
	CHECK_STR(shape(grid), "a:0,0,4,1 b:0,1,4,1 c:0,2,4,1");

	// A full-width tile dropped on the top row has to move every one of them, each by
	// the one above it. Nothing may be left overlapping anything.
	grid.place("d", 4, 1);
	grid.moveTo("d", 0, 0, 4, 1, "d");
	CHECK(nothingOverlaps(grid));
	CHECK_STR(shape(grid), "d:0,0,4,1 a:0,1,4,1 b:0,2,4,1 c:0,3,4,1");
}

TEST(tiles_fall_into_the_hole_a_closed_one_left) {
	TileGrid grid;
	grid.columns = 4;
	grid.place("a", 4, 1);
	grid.place("b", 4, 1);
	grid.place("c", 4, 1);

	grid.remove("b");
	// c does not stay at row 2 with nothing above it: a grid with a hole in it is a
	// grid that has not finished.
	CHECK_STR(shape(grid), "a:0,0,4,1 c:0,1,4,1");
}

TEST(a_tile_cannot_be_dragged_out_of_the_grid) {
	TileGrid grid;
	grid.columns = 12;
	grid.place("a", 4, 2);

	grid.moveTo("a", 99, 4, 4, 2);   // far past the right edge
	const Tile* tile = grid.find("a");
	CHECK(tile != nullptr);
	// Against the right edge, not beyond it, and still four wide.
	CHECK_EQ(tile->col, 8);
	CHECK_EQ(tile->cols, 4);

	grid.moveTo("a", -5, -5, 4, 2);
	tile = grid.find("a");
	CHECK_EQ(tile->col, 0);
	CHECK_EQ(tile->row, 0);
}

TEST(a_tile_cannot_be_resized_to_nothing) {
	TileGrid grid;
	grid.columns = 12;
	grid.place("a", 4, 2);
	grid.moveTo("a", 0, 0, 0, 0);
	const Tile* tile = grid.find("a");
	CHECK_EQ(tile->cols, TileGrid::kMinCols);
	CHECK_EQ(tile->rows, TileGrid::kMinRows);

	// And not wider than the grid it is in.
	grid.moveTo("a", 0, 0, 40, 2);
	CHECK_EQ(grid.find("a")->cols, 12);
}

TEST(what_is_held_keeps_its_row_while_the_rest_settle) {
	TileGrid grid;
	grid.columns = 4;
	grid.place("a", 4, 1);
	grid.place("b", 4, 1);

	// Carried down to row 5 with nothing under it. While the hand is on it, it stays
	// where the hand is -- pulling it up would take it out from under the pointer.
	grid.moveTo("b", 0, 5, 4, 1, "b");
	CHECK_EQ(grid.find("b")->row, 5);

	// Let go, and it settles with everything else.
	grid.moveTo("b", 0, 5, 4, 1);
	CHECK_EQ(grid.find("b")->row, 1);
}

TEST(a_grid_reads_back_as_it_was_written) {
	TileGrid grid;
	grid.columns = 10;
	grid.rowHeight = 44.0f;
	grid.gap = 5.0f;
	grid.place("alpha", 5, 2);
	grid.place("beta", 5, 3);
	grid.moveTo("beta", 5, 0, 5, 3);
	const std::string written = grid.save();

	TileGrid read;
	CHECK(read.load(written));
	CHECK_EQ(read.columns, 10);
	CHECK_EQ(read.rowHeight, 44.0f);
	CHECK_EQ(read.gap, 5.0f);
	CHECK_STR(shape(read), shape(grid));

	// Nonsense leaves whatever was there alone rather than emptying it.
	CHECK(!read.load("this is not a grid"));
	CHECK_EQ(read.count(), static_cast<size_t>(2));
}

TEST(a_hand_edited_grid_is_made_legal_before_it_is_used) {
	TileGrid grid;
	// Overlapping, too wide, and negative -- all of which a person editing the file can
	// write, and none of which the rest of the grid is prepared to meet.
	CHECK(grid.load("tiles 4 50 6\na 0 0 9 2\nb 0 0 4 2\nc -3 -9 2 2\n"));
	CHECK(nothingOverlaps(grid));
	for (const Tile& tile : grid.tiles()) {
		CHECK(tile.col >= 0);
		CHECK(tile.row >= 0);
		CHECK(tile.col + tile.cols <= grid.columns);
	}
}

TEST(a_cell_is_where_the_pixels_say_it_is) {
	TileGrid grid;
	grid.columns = 4;
	grid.rowHeight = 50.0f;
	grid.gap = 10.0f;
	grid.place("a", 1, 1);

	// 4 columns and 5 gaps of 10 in 410 pixels leaves 90 per cell.
	const Rect area{ 0.0f, 0.0f, 410.0f, 400.0f };
	const Rect r = grid.rectFor(*grid.find("a"), area);
	CHECK_EQ(r.x, 10.0f);
	CHECK_EQ(r.y, 10.0f);
	CHECK_EQ(r.w, 90.0f);
	CHECK_EQ(r.h, 50.0f);

	// And the way back: a point inside the third column reads as the third column.
	int col = 0, row = 0;
	grid.cellAt(area, { 10.0f + 2.0f * 100.0f + 4.0f, 10.0f + 2.0f * 60.0f + 4.0f }, col, row);
	CHECK_EQ(col, 2);
	CHECK_EQ(row, 2);
}
