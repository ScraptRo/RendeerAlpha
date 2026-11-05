#include "TestHarness.h"

#include <GraphicalObjects/Widget.h>

// Where a widget nothing arranges ends up, on a real widget.
//
// LayoutTests has the rule for one axis as arithmetic. What is here is what it meets on the way
// out: the anchors, which read the same size, and the fallback for a widget that said nothing.
// Asking resolveRect needs no device, and it is the whole of the answer: placement() only adds
// the measuring, and the easing of a route.
using namespace RDA;

namespace {
	const glm::vec2 kOrigin{ 10.0f, 20.0f };
	const Rect      kParent{ 10.0f, 20.0f, 640.0f, 480.0f };

	void sameRect(const Rect& a, float x, float y, float w, float h) {
		CHECK(a.x == x);
		CHECK(a.y == y);
		CHECK(a.w == w);
		CHECK(a.h == h);
	}
}

TEST(a_child_of_a_panel_is_the_size_it_says) {
	// A button in the layout this was reported from, with what its bindings came to.
	Panel child("b");
	child.width = SizeSpec::fixed(320.0f);
	child.height = SizeSpec::fixed(240.0f);
	sameRect(child.resolveRect(kOrigin, kParent), 10.0f, 20.0f, 320.0f, 240.0f);
}

TEST(saying_nothing_still_fills_the_parent) {
	Panel child("b");
	sameRect(child.resolveRect(kOrigin, kParent), 10.0f, 20.0f, 640.0f, 480.0f);

	// From where it starts, so that it ends at the edge rather than past it.
	child.rect.x = 40.0f;
	child.rect.y = 30.0f;
	sameRect(child.resolveRect(kOrigin, kParent), 50.0f, 50.0f, 600.0f, 450.0f);
}

TEST(one_axis_can_be_said_and_the_other_left) {
	Panel child("b");
	child.height = SizeSpec::fixed(30.0f);
	sameRect(child.resolveRect(kOrigin, kParent), 10.0f, 20.0f, 640.0f, 30.0f);
}

TEST(a_fill_starts_where_the_widget_does) {
	Panel child("b");
	child.rect.x = 40.0f;
	child.width = SizeSpec::fill();
	CHECK(child.resolveRect(kOrigin, kParent).w == 600.0f);
}

TEST(w_and_h_win_over_width_and_height) {
	Panel child("b");
	child.rect.w = 100.0f;
	child.rect.h = 50.0f;
	child.width = SizeSpec::fixed(320.0f);
	child.height = SizeSpec::fill();
	sameRect(child.resolveRect(kOrigin, kParent), 10.0f, 20.0f, 100.0f, 50.0f);
}

TEST(content_is_what_was_measured_and_only_on_the_axis_that_asked) {
	Panel child("b");
	child.height = SizeSpec::content();
	sameRect(child.resolveRect(kOrigin, kParent, { 75.0f, 22.0f }), 10.0f, 20.0f, 640.0f, 22.0f);
}

TEST(a_widget_following_one_edge_is_placed_by_the_size_it_wrote) {
	Panel child("b");
	child.anchor = Anchor::BottomRight;
	child.marginRight = 8.0f;
	child.marginBottom = 4.0f;
	child.width = SizeSpec::fixed(100.0f);
	child.height = SizeSpec::fixed(30.0f);
	// 640 - 8 - 100 across, and 480 - 4 - 30 down, from the origin.
	sameRect(child.resolveRect(kOrigin, kParent), 542.0f, 466.0f, 100.0f, 30.0f);
}

TEST(a_pair_of_anchored_edges_decides_its_own_axis) {
	Panel child("b");
	child.anchor = Anchor::StretchX;
	child.marginRight = 8.0f;
	child.width = SizeSpec::fixed(100.0f);
	child.height = SizeSpec::fixed(30.0f);
	sameRect(child.resolveRect(kOrigin, kParent), 10.0f, 20.0f, 632.0f, 30.0f);
}

TEST(an_unanchored_placement_with_no_parent_extent_keeps_what_it_wrote) {
	Panel child("b");
	child.width = SizeSpec::fixed(100.0f);
	const Rect none{ 0.0f, 0.0f, 0.0f, 0.0f };
	CHECK(child.resolveRect(kOrigin, none).w == 100.0f);
}
