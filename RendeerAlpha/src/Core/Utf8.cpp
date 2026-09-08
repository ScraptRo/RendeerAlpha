#include <Core/Utf8.h>

namespace RDA::Utf8 {

	namespace {
		inline bool isContinuation(char c) {
			return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
		}
	}

	size_t decode(const char* s, const char* end, uint32_t& out) {
		out = kReplacement;
		if (!s || !end || s >= end) return 1;

		const unsigned char b0 = static_cast<unsigned char>(s[0]);
		if (b0 < 0x80) { out = b0; return 1; }

		// How many bytes the lead byte promises, and the smallest codepoint allowed to
		// use that many. The second half is what rejects overlong forms: C0 80 looks like
		// a valid encoding of NUL and has been a hole in more than one parser that only
		// checked the shape of the bytes.
		int      length = 0;
		uint32_t cp = 0;
		uint32_t least = 0;
		if      ((b0 & 0xE0) == 0xC0) { length = 2; cp = b0 & 0x1Fu; least = 0x80; }
		else if ((b0 & 0xF0) == 0xE0) { length = 3; cp = b0 & 0x0Fu; least = 0x800; }
		else if ((b0 & 0xF8) == 0xF0) { length = 4; cp = b0 & 0x07u; least = 0x10000; }
		else return 1; // a continuation byte, or F8..FF: not a lead byte at all

		if (end - s < length) return 1; // truncated
		for (int i = 1; i < length; ++i) {
			if (!isContinuation(s[i])) return 1;
			cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3Fu);
		}
		if (cp < least)                    return 1; // overlong
		if (cp > 0x10FFFF)                 return 1; // past the last codepoint there is
		if (cp >= 0xD800 && cp <= 0xDFFF)  return 1; // a surrogate half: UTF-16's business

		out = cp;
		return static_cast<size_t>(length);
	}

	void encode(uint32_t cp, std::string& out) {
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
		if (cp < 0x80) {
			out += static_cast<char>(cp);
		} else if (cp < 0x800) {
			out += static_cast<char>(0xC0u | (cp >> 6));
			out += static_cast<char>(0x80u | (cp & 0x3Fu));
		} else if (cp < 0x10000) {
			out += static_cast<char>(0xE0u | (cp >> 12));
			out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
			out += static_cast<char>(0x80u | (cp & 0x3Fu));
		} else {
			out += static_cast<char>(0xF0u | (cp >> 18));
			out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
			out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
			out += static_cast<char>(0x80u | (cp & 0x3Fu));
		}
	}

	// next() and prev() step over continuation bytes rather than decoding, so they land
	// on a boundary even when what they stepped over was not a valid sequence. That is
	// the property a caret needs: it can be wrong about what the character was, but it
	// must never stop halfway through one and it must always get somewhere.
	size_t next(const std::string& s, size_t i) {
		if (i >= s.size()) return s.size();
		size_t j = i + 1;
		while (j < s.size() && isContinuation(s[j])) ++j;
		return j;
	}

	size_t prev(const std::string& s, size_t i) {
		if (i == 0 || s.empty()) return 0;
		size_t j = (i > s.size() ? s.size() : i) - 1;
		while (j > 0 && isContinuation(s[j])) --j;
		return j;
	}

	size_t floorBoundary(const std::string& s, size_t i) {
		if (i >= s.size()) return s.size();
		while (i > 0 && isContinuation(s[i])) --i;
		return i;
	}

	size_t count(const char* s, size_t n) {
		if (!s) return 0;
		const char* end = s + n;
		size_t total = 0;
		while (s < end) {
			uint32_t cp = 0;
			s += decode(s, end, cp);
			++total;
		}
		return total;
	}
}
