#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace RDA {

	// UTF-8, and only the parts an interface needs: decoding, so text can be measured and
	// drawn a codepoint at a time; encoding, because a keyboard produces codepoints; and
	// boundary stepping, because a caret is a byte offset that must never land inside a
	// character.
	//
	// This lives in Core rather than beside the font because two unrelated things need it
	// and neither should depend on the other. The atlas needs codepoints to look glyphs
	// up. The text field needs boundaries to move a caret, and it must keep working in a
	// build with no device and therefore no atlas at all.
	//
	// Nothing here throws and nothing here can loop forever: every function that walks
	// bytes advances by at least one, whatever it is given. Text arrives from clipboards,
	// from files and across a C ABI, so malformed input is an ordinary case to handle
	// rather than a bug to assert on -- it decodes to U+FFFD and the walk carries on.
	namespace Utf8 {

		constexpr uint32_t kReplacement = 0xFFFD;

		// Decodes the codepoint at `s`, which must be below `end`. Returns how many bytes
		// were consumed: at least one, at most four.
		//
		// A lone continuation byte, a truncated sequence, an overlong encoding, a
		// surrogate half and anything above U+10FFFF all yield kReplacement and consume a
		// single byte -- so the next call resynchronises on the following byte instead of
		// swallowing the good text after a bad byte.
		size_t decode(const char* s, const char* end, uint32_t& out);

		// Appends `cp` to `out`. A codepoint that cannot be encoded is written as U+FFFD
		// rather than dropped, so text never silently shortens on the way through.
		void encode(uint32_t cp, std::string& out);

		// The byte offset of the boundary after / before `i`. Both clamp to [0, size()],
		// and both move by at least one byte when there is one to move: a caret driven by
		// these cannot stall, even in the middle of a broken sequence.
		size_t next(const std::string& s, size_t i);
		size_t prev(const std::string& s, size_t i);

		// Rounds `i` down to a boundary. This is what a caret that arrived by some other
		// route -- a click, a line change, a clamp against a shorter string -- is put
		// through, so everything else can go on treating it as a plain byte offset.
		size_t floorBoundary(const std::string& s, size_t i);

		// Codepoints in [s, s + n): what decode() would produce, so a malformed byte
		// counts as one.
		size_t count(const char* s, size_t n);
	}
}
