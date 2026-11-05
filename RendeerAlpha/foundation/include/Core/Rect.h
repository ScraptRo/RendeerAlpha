#pragma once
#include <glm/vec2.hpp>

// A rectangle, in whatever units its user works in: layout units in the interface, pixels
// in the renderer. Its own header because nearly every layer has one -- the GUI lays them
// out, the renderer clips to them, the platform hosts tell the OS where a caret or a button
// is -- and none of those should have to include the GUI's types to say so.
namespace RDA {

	struct Rect {
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		bool contains(glm::vec2 p) const {
			return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h;
		}
		// Touching edges do not count as overlapping, so a widget resting exactly on the
		// boundary of what is visible is treated as outside it.
		bool overlaps(const Rect& other) const {
			return x < other.x + other.w && other.x < x + w &&
			       y < other.y + other.h && other.y < y + h;
		}
	};
}
