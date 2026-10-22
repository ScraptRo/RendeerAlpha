#include "TestHarness.h"

#include <Layout/TableOrder.h>

#include <string>
#include <vector>

// A table seen sorted and filtered: what a <grid> and a <list> show through, without the
// table being copied or reordered. The cases are the ones a reader notices -- "file10"
// after "file2", a filter that finds a number, a sort that keeps ties where they were.
namespace {

	using namespace RDA;

	// name, size, done -- in an order that is none of the orders being tested for.
	Table files() {
		Table t;
		const uint32_t name = t.defineColumn("name", ColumnType::Text);
		const uint32_t size = t.defineColumn("size", ColumnType::Number);
		const uint32_t done = t.defineColumn("done", ColumnType::Bool);
		const struct { const char* n; double s; bool d; } rows[] = {
			{ "file10.txt", 300, true },
			{ "Notes.md",   120, false },
			{ "file2.txt",  120, true },
			{ "apple.png",  2.5, false },
			{ "Apple.jpg",  900, true },
		};
		t.resize(std::size(rows));
		for (size_t i = 0; i < std::size(rows); ++i) {
			t.setText(i, name, rows[i].n);
			t.setNumber(i, size, rows[i].s);
			t.setBool(i, done, rows[i].d);
		}
		return t;
	}

	std::vector<size_t> view(const Table& t, std::string sortBy, bool down = false,
	                         std::string filter = {}, std::string columns = {}) {
		TableViewSpec spec;
		spec.sortBy = std::move(sortBy);
		spec.descending = down;
		spec.filter = std::move(filter);
		spec.filterColumns = std::move(columns);
		return tableView(t, spec);
	}

	using Rows = std::vector<size_t>;
}

TEST(nothing_asked_is_the_table_in_order) {
	const Table t = files();
	CHECK(view(t, "") == (Rows{ 0, 1, 2, 3, 4 }));
	CHECK(!TableViewSpec{}.active());
}

TEST(text_sorts_naturally_and_ignoring_case) {
	CHECK(naturalCompare("file2", "file10") < 0);
	CHECK(naturalCompare("file10", "file2") > 0);
	CHECK(naturalCompare("apple", "Apple") == 0);
	CHECK(naturalCompare("a", "ab") < 0);
	CHECK(naturalCompare("x007", "x7") == 0);
	// A number longer than any integer type still compares, digit by digit.
	CHECK(naturalCompare("v123456789012345678901", "v123456789012345678902") < 0);

	const Table t = files();
	// apple.png and Apple.jpg compare apart on the letter after "apple" -- '.' then 'j'
	// against '.' then 'p' -- so they are ordered, not tied.
	CHECK(view(t, "name") == (Rows{ 4, 3, 2, 0, 1 }));
}

TEST(numbers_sort_by_value_and_ties_keep_the_tables_order) {
	const Table t = files();
	CHECK(view(t, "size") == (Rows{ 3, 1, 2, 0, 4 }));
	// Descending is each key reversed, not the whole list: the two 120s stay 1 then 2.
	CHECK(view(t, "size", true) == (Rows{ 4, 0, 1, 2, 3 }));
}

TEST(bools_sort_false_first) {
	const Table t = files();
	CHECK(view(t, "done") == (Rows{ 1, 3, 0, 2, 4 }));
}

TEST(an_unknown_column_leaves_the_order_alone) {
	const Table t = files();
	CHECK(view(t, "colour") == (Rows{ 0, 1, 2, 3, 4 }));
}

TEST(the_filter_looks_in_text_columns_ignoring_case) {
	const Table t = files();
	CHECK(view(t, "", false, "APPLE") == (Rows{ 3, 4 }));
	CHECK(view(t, "", false, "txt") == (Rows{ 0, 2 }));
	CHECK(view(t, "", false, "nothing like it").empty());
	// Every text column but no others: 120 is a size, not a name.
	CHECK(view(t, "", false, "120").empty());
}

TEST(the_filter_finds_a_number_as_it_is_shown) {
	const Table t = files();
	CHECK(view(t, "", false, "120", "size") == (Rows{ 1, 2 }));
	CHECK(view(t, "", false, "2.5", "name|size") == (Rows{ 3 }));
	// A column that is not there is passed over rather than refused.
	CHECK(view(t, "", false, "notes", "colour|name") == (Rows{ 1 }));
}

TEST(filter_and_sort_together) {
	const Table t = files();
	CHECK(view(t, "size", true, "file") == (Rows{ 0, 2 }));
	CHECK(view(t, "name", false, "file") == (Rows{ 2, 0 }));
}

TEST(a_cell_is_shown_by_its_type) {
	const Table t = files();
	CHECK(tableCellText(t, 0, t.column("name")) == "file10.txt");
	CHECK(tableCellText(t, 0, t.column("size")) == "300");
	CHECK(tableCellText(t, 3, t.column("size")) == "2.5");
	CHECK(tableCellText(t, 3, t.column("size"), 2) == "2.50");
	CHECK(tableCellText(t, 0, t.column("done")) == "\xE2\x9C\x93");
	CHECK(tableCellText(t, 1, t.column("done")).empty());
	CHECK(tableCellText(t, 9, t.column("name")).empty());
	CHECK(tableCellText(t, 0, kNoColumn).empty());
}
