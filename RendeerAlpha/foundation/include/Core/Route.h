#pragma once
#include <string>

// The shape a thing travels along, as opposed to how fast it travels it.
//
// Those are two different questions and the engine used to be unable to ask the first one.
// A widget moving from one place to another had its x and its y eased independently, which
// makes the route a straight line always -- not as a decision, but because there was
// nowhere for a decision to live. Everything a designer means by "motion" past a fade is
// in the part that was missing.
//
// So a route is a curve, written as an SVG path, because that is a curve every drawing tool
// already exports and this engine already has a parser for. It is **normalised**: its own
// first point is mapped onto wherever the journey starts and its last onto wherever it
// ends, turned and scaled to fit. One arc therefore works for any two places, and bends
// relative to the direction of travel rather than to the screen.
//
// Measured by distance, not by the curve's parameter. Those are not the same -- a bezier's
// control points bunch its parameter up, so a journey at a constant parameter rate visibly
// speeds up and slows down on its own. That would fight whatever pace was asked for, and
// the pace would be blamed for it.
namespace RDA {
	namespace Route {

		// A parsed, flattened, measured route.
		struct Shape;

		// By the text it was written as, parsed once and kept. Null, with a line in the
		// log, if it is not a path -- and the caller then travels in a straight line,
		// because a broken route is not a reason to stop moving.
		//
		// Cached for the life of the process: routes are written in layouts, so there are
		// as many of them as a designer wrote, which is a handful.
		const Shape* named(const std::string& path);

		// Where along it, `progress` being the fraction of the **distance** covered.
		void at(const Shape& shape, float progress, float& x, float& y);

		// Its first point, and the straight line from there to its last. Together these
		// are what a route is fitted onto a journey by.
		void from(const Shape& shape, float& x, float& y);
		void chord(const Shape& shape, float& dx, float& dy);
	}
}
