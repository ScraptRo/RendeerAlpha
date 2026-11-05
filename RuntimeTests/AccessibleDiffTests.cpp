#include "TestHarness.h"

#include <GraphicalObjects/AccessibleDiff.h>

#include <initializer_list>
#include <string>

// What changed between two trees, which both platform layers build their events from. A
// mistake here is the same mistake on Windows and on Linux, so the rules that would otherwise
// be read off two event streams are written down once, here.
namespace {

	using namespace RDA;

	AccessibleNode node(uint32_t id, const std::string& name = "") {
		AccessibleNode one;
		one.id = id;
		one.info.name = name;
		return one;
	}

	// As the GUI builds one: the index is filled alongside the nodes, first of an id winning.
	AccessibleTree treeOf(std::initializer_list<AccessibleNode> nodes) {
		AccessibleTree tree;
		for (const AccessibleNode& one : nodes) {
			tree.byId.emplace(one.id, static_cast<int>(tree.nodes.size()));
			tree.nodes.push_back(one);
		}
		return tree;
	}

	// The one change a pair of single-node trees should produce.
	AccessibleChange onlyChange(const AccessibleNode& before, const AccessibleNode& now) {
		const AccessibleTree was = treeOf({ before });
		const AccessibleTree is = treeOf({ now });
		const AccessibleDiff diff = diffAccessibleTrees(&was, is);
		CHECK_EQ(diff.changes.size(), size_t(1));
		// Pointers into the two locals, which are gone on return: only the flags are read.
		AccessibleChange change = diff.changes.empty() ? AccessibleChange{} : diff.changes[0];
		change.before = change.now = nullptr;
		return change;
	}

	bool nothingBetween(const AccessibleNode& before, const AccessibleNode& now) {
		const AccessibleTree was = treeOf({ before });
		const AccessibleTree is = treeOf({ now });
		return diffAccessibleTrees(&was, is).changes.empty();
	}
}

TEST(the_first_tree_is_all_new_and_says_it_reshaped) {
	const AccessibleTree now = treeOf({ node(1), node(2) });
	const AccessibleDiff diff = diffAccessibleTrees(nullptr, now);
	CHECK(diff.reshaped);
	CHECK(diff.removed.empty());
	CHECK_EQ(diff.changes.size(), size_t(2));
	CHECK(diff.changes[0].before == nullptr);
	CHECK_EQ(diff.changes[0].now->id, 1u);
	CHECK_EQ(diff.changes[1].now->id, 2u);
}

TEST(two_equal_trees_have_nothing_to_report) {
	const AccessibleTree before = treeOf({ node(1, "a"), node(2, "b") });
	const AccessibleTree now = treeOf({ node(1, "a"), node(2, "b") });
	const AccessibleDiff diff = diffAccessibleTrees(&before, now);
	CHECK(!diff.reshaped);
	CHECK(diff.removed.empty());
	CHECK(diff.changes.empty());
}

TEST(a_new_node_carries_no_flags_and_no_before) {
	const AccessibleTree before = treeOf({ node(1) });
	AccessibleNode arrived = node(2, "named");
	arrived.info.toggled = 1;
	const AccessibleTree now = treeOf({ node(1), arrived });
	const AccessibleDiff diff = diffAccessibleTrees(&before, now);
	CHECK(diff.reshaped);
	CHECK_EQ(diff.changes.size(), size_t(1));
	CHECK(diff.changes[0].before == nullptr);
	CHECK(!diff.changes[0].toggled);
	CHECK(!diff.changes[0].name);
}

// A client hears them in this order, and a platform layer that took new nodes and changed
// nodes from two lists could not keep it.
TEST(new_and_changed_nodes_stay_in_the_order_the_tree_lists_them) {
	const AccessibleTree before = treeOf({ node(1, "a"), node(3, "c") });
	const AccessibleTree now = treeOf({ node(2), node(1, "changed"), node(4), node(3, "changed") });
	const AccessibleDiff diff = diffAccessibleTrees(&before, now);
	CHECK_EQ(diff.changes.size(), size_t(4));
	CHECK_EQ(diff.changes[0].now->id, 2u);
	CHECK_EQ(diff.changes[1].now->id, 1u);
	CHECK_EQ(diff.changes[2].now->id, 4u);
	CHECK_EQ(diff.changes[3].now->id, 3u);
	CHECK(diff.changes[0].before == nullptr);
	CHECK(diff.changes[1].before != nullptr);
}

TEST(removed_nodes_come_in_the_order_the_last_tree_listed_them) {
	const AccessibleTree before = treeOf({ node(5), node(1), node(9) });
	const AccessibleTree now = treeOf({ node(1) });
	const AccessibleDiff diff = diffAccessibleTrees(&before, now);
	CHECK(diff.reshaped);
	CHECK_EQ(diff.removed.size(), size_t(2));
	CHECK_EQ(diff.removed[0]->id, 5u);
	CHECK_EQ(diff.removed[1]->id, 9u);
}

// Nothing arrived and nothing left, and a client still has to read the list again.
TEST(the_same_ids_in_another_order_is_reshaped_with_nothing_added_or_removed) {
	const AccessibleTree before = treeOf({ node(1), node(2) });
	const AccessibleTree now = treeOf({ node(2), node(1) });
	const AccessibleDiff diff = diffAccessibleTrees(&before, now);
	CHECK(diff.reshaped);
	CHECK(diff.removed.empty());
	CHECK(diff.changes.empty());
}

TEST(a_node_is_followed_by_its_id_wherever_it_moved) {
	const AccessibleTree before = treeOf({ node(1, "a"), node(2, "b") });
	const AccessibleTree now = treeOf({ node(2, "b"), node(1, "renamed") });
	const AccessibleDiff diff = diffAccessibleTrees(&before, now);
	CHECK_EQ(diff.changes.size(), size_t(1));
	CHECK_EQ(diff.changes[0].now->id, 1u);
	CHECK_STR(diff.changes[0].before->info.name, "a");
}

TEST(a_change_is_reported_for_what_changed_and_nothing_else) {
	AccessibleNode a = node(1, "before");
	AccessibleNode b = node(1, "after");
	const AccessibleChange change = onlyChange(a, b);
	CHECK(change.name);
	CHECK(!change.value);
	CHECK(!change.toggled);
	CHECK(!change.expanded);
	CHECK(!change.selected);
	CHECK(!change.range);
	CHECK(!change.caret);
	CHECK(!change.anchor);
}

TEST(toggled_expanded_and_selected_report_a_change_between_two_real_states) {
	AccessibleNode a = node(1);
	a.info.toggled = 0;
	a.info.expanded = 1;
	a.info.selected = 0;
	AccessibleNode b = a;
	b.info.toggled = 1;
	b.info.expanded = 0;
	b.info.selected = 1;
	const AccessibleChange change = onlyChange(a, b);
	CHECK(change.toggled);
	CHECK(change.expanded);
	CHECK(change.selected);
}

// -1 is the state not being there, and there is nothing to announce that as.
TEST(a_node_that_stops_having_a_state_reports_nothing_about_it) {
	AccessibleNode a = node(1);
	a.info.toggled = 1;
	a.info.expanded = 1;
	a.info.selected = 1;
	AccessibleNode b = node(1);
	CHECK(nothingBetween(a, b));
}

// The other way round is a change: it has a state now, and that is worth saying.
TEST(a_node_that_starts_having_a_state_reports_it) {
	AccessibleNode a = node(1);
	AccessibleNode b = node(1);
	b.info.selected = 0;
	const AccessibleChange change = onlyChange(a, b);
	CHECK(change.selected);
}

TEST(a_range_only_counts_on_a_node_that_has_one_now) {
	AccessibleNode a = node(1);
	a.info.hasRange = true;
	a.info.rangeValue = 1.0;
	AccessibleNode b = a;
	b.info.rangeValue = 2.0;
	CHECK(onlyChange(a, b).range);
	b.info.hasRange = false;
	CHECK(nothingBetween(a, b));
}

TEST(a_caret_and_an_anchor_only_count_on_a_node_with_text) {
	AccessibleNode a = node(1);
	a.text.present = true;
	a.text.caret = 1;
	a.text.anchor = 1;
	AccessibleNode b = a;
	b.text.caret = 4;
	const AccessibleChange moved = onlyChange(a, b);
	CHECK(moved.caret);
	CHECK(!moved.anchor);
	b = a;
	b.text.anchor = 0;
	const AccessibleChange selected = onlyChange(a, b);
	CHECK(!selected.caret);
	CHECK(selected.anchor);
	// Each on its own, or one of them could count without text and the other hide it.
	b = a;
	b.text.present = false;
	b.text.caret = 4;
	CHECK(nothingBetween(a, b));
	b = a;
	b.text.present = false;
	b.text.anchor = 0;
	CHECK(nothingBetween(a, b));
}

TEST(the_focused_node_is_named_by_its_id_and_none_is_zero) {
	AccessibleTree now = treeOf({ node(7), node(8) });
	now.focused = 1;
	CHECK_EQ(diffAccessibleTrees(nullptr, now).focused, 8u);
	now.focused = -1;
	CHECK_EQ(diffAccessibleTrees(nullptr, now).focused, 0u);
}

TEST(the_caret_is_kept_inside_the_text) {
	AccessibleText text;
	text.caret = -1;
	CHECK_EQ(textCaretIn(text, 5), 0);
	text.caret = 3;
	CHECK_EQ(textCaretIn(text, 5), 3);
	text.caret = 9;
	CHECK_EQ(textCaretIn(text, 5), 5);
}

// An unknown anchor is no selection, which is the anchor sitting on the caret.
TEST(an_unknown_anchor_sits_on_the_caret_and_a_known_one_is_kept_inside_the_text) {
	AccessibleText text;
	text.caret = 3;
	text.anchor = -1;
	CHECK_EQ(textAnchorIn(text, 5), 3);
	text.anchor = 1;
	CHECK_EQ(textAnchorIn(text, 5), 1);
	text.anchor = 9;
	CHECK_EQ(textAnchorIn(text, 5), 5);
	text.caret = 9;
	text.anchor = -1;
	CHECK_EQ(textAnchorIn(text, 5), 5);
}
