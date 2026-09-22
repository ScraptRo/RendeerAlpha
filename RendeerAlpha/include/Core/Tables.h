#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Rows of data the interface can show, owned by C++.
//
// A signal holds one value. A catalogue holds ten thousand, and generating a widget per
// item is the thing this exists to avoid: the blueprint for two hundred rows is already
// 88 KB and 631 widgets, all built at start-up whether or not anyone scrolls to them.
//
// So the rows live here instead, and a list widget keeps only about as many widgets as
// fit on screen, changing which row each one shows as it scrolls. That is not a new
// mechanism -- rebinding a widget to a different value is the one thing this system does.
//
// Stored by column rather than by row, because that is how it is read: a list asks for
// one field of thirty consecutive rows, not for every field of one.
//
// Change notification is a single version per table, deliberately. A dirty set per row
// would be the general answer, and it is unnecessary here: when anything changes, the
// visible rows are re-bound, and the visible set is tiny by construction.
namespace RDA {

	inline constexpr uint32_t kNoColumn = 0xFFFFFFFFu;
	inline constexpr uint32_t kNoTable  = 0xFFFFFFFFu;

	enum class ColumnType : uint8_t {
		Number,
		Bool,
		Text,
	};

	class Table {
	public:
		// ---- shape, declared once ----
		uint32_t   defineColumn(std::string_view name, ColumnType type);
		uint32_t   column(std::string_view name) const;
		ColumnType columnType(uint32_t column) const;
		size_t     columns() const { return mColumns.size(); }

		// ---- contents ----
		size_t rows() const { return mRows; }
		void   resize(size_t rows);

		void setNumber(size_t row, uint32_t column, double value);
		void setBool(size_t row, uint32_t column, bool value);
		void setText(size_t row, uint32_t column, std::string_view value);

		double           number(size_t row, uint32_t column) const;
		bool             boolean(size_t row, uint32_t column) const;
		std::string_view text(size_t row, uint32_t column) const;

		// Bumped by anything that changes the shape or the contents. A list re-binds what
		// it is showing when this moves, and does nothing at all when it does not.
		uint64_t version() const { return mVersion; }

	private:
		struct Column {
			std::string name;
			ColumnType  type = ColumnType::Number;
			// Only the one matching `type` is ever filled; the other stays empty and
			// costs a pointer's worth of nothing.
			std::vector<double>      numbers;
			std::vector<std::string> texts;
		};

		bool valid(size_t row, uint32_t column) const {
			return column < mColumns.size() && row < mRows;
		}

		std::vector<Column>                       mColumns;
		std::unordered_map<std::string, uint32_t> mByName;
		size_t                                    mRows = 0;
		uint64_t                                  mVersion = 1;
	};

	// The tables an application declared, by name -- the same arrangement signals and
	// commands have, and for the same reason: a layout is resolved against names.
	class Tables {
	public:
		uint32_t define(std::string_view name);
		uint32_t find(std::string_view name) const;
		// Whether an id names a table. at() answers a bad id with a shared empty table,
		// which is the right thing for a layout that names one that is not there and the
		// wrong thing for a caller that wants to be told -- hence this.
		// Every table's version added up.
		//
		// A cheap "did any of them move" for whoever caches work between frames. The GUI
		// keeps last frame's geometry when nothing it can see has changed, and rows
		// arriving is invisible to every one of its other checks -- same widget tree,
		// same input, a <list> quietly showing different things. Without this a filled
		// table reached the screen only when something else happened to force a walk,
		// which in an application that is also writing signals looks like it works.
		//
		// Monotonic, because a table's own version only ever increases, so two tables
		// cannot cancel each other out.
		uint64_t revision() const;

		bool     valid(uint32_t table) const { return table < mTables.size(); }
		Table&   at(uint32_t table);

	private:
		std::vector<std::string>                  mNames;
		std::vector<Table>                        mTables;
		std::unordered_map<std::string, uint32_t> mByName;
		Table                                     mMissing; // returned for a bad id
	};

	Tables& tables();
}
