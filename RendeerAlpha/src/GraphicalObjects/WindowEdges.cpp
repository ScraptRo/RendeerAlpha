#include <GraphicalObjects/WindowEdges.h>
#include <algorithm>
#include <cmath>

namespace RDA {

	uint8_t windowEdgeAt(double x, double y, double w, double h, double band) {
		if (w <= 0.0 || h <= 0.0 || band <= 0.0) return EdgeNone;
		if (x < 0.0 || y < 0.0 || x >= w || y >= h) return EdgeNone;
		const double corner = band * 3.0;

		uint8_t edge = EdgeNone;
		if (x < band)      edge |= EdgeLeft;
		if (x >= w - band) edge |= EdgeRight;
		if (y < band)      edge |= EdgeTop;
		if (y >= h - band) edge |= EdgeBottom;
		if (edge == EdgeNone) return EdgeNone;

		// Along a side, near the end of it, is the corner.
		if (edge & (EdgeLeft | EdgeRight)) {
			if (y < corner)      edge |= EdgeTop;
			if (y >= h - corner) edge |= EdgeBottom;
		}
		if (edge & (EdgeTop | EdgeBottom)) {
			if (x < corner)      edge |= EdgeLeft;
			if (x >= w - corner) edge |= EdgeRight;
		}
		// A window narrower or shorter than two bands would claim both sides at once;
		// the nearer one wins.
		if ((edge & EdgeLeft) && (edge & EdgeRight))
			edge = static_cast<uint8_t>(edge & ~((x < w * 0.5) ? EdgeRight : EdgeLeft));
		if ((edge & EdgeTop) && (edge & EdgeBottom))
			edge = static_cast<uint8_t>(edge & ~((y < h * 0.5) ? EdgeBottom : EdgeTop));
		return edge;
	}

	namespace {
		// One axis: where the low and high sides end up, the side not being dragged held.
		void resizeAxis(int start, int length, bool low, bool high, double delta,
		                int minimum, int maximum, int& outStart, int& outLength) {
			outStart = start;
			outLength = length;
			const int moved = static_cast<int>(std::lround(delta));
			int wanted = length;
			if (low)  wanted = length - moved;
			if (high) wanted = length + moved;
			const int lo = std::max(1, minimum);
			const int hi = (maximum > 0) ? std::max(lo, maximum) : wanted;
			wanted = std::clamp(wanted, lo, std::max(lo, hi));
			outLength = wanted;
			// The far side stays put: dragging the low side moves the start by however
			// much the length actually changed, not by however far the pointer went.
			if (low) outStart = start + (length - wanted);
		}
	}

	WindowRect resizeFromEdge(const WindowRect& start, uint8_t edge, double dx, double dy,
	                          int minW, int minH, int maxW, int maxH) {
		WindowRect out;
		resizeAxis(start.x, start.w, (edge & EdgeLeft) != 0, (edge & EdgeRight) != 0, dx,
		           minW, maxW, out.x, out.w);
		resizeAxis(start.y, start.h, (edge & EdgeTop) != 0, (edge & EdgeBottom) != 0, dy,
		           minH, maxH, out.y, out.h);
		return out;
	}

	std::vector<uint8_t> diagonalResizeCursor(int size, bool nwse) {
		size = std::max(size, 8);
		std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4, 0);
		const double c = size * 0.5;
		const double s = 1.0 / std::sqrt(2.0);
		// Along the arrow and across it.
		const double ax = s, ay = nwse ? s : -s;
		const double half = size * 0.44;            // from the centre to a tip
		const double headLength = size * 0.30;
		const double headHalf = size * 0.24;
		const double shaftHalf = std::max(1.0, size * 0.055);

		// Inside the arrow, grown by `grow` all round -- the outline is the arrow grown
		// by a pixel and a bit, less the arrow.
		auto inside = [&](double t, double n, double grow) {
			const double reach = half + grow;
			if (std::abs(t) > reach) return false;
			if (n <= shaftHalf + grow) return true;
			const double fromTip = reach - std::abs(t);
			return fromTip <= headLength + grow && n <= (headHalf + grow) * (fromTip / (headLength + grow));
		};

		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				const double px = x + 0.5 - c, py = y + 0.5 - c;
				const double t = px * ax + py * ay;
				const double n = std::abs(-px * ay + py * ax);
				uint8_t* p = &rgba[(static_cast<size_t>(y) * size + x) * 4];
				if (inside(t, n, 0.0)) {
					p[0] = p[1] = p[2] = 0; p[3] = 255;
				} else if (inside(t, n, 1.4)) {
					p[0] = p[1] = p[2] = 255; p[3] = 255;
				}
			}
		}
		return rgba;
	}
}
