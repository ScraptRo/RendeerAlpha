#include <Core/Tables.h>

namespace RDA {

	// ---- Table -------------------------------------------------------------------

	uint32_t Table::defineColumn(std::string_view name, ColumnType type) {
		const std::string key(name);
		const auto found = mByName.find(key);
		if (found != mByName.end()) return found->second;

		Column column;
		column.name = key;
		column.type = type;
		if (type == ColumnType::Text) column.texts.resize(mRows);
		else                          column.numbers.resize(mRows, 0.0);

		const uint32_t id = static_cast<uint32_t>(mColumns.size());
		mColumns.push_back(std::move(column));
		mByName.emplace(key, id);
		++mVersion;
		return id;
	}

	uint32_t Table::column(std::string_view name) const {
		const auto found = mByName.find(std::string(name));
		return found == mByName.end() ? kNoColumn : found->second;
	}

	ColumnType Table::columnType(uint32_t column) const {
		return column < mColumns.size() ? mColumns[column].type : ColumnType::Number;
	}


	void Table::resize(size_t rows) {
		if (rows == mRows) return;
		for (Column& column : mColumns) {
			if (column.type == ColumnType::Text) column.texts.resize(rows);
			else                                 column.numbers.resize(rows, 0.0);
		}
		mRows = rows;
		++mVersion;
	}

	void Table::setNumber(size_t row, uint32_t col, double value) {
		if (!valid(row, col) || mColumns[col].type == ColumnType::Text) return;
		double& slot = mColumns[col].numbers[row];
		if (slot == value) return; // an unchanged write notifies nobody, as with signals
		slot = value;
		++mVersion;
	}

	void Table::setBool(size_t row, uint32_t col, bool value) {
		setNumber(row, col, value ? 1.0 : 0.0);
	}

	void Table::setText(size_t row, uint32_t col, std::string_view value) {
		if (!valid(row, col) || mColumns[col].type != ColumnType::Text) return;
		std::string& slot = mColumns[col].texts[row];
		if (slot == value) return;
		slot.assign(value);
		++mVersion;
	}

	double Table::number(size_t row, uint32_t col) const {
		if (!valid(row, col) || mColumns[col].type == ColumnType::Text) return 0.0;
		return mColumns[col].numbers[row];
	}

	bool Table::boolean(size_t row, uint32_t col) const {
		return number(row, col) != 0.0;
	}

	std::string_view Table::text(size_t row, uint32_t col) const {
		if (!valid(row, col) || mColumns[col].type != ColumnType::Text) return {};
		return mColumns[col].texts[row];
	}

	// ---- Tables ------------------------------------------------------------------

	// Never destroyed, like the other three tables: a destructor that runs after main may
	// still reach this. See bindings() in Layout/Bindings.cpp for the order that bites.
	Tables& tables() {
		static Tables* registry = new Tables;
		return *registry;
	}

	uint32_t Tables::define(std::string_view name) {
		const std::string key(name);
		const auto found = mByName.find(key);
		if (found != mByName.end()) return found->second;

		const uint32_t id = static_cast<uint32_t>(mTables.size());
		mNames.push_back(key);
		mTables.emplace_back();
		mByName.emplace(key, id);
		return id;
	}

	uint32_t Tables::find(std::string_view name) const {
		const auto found = mByName.find(std::string(name));
		return found == mByName.end() ? kNoTable : found->second;
	}

	Table& Tables::at(uint32_t table) {
		// A bad id gets a real, empty table rather than a crash: a list naming something
		// that does not exist should show nothing and say so, not take the process down.
		if (table >= mTables.size()) return mMissing;
		return mTables[table];
	}
}
