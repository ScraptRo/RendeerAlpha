#include <GraphicalObjects/GuiTypes.h>

// One idea of what a colour is.
//
// It used to live in GuiTheme.cpp, because a theme was the only thing reading colours
// written by a person. It is not any more: a label's spans carry one, an SVG's fill and
// stroke are two more, and an animation blends between them -- and none of those are a
// theme's business. Three parsers would have been three slightly different ideas of what
// "#F80" means, so there is one, and it is here rather than inside any of its callers.
//
// What it takes:
//   #RGB  #RRGGBB  #RRGGBBAA        hex, with or without alpha
//   r,g,b   r,g,b,a                 decimals, separated by anything that is not a digit
//
// Anything else is refused rather than guessed at, so a misspelt colour leaves whatever
// was already there instead of quietly turning something black.

namespace RDA {

	namespace {
		bool hexVal(char c, int& v) {
			if (c >= '0' && c <= '9') { v = c - '0';      return true; }
			if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
			if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
			return false;
		}
	}

	bool parseColor(const char* text, uint32_t& out) {
		if (!text) return false;
		while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') ++text;

		if (*text == '#') {
			++text;
			int d[8], n = 0;
			for (; n < 8 && text[n]; ++n) {
				if (!hexVal(text[n], d[n])) break;
			}
			auto byte = [&](int i) { return (d[2 * i] << 4) | d[2 * i + 1]; };
			if (n == 3) { out = rgba(d[0] * 17, d[1] * 17, d[2] * 17); return true; }
			if (n == 6) { out = rgba(byte(0), byte(1), byte(2)); return true; }
			if (n == 8) { out = rgba(byte(0), byte(1), byte(2), byte(3)); return true; }
			return false;
		}

		int c[4] = { 0, 0, 0, 255 }, n = 0;
		for (const char* p = text; *p && n < 4; ) {
			while (*p && (*p < '0' || *p > '9')) ++p;
			if (!*p) break;
			int v = 0;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
			c[n++] = v > 255 ? 255 : v;
		}
		if (n < 3) return false;
		out = rgba(c[0], c[1], c[2], c[3]);
		return true;
	}
}
