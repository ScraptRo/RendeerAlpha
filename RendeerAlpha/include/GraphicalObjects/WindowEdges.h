#pragma once
#include <cstdint>
#include <vector>

namespace RDA {

	// The edges of a window with no frame of its own.
	//
	// A frameless window has no non-client area for the OS to hit-test, so nothing lets
	// the reader resize it by its edges unless the engine does -- and it was the one thing
	// such a window could not do that a framed one could. The engine keeps a thin band
	// around the inside of the window that belongs to the window rather than to the
	// interface: the pointer shows which way it would resize, and a press there resizes.
	//
	// These are the parts that are only arithmetic, kept apart from GLFW so they can be
	// tested without a window: which edge a point is on, what a drag from it makes of the
	// rectangle, and the picture of a diagonal resize cursor, which GLFW 3.3 does not have.
	enum WindowEdge : uint8_t {
		EdgeNone = 0,
		EdgeLeft = 1,
		EdgeRight = 2,
		EdgeTop = 4,
		EdgeBottom = 8,
	};

	// Which edges (x, y) is on, for a window `w` by `h` with a band `band` wide, all in the
	// same coordinates. A corner is caught a little further along each edge than the band
	// is deep -- a few pixels square is too small a target to find, and the platforms that
	// draw their own frames make the corners generous for the same reason.
	uint8_t windowEdgeAt(double x, double y, double w, double h, double band);

	struct WindowRect {
		int x = 0, y = 0, w = 0, h = 0;
	};

	// The window as a drag from `edge` makes it, `dx`/`dy` from where it was taken hold
	// of. The opposite edge stays where it is -- including when a minimum or maximum stops
	// the dragged one, so a window dragged too narrow from the left does not start
	// walking right. A bound of 0 is no bound.
	WindowRect resizeFromEdge(const WindowRect& start, uint8_t edge, double dx, double dy,
	                          int minW, int minH, int maxW, int maxH);

	// An RGBA picture, `size` square, of a double-headed diagonal arrow: top-left to
	// bottom-right when `nwse`, else top-right to bottom-left. Black with a white outline,
	// the way a system cursor reads on any background. Its hot spot is the centre.
	std::vector<uint8_t> diagonalResizeCursor(int size, bool nwse);
}
