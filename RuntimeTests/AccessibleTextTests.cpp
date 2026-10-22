#include "TestHarness.h"

#include <GraphicalObjects/Accessible.h>

#include <string>

// How a screen reader moves through a field's text.
//
// Both platform layers -- UI Automation and AT-SPI -- answer "the next word", "the line the
// caret is on" with these, so the two cannot disagree about what a word is, and a mistake
// here is a mistake a screen reader's user hears on both.
namespace {

	using namespace RDA;

	AccessibleText lines(std::initializer_list<int> starts) {
		AccessibleText info;
		info.present = true;
		info.lineStarts = starts;
		return info;
	}

	std::string unitAt(const std::string& utf8, const AccessibleText& info, TextUnit unit, int at) {
		const std::vector<uint32_t> text = textCodepoints(utf8);
		int start = 0, end = 0;
		textEnclosing(text, info, unit, at, start, end);
		return textSlice(text, start, end);
	}
}

TEST(text_is_counted_in_characters_not_bytes) {
	const std::vector<uint32_t> text = textCodepoints("a\xC4\x83" "b\xE6\x9D\xB1");   // a ă b 東
	CHECK_EQ(text.size(), size_t(4));
	CHECK_EQ(text[1], 0x103u);
	CHECK_EQ(text[3], 0x6771u);
	CHECK_STR(textSlice(text, 1, 3), "\xC4\x83" "b");
	CHECK_STR(textSlice(text, 2, 99), "b\xE6\x9D\xB1");
	CHECK_STR(textSlice(text, 3, 1), "");
}

TEST(a_word_carries_the_spaces_after_it) {
	const AccessibleText info = lines({ 0 });
	CHECK_STR(unitAt("hello brave world", info, TextUnit::Word, 0), "hello ");
	CHECK_STR(unitAt("hello brave world", info, TextUnit::Word, 7), "brave ");
	CHECK_STR(unitAt("hello brave world", info, TextUnit::Word, 16), "world");
	// At the very end: the last word, so "read the current word" says something.
	CHECK_STR(unitAt("hello brave world", info, TextUnit::Word, 17), "world");
}

TEST(punctuation_is_a_word_of_its_own) {
	const AccessibleText info = lines({ 0 });
	CHECK_STR(unitAt("save, then quit", info, TextUnit::Word, 0), "save");
	CHECK_STR(unitAt("save, then quit", info, TextUnit::Word, 4), ", ");
}

TEST(each_han_character_is_a_step) {
	// No spaces to go by, so each character is a word: not right, but followable.
	const AccessibleText info = lines({ 0 });
	const std::string tokyo = "\xE6\x9D\xB1\xE4\xBA\xAC";   // 東京
	CHECK_STR(unitAt(tokyo, info, TextUnit::Word, 0), "\xE6\x9D\xB1");
	CHECK_STR(unitAt(tokyo, info, TextUnit::Word, 1), "\xE4\xBA\xAC");
	// Accented Latin is letters, not punctuation.
	CHECK_STR(unitAt("caf\xC3\xA9 noir", info, TextUnit::Word, 0), "caf\xC3\xA9 ");
}

TEST(lines_come_from_where_the_field_says_they_start) {
	const AccessibleText info = lines({ 0, 6, 12 });
	const std::string text = "first\nother\nlast";
	CHECK_STR(unitAt(text, info, TextUnit::Line, 0), "first\n");
	CHECK_STR(unitAt(text, info, TextUnit::Line, 8), "other\n");
	CHECK_STR(unitAt(text, info, TextUnit::Line, 15), "last");
	CHECK_STR(unitAt(text, info, TextUnit::Document, 8), text);
}

TEST(next_and_previous_stop_at_the_ends) {
	const AccessibleText info = lines({ 0 });
	const std::vector<uint32_t> text = textCodepoints("one two");
	CHECK_EQ(textNext(text, info, TextUnit::Word, 0), 4);
	CHECK_EQ(textNext(text, info, TextUnit::Word, 4), 7);
	CHECK_EQ(textNext(text, info, TextUnit::Word, 7), 7);
	CHECK_EQ(textPrevious(text, info, TextUnit::Word, 7), 4);
	CHECK_EQ(textPrevious(text, info, TextUnit::Word, 4), 0);
	CHECK_EQ(textPrevious(text, info, TextUnit::Word, 0), 0);
	CHECK_EQ(textNext(text, info, TextUnit::Character, 2), 3);
	CHECK_EQ(textPrevious(text, info, TextUnit::Character, 2), 1);
}

TEST(a_character_is_where_the_line_says_it_is) {
	AccessibleText info = lines({ 0 });
	AccessibleText::Line line;
	line.first = 0;
	line.y = 10.0f;
	line.h = 18.0f;
	line.x = { 5.0f, 13.0f, 21.0f };   // two characters
	info.lines.push_back(line);
	Rect box{};
	CHECK(textCharacterBox(info, 1, box));
	CHECK_EQ(box.x, 13.0f);
	CHECK_EQ(box.w, 8.0f);
	CHECK_EQ(box.y, 10.0f);
	// After the last character: a caret's box, with no width.
	CHECK(textCharacterBox(info, 2, box));
	CHECK_EQ(box.x, 21.0f);
	CHECK_EQ(box.w, 0.0f);
	// A line not on screen has no boxes.
	CHECK(!textCharacterBox(info, 3, box));
}
