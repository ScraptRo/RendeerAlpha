#pragma once
#include <Core/Tables.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A table seen sorted, filtered, or both -- without the table being copied or reordered.
//
// The backend owns a table's rows and their order, and a backend that had to keep a second,
// sorted copy for every way an interface might show it would be keeping state that belongs
// to the interface. What the interface needs instead is a view: which table rows to show,
// in which order. That is a list of indices, worked out here, and a <list> or a <grid>
// shows the table through it.
namespace RDA {

	struct TableViewSpec {
		std::string sortBy;            // a column name; empty keeps the table's own order
		bool        descending = false;
		std::string filter;            // shown rows contain this, ignoring case; empty shows all
		// The columns the filter looks in, separated by '|'. Empty means every text column.
		std::string filterColumns;

		bool active() const { return !sortBy.empty() || !filter.empty(); }
		bool operator==(const TableViewSpec& o) const {
			return sortBy == o.sortBy && descending == o.descending && filter == o.filter &&
			       filterColumns == o.filterColumns;
		}
	};

	// The rows of `table` to show, in order. Sorting is stable, so rows that compare equal
	// keep the table's order, and descending is that order reversed per key, not overall.
	// Numbers compare as numbers, bools false first, and text naturally and without regard
	// to case -- "file2" before "file10", "apple" beside "Apple".
	std::vector<size_t> tableView(const Table& table, const TableViewSpec& spec);

	// Text compared the way a person sorts it: runs of digits by their value, everything
	// else letter by letter ignoring ASCII case. Negative, zero or positive.
	int naturalCompare(std::string_view a, std::string_view b);

	// A cell as it is shown: text as it is, a number with no trailing zeros, a bool as a
	// tick or nothing. `decimals` fixes a number's places; negative means "as needed".
	std::string tableCellText(const Table& table, size_t row, uint32_t column, int decimals = -1);
}
