#include <Layout/TableOrder.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace RDA {

	namespace {
		char lower(char c) {
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		}
		bool isDigit(char c) { return c >= '0' && c <= '9'; }

		// Whether `text` contains `needle`, ignoring ASCII case. `needle` is already lower.
		bool containsFolded(std::string_view text, std::string_view needle) {
			if (needle.empty()) return true;
			if (needle.size() > text.size()) return false;
			for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
				size_t k = 0;
				while (k < needle.size() && lower(text[i + k]) == needle[k]) ++k;
				if (k == needle.size()) return true;
			}
			return false;
		}
	}

	int naturalCompare(std::string_view a, std::string_view b) {
		size_t i = 0, j = 0;
		while (i < a.size() && j < b.size()) {
			if (isDigit(a[i]) && isDigit(b[j])) {
				// A run of digits against a run of digits: by value, which for runs with no
				// leading zeros is by length first and then digit by digit -- no overflow
				// however long the number.
				size_t ie = i, je = j;
				while (ie < a.size() && isDigit(a[ie])) ++ie;
				while (je < b.size() && isDigit(b[je])) ++je;
				size_t is = i, js = j;
				while (is + 1 < ie && a[is] == '0') ++is;
				while (js + 1 < je && b[js] == '0') ++js;
				const size_t la = ie - is, lb = je - js;
				if (la != lb) return la < lb ? -1 : 1;
				for (size_t k = 0; k < la; ++k) {
					if (a[is + k] != b[js + k]) return a[is + k] < b[js + k] ? -1 : 1;
				}
				i = ie;
				j = je;
				continue;
			}
			const char ca = lower(a[i]), cb = lower(b[j]);
			if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
			++i;
			++j;
		}
		if (i < a.size()) return 1;
		if (j < b.size()) return -1;
		return 0;
	}

	std::string tableCellText(const Table& table, size_t row, uint32_t column, int decimals) {
		if (column == kNoColumn || column >= table.columns() || row >= table.rows()) return {};
		switch (table.columnType(column)) {
		case ColumnType::Text:
			return std::string(table.text(row, column));
		case ColumnType::Bool:
			return table.boolean(row, column) ? "\xE2\x9C\x93" : "";   // a tick
		case ColumnType::Number: {
			const double v = table.number(row, column);
			char out[64];
			if (decimals >= 0) {
				std::snprintf(out, sizeof(out), "%.*f", (std::min)(decimals, 9), v);
			} else if (std::floor(v) == v && std::fabs(v) < 1e15) {
				std::snprintf(out, sizeof(out), "%.0f", v);
			} else {
				std::snprintf(out, sizeof(out), "%g", v);
			}
			return out;
		}
		}
		return {};
	}

	std::vector<size_t> tableView(const Table& table, const TableViewSpec& spec) {
		const size_t rows = table.rows();
		std::vector<size_t> order;
		order.reserve(rows);

		// ---- which rows ----
		if (spec.filter.empty()) {
			order.resize(rows);
			std::iota(order.begin(), order.end(), size_t{ 0 });
		} else {
			std::string needle;
			for (const char c : spec.filter) needle += lower(c);
			std::vector<uint32_t> columns;
			if (spec.filterColumns.empty()) {
				for (uint32_t c = 0; c < table.columns(); ++c)
					if (table.columnType(c) == ColumnType::Text) columns.push_back(c);
			} else {
				std::string_view rest(spec.filterColumns);
				while (!rest.empty()) {
					const size_t bar = rest.find('|');
					const uint32_t c = table.column(rest.substr(0, bar));
					if (c != kNoColumn) columns.push_back(c);
					if (bar == std::string_view::npos) break;
					rest.remove_prefix(bar + 1);
				}
			}
			for (size_t r = 0; r < rows; ++r) {
				for (const uint32_t c : columns) {
					// Numbers are looked for as they are shown, so "120" finds a size of 120.
					const bool hit = table.columnType(c) == ColumnType::Text
						? containsFolded(table.text(r, c), needle)
						: containsFolded(tableCellText(table, r, c), needle);
					if (hit) { order.push_back(r); break; }
				}
			}
		}

		// ---- in what order ----
		if (spec.sortBy.empty()) return order;
		const uint32_t column = table.column(spec.sortBy);
		if (column == kNoColumn) return order;
		const ColumnType type = table.columnType(column);
		const bool down = spec.descending;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			int c = 0;
			if (type == ColumnType::Number) {
				const double x = table.number(a, column), y = table.number(b, column);
				c = (x < y) ? -1 : (x > y) ? 1 : 0;
			} else if (type == ColumnType::Bool) {
				c = static_cast<int>(table.boolean(a, column)) - static_cast<int>(table.boolean(b, column));
			} else {
				c = naturalCompare(table.text(a, column), table.text(b, column));
			}
			return down ? c > 0 : c < 0;
		});
		return order;
	}
}
