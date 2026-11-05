#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Icons that stay sharp.
//
// A PNG icon is wrong at every size but the one it was exported at: too soft scaled up,
// muddy scaled down, and a set of them is the same picture saved five times. An SVG is the
// shape, and the shape can be drawn at whatever size the layout turned out to give it --
// which on a display at 150% is not a size anybody exported.
//
// This is a rasteriser for **icons**, not a browser. What it reads is what icon sets are
// made of: <path>, <rect>, <circle>, <ellipse>, <line>, <polyline>, <polygon>, <g>, the
// whole of the path grammar including arcs, transforms, fills, strokes with caps and
// joins, and per-shape opacity. What it does not read is text, gradients, patterns,
// filters, masks, clip paths and CSS -- none of which an icon has, and each of which is a
// renderer rather than a feature.
//
// One deliberate difference from the specification, because the specification's answer is
// wrong here: `currentColor`, and a `fill` nobody stated, both come out **white** rather
// than black. An icon set almost always means "whatever colour the text is", and a UI that
// rendered the default as black would draw invisible icons on every dark theme. White is
// the colour a tint multiplies cleanly, so `<image src="save.svg" tint=...>` is the
// colour, and an icon that states its own colours keeps them.
namespace RDA {

	namespace Svg {

		// A parsed document: the shapes, in user units, with every transform already
		// applied. Parsing is cheap and happens once; rasterising happens per size.
		struct Picture;

		// From the text of an SVG, or from a file. Null, with a line in the log, if it
		// cannot be read -- which for an icon usually means it is not an SVG at all.
		//
		// A shared_ptr rather than a unique_ptr because `Picture` is only defined in the
		// rasteriser: the deleter has to be made where the type is complete, and a
		// shared_ptr carries one. That is what lets a widget hold a parsed drawing without
		// this header dragging the whole rasteriser into it.
		std::shared_ptr<Picture> parse(const std::string& xml, const std::string& named);
		std::shared_ptr<Picture> load(const std::string& path);

		// A path flattened to a polyline: x, y, x, y ... in the units it was written in.
		// For anything that needs to *follow* a path rather than fill one -- a motion
		// route, see Core/Route.h. False if it is not a path.
		bool flattenPath(const std::string& d, float tolerance, std::vector<float>& out);

		// What the document says it is, in pixels -- its width and height, or its viewBox
		// when it gives no size. Zero for either when it says neither, which the caller
		// has to have an answer for.
		void size(const Picture& picture, float& width, float& height);

		// Drawn at exactly `width` x `height`, as RGBA8 with straight alpha, rows tightly
		// packed. The document is fitted inside that box with its shape kept and centred,
		// which is what `preserveAspectRatio` defaults to and what an icon in a square
		// wants.
		//
		// Anti-aliased by sampling each pixel row several times and taking exact coverage
		// across it, which for shapes this small is both cheaper and steadier than
		// supersampling the whole image.
		// How long one loop of its animation lasts, in seconds. Zero when nothing in it
		// moves, which is what a still icon answers and what tells a caller it needs one
		// frame rather than thirty.
		float duration(const Picture& picture);

		bool rasterise(const Picture& picture, uint32_t width, uint32_t height,
		               std::vector<unsigned char>& out, float time = 0.0f);
	}
}
