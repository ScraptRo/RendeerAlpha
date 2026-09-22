#include "TestHarness.h"

#include <Core/Utf8.h>
#include <GraphicalSrc/GlyphRanges.h>
#include <GraphicalObjects/GuiTypes.h>

#include <string>

// Text that is not English.
//
// These exist because the engine spent its whole life drawing ASCII 32..126 and turning
// everything else into blanks -- correctly spaced, silently wrong, and invisible to every
// test that was written in English. The C ABI carried UTF-8 faithfully the whole time,
// which made the gap harder to see rather than easier: the bytes arrived intact and the
// screen was still wrong.
//
// So the checks below are deliberately about the two halves separately. Decoding is what
// turns bytes into characters; the glyph table is what says which characters can be
// drawn. Getting one right and the other wrong is what happened before.
namespace {

	using namespace RDA;

	// Written as bytes rather than as source characters, so these tests do not depend on
	// what encoding the compiler believes this file is in. That is the same class of
	// silent mistake they are here to catch, and it would be a poor joke to fall into it
	// while testing for it.
	const std::string kBreveA   = "\xC4\x83";         // a-breve       U+0103, Romanian
	const std::string kCommaS   = "\xC8\x99";         // s-comma-below U+0219, Romanian
	const std::string kCommaT   = "\xC8\x9B";         // t-comma-below U+021B, Romanian
	const std::string kAcuteE   = "\xC3\xA9";         // e-acute       U+00E9, French
	const std::string kEmDash   = "\xE2\x80\x94";     // em dash       U+2014
	const std::string kEuro     = "\xE2\x82\xAC";     // euro          U+20AC
	const std::string kGrinning = "\xF0\x9F\x98\x80"; // grinning face U+1F600, four bytes

	uint32_t decodeOne(const std::string& s, size_t* consumed = nullptr) {
		uint32_t cp = 0;
		const size_t n = Utf8::decode(s.data(), s.data() + s.size(), cp);
		if (consumed) *consumed = n;
		return cp;
	}
}

TEST(utf8_decodes_a_sequence_of_every_length) {
	size_t used = 0;
	CHECK_EQ(decodeOne("A", &used), 0x41u);        CHECK_EQ(used, size_t(1));
	CHECK_EQ(decodeOne(kAcuteE, &used), 0xE9u);    CHECK_EQ(used, size_t(2));
	CHECK_EQ(decodeOne(kEmDash, &used), 0x2014u);  CHECK_EQ(used, size_t(3));
	CHECK_EQ(decodeOne(kGrinning, &used), 0x1F600u); CHECK_EQ(used, size_t(4));
}

TEST(utf8_encodes_what_it_decodes) {
	const uint32_t points[] = { 0x41, 0xE9, 0x103, 0x219, 0x21B, 0x2014, 0x20AC, 0x1F600, 0x10FFFF };
	for (uint32_t cp : points) {
		std::string encoded;
		Utf8::encode(cp, encoded);
		CHECK_EQ(decodeOne(encoded), cp);
	}
}

TEST(utf8_refuses_every_shape_of_malformed_byte) {
	// Each of these decodes to the replacement character and consumes exactly one byte,
	// so a walk resynchronises on the next byte rather than swallowing the good text
	// after a bad one.
	const std::string broken[] = {
		"\x80",                 // a continuation byte with no lead
		"\xC3",                 // a two-byte lead, truncated
		"\xE2\x80",             // a three-byte lead, truncated
		"\xC3\x41",             // a lead followed by something that is not a continuation
		"\xC0\xAF",             // an overlong '/': valid shape, forbidden encoding
		"\xE0\x80\xAF",         // overlong again, one byte longer
		"\xED\xA0\x80",         // U+D800, a surrogate half that has no business here
		"\xF5\x80\x80\x80",     // past U+10FFFF
		"\xFF",                 // never a lead byte at all
	};
	for (const std::string& s : broken) {
		size_t used = 0;
		CHECK_EQ(decodeOne(s, &used), Utf8::kReplacement);
		CHECK_EQ(used, size_t(1));
	}
}

TEST(utf8_encodes_the_unencodable_as_a_replacement) {
	// Not dropped: a string that goes through this must not come out shorter than it went
	// in, or a caret that trusted the length is now pointing somewhere else.
	std::string out;
	Utf8::encode(0xD800, out);     // a surrogate half
	Utf8::encode(0x110000, out);   // past the last codepoint there is
	CHECK_EQ(Utf8::count(out.data(), out.size()), size_t(2));
	CHECK_EQ(decodeOne(out), Utf8::kReplacement);
}

TEST(utf8_steps_over_whole_characters) {
	// "aăb" -- one byte, two bytes, one byte. A caret walking this must see three stops,
	// not four, and must never land on the second byte of the middle letter.
	const std::string s = "a" + kBreveA + "b";
	CHECK_EQ(s.size(), size_t(4));

	CHECK_EQ(Utf8::next(s, 0), size_t(1));
	CHECK_EQ(Utf8::next(s, 1), size_t(3)); // over both bytes of the letter, not one
	CHECK_EQ(Utf8::next(s, 3), size_t(4));
	CHECK_EQ(Utf8::next(s, 4), size_t(4)); // the end stays the end

	CHECK_EQ(Utf8::prev(s, 4), size_t(3));
	CHECK_EQ(Utf8::prev(s, 3), size_t(1));
	CHECK_EQ(Utf8::prev(s, 1), size_t(0));
	CHECK_EQ(Utf8::prev(s, 0), size_t(0));
}

TEST(utf8_floors_an_offset_that_landed_mid_character) {
	// This is what Up and Down do: keep a column from one line and apply it to another,
	// where the same number of bytes is a different number of characters.
	const std::string s = "a" + kBreveA + "b";
	CHECK_EQ(Utf8::floorBoundary(s, 2), size_t(1)); // the middle of the letter -> its start
	CHECK_EQ(Utf8::floorBoundary(s, 1), size_t(1));
	CHECK_EQ(Utf8::floorBoundary(s, 3), size_t(3));
	CHECK_EQ(Utf8::floorBoundary(s, 99), s.size());
}

TEST(utf8_walks_forward_even_through_rubbish) {
	// The property that matters more than correctness on malformed input: a caret driven
	// by these cannot stall, so no keystroke can hang the interface on a bad byte.
	const std::string s = "\xFF\xFE\x80\x80";
	size_t i = 0, steps = 0;
	while (i < s.size() && steps < 100) { i = Utf8::next(s, i); ++steps; }
	CHECK_EQ(i, s.size());
	CHECK(steps < 100);

	i = s.size(); steps = 0;
	while (i > 0 && steps < 100) { i = Utf8::prev(s, i); ++steps; }
	CHECK_EQ(i, size_t(0));
	CHECK(steps < 100);
}

TEST(utf8_counts_characters_not_bytes) {
	const std::string s = "Bucure" + kCommaS + "ti, Ia" + kCommaT + "i " + kEmDash + " " + kEuro + "5";
	CHECK_EQ(Utf8::count(s.data(), s.size()), size_t(20));
	CHECK_EQ(s.size(), size_t(26)); // and not the same as the byte count, which is the point
}

TEST(glyph_slots_are_dense_and_in_order) {
	// Every baked codepoint gets its own slot, the slots run 0..kGlyphCount-1 with no
	// gaps, and nothing outside the table claims one. glyphSlot() walks the blocks in
	// order and would quietly return wrong answers if they were ever listed out of order.
	int expected = 0;
	for (const GlyphBlock& block : kGlyphBlocks) {
		for (uint32_t k = 0; k < block.count; ++k) {
			CHECK_EQ(glyphSlot(block.first + k), expected);
			++expected;
		}
	}
	CHECK_EQ(expected, kGlyphCount);
	CHECK_EQ(glyphSlot(0x7F), -1);   // DEL
	CHECK_EQ(glyphSlot(0x0080), -1); // a C1 control
	CHECK_EQ(glyphSlot(0x0180), -1); // just past Latin Extended-A
}

TEST(the_glyph_table_holds_the_letters_europe_writes) {
	// The regression, named. Every one of these used to be drawn as blank space.
	struct Wanted { uint32_t cp; const char* what; };
	const Wanted wanted[] = {
		{ 0x0103, "a-breve, Romanian" },
		{ 0x00E2, "a-circumflex, Romanian" },
		{ 0x00EE, "i-circumflex, Romanian" },
		{ 0x0219, "s-comma-below, Romanian" },
		{ 0x021B, "t-comma-below, Romanian" },
		{ 0x00E9, "e-acute, French" },
		{ 0x00FC, "u-diaeresis, German" },
		{ 0x00DF, "sharp s, German" },
		{ 0x00F1, "n-tilde, Spanish" },
		{ 0x00E7, "c-cedilla, Portuguese" },
		{ 0x0142, "l-stroke, Polish" },
		{ 0x0159, "r-caron, Czech" },
		{ 0x0151, "o-double-acute, Hungarian" },
		{ 0x0131, "dotless i, Turkish" },
		{ 0x2014, "em dash" },
		{ 0x2019, "right single quote, the apostrophe an editor produces" },
		{ 0x2026, "ellipsis" },
		{ 0x20AC, "euro" },
		{ 0xFFFD, "the replacement box everything else is drawn as" },
	};
	for (const Wanted& w : wanted) {
		if (glyphSlot(w.cp) < 0) {
			::test::fail(__FILE__, __LINE__, std::string("not in the glyph table: ") + w.what);
		}
	}
}

TEST(the_glyph_table_says_plainly_what_it_does_not_hold) {
	// The boundary is a decision, not an oversight: these need shaping, or bidirectional
	// layout, or an atlas that is not baked once at startup. They draw as the replacement
	// box, and this is where that is written down as a fact rather than a plan.
	CHECK_EQ(glyphSlot(0x0391),  -1); // Greek capital alpha
	CHECK_EQ(glyphSlot(0x0410),  -1); // Cyrillic capital A
	CHECK_EQ(glyphSlot(0x05D0),  -1); // Hebrew alef
	CHECK_EQ(glyphSlot(0x0627),  -1); // Arabic alef
	CHECK_EQ(glyphSlot(0x4E00),  -1); // CJK unified ideograph
	CHECK_EQ(glyphSlot(0x1F600), -1); // an emoji: colour, and not one atlas's job
}

TEST(the_replacement_box_is_the_last_slot) {
	// slotFor() falls back to kReplacementSlot for anything undrawable, so the table has
	// to actually end there. A block appended below it would break the fallback silently.
	CHECK_EQ(glyphSlot(0xFFFD), kReplacementSlot);
	CHECK_EQ(kReplacementSlot, kGlyphCount - 1);
	CHECK_EQ(glyphSlot(0x20), kSpaceSlot);
}


// ---- which keystroke sends -------------------------------------------------------
//
// A text field's send gesture, as a table.
//
// This is here rather than driven through a window because one row of it cannot be
// pressed from outside the process: Windows refuses synthesised input aimed at another
// program's window unless the caller owns the input desktop, and GLFW releases a Shift
// it cannot see held on the real keyboard -- so Shift+Enter is the one gesture a harness
// cannot produce. Every other row has been driven through a running field as well; this
// is what covers the one that cannot be.

TEST(a_field_that_says_nothing_sends_the_way_its_mode_always_did) {
	using namespace RDA;
	const SubmitKey mine = SubmitKey::Default;

	// A single line: Enter sends, and always has.
	CHECK(submitsOn(mine, /*multiline*/false, /*ctrl*/false, /*shift*/false));
	// Ctrl+Enter on a single line is not a second way to do the same thing.
	CHECK(!submitsOn(mine, false, true, false));

	// A multi-line one: Enter is a line, Ctrl+Enter is the send. Enter has always made
	// a line here, and Ctrl+Enter has always been swallowed -- now it is heard.
	CHECK(!submitsOn(mine, /*multiline*/true, false, false));
	CHECK(submitsOn(mine, true, true, false));
}

TEST(a_composer_can_be_multi_line_and_still_send_on_enter) {
	using namespace RDA;
	// The combination the modes could not express, and the reason this property exists.
	CHECK(submitsOn(SubmitKey::Enter, /*multiline*/true, /*ctrl*/false, /*shift*/false));
	// Shift+Enter is the line break. This is the row no harness can press.
	CHECK(!submitsOn(SubmitKey::Enter, true, false, /*shift*/true));
	// And Ctrl+Enter is not a send here: the field named one gesture, not two.
	CHECK(!submitsOn(SubmitKey::Enter, true, true, false));
}

TEST(both_means_both_and_none_means_neither) {
	using namespace RDA;
	CHECK(submitsOn(SubmitKey::Both, true, false, false));   // Enter
	CHECK(submitsOn(SubmitKey::Both, true, true, false));    // Ctrl+Enter
	CHECK(!submitsOn(SubmitKey::Both, true, false, true));   // Shift+Enter is still a line

	CHECK(!submitsOn(SubmitKey::None, true, false, false));
	CHECK(!submitsOn(SubmitKey::None, true, true, false));
	CHECK(!submitsOn(SubmitKey::None, false, false, false)); // even on a single line

	// CtrlEnter on its own: the multi-line default, said out loud.
	CHECK(submitsOn(SubmitKey::CtrlEnter, true, true, false));
	CHECK(!submitsOn(SubmitKey::CtrlEnter, true, false, false));
}

TEST(a_misspelled_submit_key_leaves_the_field_alone) {
	using namespace RDA;
	// Not silently unsendable: a name nobody recognises keeps whatever was there.
	CHECK(RDA::submitFrom("enter", SubmitKey::None) == SubmitKey::Enter);
	CHECK(RDA::submitFrom("ctrlenter", SubmitKey::Enter) == SubmitKey::Enter);  // case matters
	CHECK(RDA::submitFrom("", SubmitKey::CtrlEnter) == SubmitKey::CtrlEnter);
	CHECK(RDA::submitFrom("both", SubmitKey::None) == SubmitKey::Both);
	CHECK(RDA::submitFrom("none", SubmitKey::Enter) == SubmitKey::None);
}
