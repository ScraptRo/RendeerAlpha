#include "TestHarness.h"

#include <Layout/Selection.h>

#include <string>
#include <vector>

// Several rows chosen at once: what Ctrl and Shift do, what survives the rows moving, and
// what the layout reads back.
namespace {

	using namespace RDA;
	using Keys = std::vector<std::string>;

	// Six rows, a..f, in the order they are shown.
	const Keys kShown{ "a", "b", "c", "d", "e", "f" };
	std::string at(size_t i) { return kShown[i]; }

	bool pick(RowSelection& s, Pick p, size_t to, const Keys& shown = kShown) {
		return s.apply(p, to, shown.size(), [&](size_t i) { return shown[i]; });
	}
}

TEST(a_click_chooses_one_and_makes_it_the_anchor) {
	RowSelection s;
	CHECK(pick(s, Pick::Only, 2));
	CHECK(s.keys() == (Keys{ "c" }));
	CHECK(s.anchor() == std::string("c"));
	CHECK(!pick(s, Pick::Only, 2));   // the same again is no change
}

TEST(ctrl_toggles_and_keeps_the_rest) {
	RowSelection s;
	pick(s, Pick::Only, 0);
	CHECK(pick(s, Pick::Toggle, 3));
	CHECK(s.keys() == (Keys{ "a", "d" }));
	CHECK(pick(s, Pick::Toggle, 0));
	CHECK(s.keys() == (Keys{ "d" }));
	CHECK(s.anchor() == std::string("a"));   // toggled out, and still where Shift reaches from
}

TEST(shift_reaches_from_the_anchor_either_way) {
	RowSelection s;
	pick(s, Pick::Only, 2);
	CHECK(pick(s, Pick::Range, 4));
	CHECK(s.keys() == (Keys{ "c", "d", "e" }));
	// The anchor does not move, so reaching the other way replaces the reach.
	CHECK(pick(s, Pick::Range, 0));
	CHECK(s.keys() == (Keys{ "c", "b", "a" }));
	CHECK(s.anchor() == std::string("c"));
}

TEST(ctrl_shift_adds_the_reach_to_what_is_chosen) {
	RowSelection s;
	pick(s, Pick::Only, 0);
	pick(s, Pick::Toggle, 3);              // a, d; anchor d
	CHECK(pick(s, Pick::RangeAdd, 5));
	CHECK(s.keys() == (Keys{ "a", "d", "e", "f" }));
}

TEST(a_reach_follows_the_anchor_when_the_rows_move) {
	RowSelection s;
	pick(s, Pick::Only, 1);                // b
	// Sorted the other way round: b is now fifth.
	const Keys reversed{ "f", "e", "d", "c", "b", "a" };
	CHECK(pick(s, Pick::Range, 2, reversed));
	CHECK(s.keys() == (Keys{ "b", "c", "d" }));
}

TEST(a_reach_from_an_anchor_no_longer_shown_starts_here) {
	RowSelection s;
	pick(s, Pick::Only, 1);                // b
	const Keys filtered{ "d", "e", "f" };
	CHECK(pick(s, Pick::Range, 1, filtered));
	CHECK(s.keys() == (Keys{ "e" }));
	CHECK(s.anchor() == std::string("e"));
}

TEST(moving_with_ctrl_leaves_the_selection_alone) {
	RowSelection s;
	pick(s, Pick::Only, 0);
	CHECK(!pick(s, Pick::Move, 4));
	CHECK(s.keys() == (Keys{ "a" }));
}

TEST(the_layout_reads_and_writes_keys_one_per_line) {
	RowSelection s;
	pick(s, Pick::Only, 1);
	pick(s, Pick::Range, 3);
	CHECK(s.joined() == std::string("b\nc\nd"));

	// Written back unchanged: no change, and the anchor stays.
	CHECK(!s.assign("b\nc\nd"));
	CHECK(s.anchor() == std::string("b"));
	// Blank lines, a Windows line ending and a repeat are not keys.
	CHECK(s.assign("x\r\n\ny\nx"));
	CHECK(s.keys() == (Keys{ "x", "y" }));
	CHECK(s.contains("y"));
	CHECK(!s.contains("b"));
	// The anchor was not kept, so it is the last one written.
	CHECK(s.anchor() == std::string("y"));
	CHECK(s.assign(""));
	CHECK(s.empty());
}

TEST(everything_and_nothing) {
	RowSelection s;
	pick(s, Pick::Only, 4);
	CHECK(s.all(kShown.size(), at));
	CHECK(s.size() == size_t{ 6 });
	CHECK(!s.all(kShown.size(), at));
	CHECK(s.clear());
	CHECK(!s.clear());
}

TEST(a_screen_reader_adds_and_removes_one_at_a_time) {
	RowSelection s;
	CHECK(s.add("c"));
	CHECK(!s.add("c"));
	CHECK(s.add("a"));
	CHECK(s.remove("c"));
	CHECK(!s.remove("c"));
	CHECK(s.keys() == (Keys{ "a" }));
}

TEST(what_a_click_or_key_means) {
	CHECK(pickForClick(false, true, true) == Pick::Only);
	CHECK(pickForClick(true, true, false) == Pick::Toggle);
	CHECK(pickForClick(true, false, true) == Pick::Range);
	CHECK(pickForClick(true, true, true) == Pick::RangeAdd);
	CHECK(pickForKey(true, true, false) == Pick::Move);
	CHECK(pickForKey(true, true, true) == Pick::Range);
	CHECK(pickForKey(false, false, true) == Pick::Only);
}
