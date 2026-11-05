#pragma once
#include <cstdint>

// One idea of what a colour is: a packed 0xAABBGGRR, the R8G8B8A8_UNORM layout the
// interface's vertices carry. Here, below everything, because colours are written by a
// theme, a label's spans, an SVG and a drawing sent over the C ABI -- none of which is the
// GUI's business -- and the code that reads them must not have to include the GUI to do it.
namespace RDA {

	// Pack RGBA (0..255). constexpr so it can be a default argument.
	constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
		return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
	}

	// The same colour at a fraction of its opacity. Animation's other half: something
	// arriving grows *and* fades in, and doing only one of those looks like a glitch.
	constexpr uint32_t fadeTo(uint32_t colour, float t) {
		const uint32_t alpha = (colour >> 24) & 0xFFu;
		const float scaled = static_cast<float>(alpha) * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
		return (colour & 0x00FFFFFFu) | (static_cast<uint32_t>(scaled + 0.5f) << 24);
	}

	// "#RGB" / "#RRGGBB" / "#RRGGBBAA", or decimal "r,g,b" / "r,g,b,a" with any
	// non-digit separators. Leaves `out` alone and answers false on anything else.
	//
	// Shared rather than copied: a theme, a drawing sent over the C ABI and a label's
	// spans all take a colour written by a person, and three parsers would be three
	// slightly different ideas of what a colour is.
	bool parseColor(const char* text, uint32_t& out);
}
