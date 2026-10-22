#include "TestHarness.h"

#include <GraphicalObjects/WindowEdges.h>

#include <cstdint>
#include <vector>

// The edges of a frameless window: which one the pointer is on, what dragging it does to
// the window, and the diagonal cursor GLFW 3.3 does not have.
//
// What these protect is the feel of an OS frame -- the corners are easy to find, the
// edge being dragged is the only one that moves, and a window dragged past its minimum
// stops rather than wandering off in the other direction.
namespace {

	using namespace RDA;

	constexpr double W = 800, H = 600, Band = 6;

	uint8_t at(double x, double y) { return windowEdgeAt(x, y, W, H, Band); }

	bool opaque(const std::vector<uint8_t>& rgba, int size, int x, int y) {
		return rgba[(static_cast<size_t>(y) * size + x) * 4 + 3] != 0;
	}
	bool black(const std::vector<uint8_t>& rgba, int size, int x, int y) {
		const uint8_t* p = &rgba[(static_cast<size_t>(y) * size + x) * 4];
		return p[3] == 255 && p[0] == 0 && p[1] == 0 && p[2] == 0;
	}
}

TEST(the_middle_of_a_window_is_no_edge) {
	CHECK_EQ(at(400, 300), static_cast<uint8_t>(EdgeNone));
	CHECK_EQ(at(Band + 1, 300), static_cast<uint8_t>(EdgeNone));
	CHECK_EQ(at(-1, 300), static_cast<uint8_t>(EdgeNone));   // outside is not ours either
}

TEST(each_side_is_its_own_edge) {
	CHECK_EQ(at(2, 300), static_cast<uint8_t>(EdgeLeft));
	CHECK_EQ(at(W - 2, 300), static_cast<uint8_t>(EdgeRight));
	CHECK_EQ(at(400, 2), static_cast<uint8_t>(EdgeTop));
	CHECK_EQ(at(400, H - 2), static_cast<uint8_t>(EdgeBottom));
}

TEST(a_corner_is_caught_a_little_way_along_either_side) {
	CHECK_EQ(at(2, 2), static_cast<uint8_t>(EdgeLeft | EdgeTop));
	// On the left edge, but near the bottom: the corner, not only the side.
	CHECK_EQ(at(2, H - 12), static_cast<uint8_t>(EdgeLeft | EdgeBottom));
	// On the top edge, near the right: the corner.
	CHECK_EQ(at(W - 12, 2), static_cast<uint8_t>(EdgeTop | EdgeRight));
	// Far enough along, the side again.
	CHECK_EQ(at(W - 40, 2), static_cast<uint8_t>(EdgeTop));
}

TEST(a_window_thinner_than_two_bands_gives_the_nearer_side) {
	CHECK_EQ(windowEdgeAt(2, 300, 8, 600, Band), static_cast<uint8_t>(EdgeLeft));
	CHECK_EQ(windowEdgeAt(6, 300, 8, 600, Band), static_cast<uint8_t>(EdgeRight));
}

TEST(dragging_the_right_edge_moves_only_the_right_edge) {
	const WindowRect start{ 100, 50, 800, 600 };
	const WindowRect r = resizeFromEdge(start, EdgeRight, 120, 40, 0, 0, 0, 0);
	CHECK_EQ(r.x, 100);
	CHECK_EQ(r.y, 50);
	CHECK_EQ(r.w, 920);
	CHECK_EQ(r.h, 600);   // a horizontal edge ignores vertical movement
}

TEST(dragging_the_left_edge_keeps_the_right_edge_where_it_was) {
	const WindowRect start{ 100, 50, 800, 600 };
	const WindowRect r = resizeFromEdge(start, EdgeLeft, 30, 0, 0, 0, 0, 0);
	CHECK_EQ(r.x, 130);
	CHECK_EQ(r.w, 770);
	CHECK_EQ(r.x + r.w, 900);
}

TEST(a_minimum_stops_the_edge_and_the_far_side_stays) {
	const WindowRect start{ 100, 50, 800, 600 };
	// Dragged 700 to the right from the left edge, with a 400-wide minimum.
	const WindowRect r = resizeFromEdge(start, EdgeLeft, 700, 0, 400, 0, 0, 0);
	CHECK_EQ(r.w, 400);
	CHECK_EQ(r.x + r.w, 900);   // the right edge did not move
	// And from the top, past the minimum height.
	const WindowRect t = resizeFromEdge(start, EdgeTop, 0, 1000, 0, 200, 0, 0);
	CHECK_EQ(t.h, 200);
	CHECK_EQ(t.y + t.h, 650);
}

TEST(a_maximum_holds_too) {
	const WindowRect start{ 0, 0, 800, 600 };
	const WindowRect r = resizeFromEdge(start, EdgeRight | EdgeBottom, 900, 900, 0, 0, 1000, 700);
	CHECK_EQ(r.w, 1000);
	CHECK_EQ(r.h, 700);
}

TEST(a_corner_moves_both_of_its_edges) {
	const WindowRect start{ 100, 100, 800, 600 };
	const WindowRect r = resizeFromEdge(start, EdgeLeft | EdgeTop, -50, -20, 0, 0, 0, 0);
	CHECK_EQ(r.x, 50);
	CHECK_EQ(r.y, 80);
	CHECK_EQ(r.w, 850);
	CHECK_EQ(r.h, 620);
}

TEST(the_diagonal_cursor_is_an_arrow_along_its_own_diagonal) {
	const int size = 24;
	const std::vector<uint8_t> nwse = diagonalResizeCursor(size, true);
	const std::vector<uint8_t> nesw = diagonalResizeCursor(size, false);
	CHECK_EQ(nwse.size(), static_cast<size_t>(size * size * 4));
	CHECK(black(nwse, size, size / 2, size / 2));          // the shaft crosses the hot spot
	CHECK(black(nesw, size, size / 2, size / 2));
	CHECK(opaque(nwse, size, 4, 4));                        // a head top-left...
	CHECK(!opaque(nwse, size, size - 4, 3));                // ...and nothing top-right
	CHECK(opaque(nesw, size, size - 5, 4));                 // the mirror image
	CHECK(!opaque(nesw, size, 3, 3));
	// Outlined, so it reads on a dark interface: some white is in there.
	bool white = false;
	for (size_t i = 0; i < nwse.size(); i += 4) {
		if (nwse[i + 3] == 255 && nwse[i] == 255) { white = true; break; }
	}
	CHECK(white);
}
