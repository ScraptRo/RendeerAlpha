#include "TestHarness.h"
#include <Core/Commands.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Layout/Expression.h>
#include <Layout/ExpressionParser.h>

// The pieces the interface layer grew after signals: commands, tables, and the three
// additions to the expression grammar that reach them.
//
// These are the promises a layout is compiled against. A row template that reads a column
// and a handler that calls a command both turn into one instruction, and what that
// instruction does when the thing it names is missing is as much a part of the contract as
// what it does when it is there.
using namespace RDA;
using namespace RDA::Layout;

namespace {

	// Everything here is process-wide, the way signals are, so each case starts clean.
	struct Fixture {
		Fixture() { signals().clear(); }
		~Fixture() { signals().clear(); }
	};

	// Parses and evaluates, resolving names the way the loader does: a marked name is a
	// command or a column, anything else is a signal.
	struct Run {
		bool        parsed = false;
		bool        ran = false;
		std::string error;
		Value       value;
	};

	Run run(const std::string& source, bool handler = false,
	        const std::string& rowParam = {}, Table* table = nullptr, size_t row = 0) {
		Run out;
		const ParseResult parsed = parseBinding(source, handler, rowParam);
		if (!parsed.ok) {
			out.error = parsed.error;
			return out;
		}
		out.parsed = true;

		std::vector<uint32_t> ids;
		for (const std::string& pooled : parsed.program.signals) {
			if (!pooled.empty() && pooled.front() == '@') {
				ids.push_back(commands().find(pooled.substr(1)));
			} else if (!pooled.empty() && pooled.front() == '#') {
				ids.push_back(table ? table->column(pooled.substr(1)) : kNoColumn);
			} else {
				uint32_t id = signals().find(pooled);
				if (id == kNoSignal) id = signals().define(pooled, 0.0);
				ids.push_back(id);
			}
		}

		const RowRef ref{ table, row };
		const EvalResult result = evaluate(parsed.program, signals(), ids, nullptr,
		                                   table ? &ref : nullptr);
		out.ran = result.ok;
		out.error = result.error;
		out.value = result.value;
		return out;
	}

	Table makeCatalogue() {
		Table t;
		t.defineColumn("title", ColumnType::Text);
		t.defineColumn("price", ColumnType::Number);
		t.defineColumn("inStock", ColumnType::Bool);
		t.resize(3);
		t.setText(0, 0, "first");   t.setNumber(0, 1, 10.0); t.setBool(0, 2, true);
		t.setText(1, 0, "second");  t.setNumber(1, 1, 20.0); t.setBool(1, 2, false);
		t.setText(2, 0, "third");   t.setNumber(2, 1, 30.0); t.setBool(2, 2, true);
		return t;
	}
}

// ---- tables --------------------------------------------------------------------------

TEST(a_table_holds_rows_by_column) {
	Table t = makeCatalogue();
	CHECK_EQ(t.rows(), size_t(3));
	CHECK_EQ(t.columns(), size_t(3));
	CHECK(t.text(1, 0) == "second");
	CHECK_EQ(t.number(2, 1), 30.0);
	CHECK(t.boolean(0, 2));
	CHECK(!t.boolean(1, 2));
}

TEST(a_column_is_found_by_name) {
	Table t = makeCatalogue();
	CHECK_EQ(t.column("price"), uint32_t(1));
	CHECK_EQ(t.column("nothing"), kNoColumn);
}

TEST(an_unchanged_write_does_not_move_the_version) {
	// The same promise a signal makes, and for the same reason: a list re-reads what is
	// on screen whenever this moves, so a write that changed nothing must not move it.
	Table t = makeCatalogue();
	const uint64_t before = t.version();
	t.setNumber(0, 1, 10.0);
	CHECK_EQ(t.version(), before);
	t.setNumber(0, 1, 11.0);
	CHECK(t.version() != before);
}

TEST(resizing_keeps_what_fits) {
	Table t = makeCatalogue();
	t.resize(2);
	CHECK_EQ(t.rows(), size_t(2));
	CHECK(t.text(1, 0) == "second");
	t.resize(4);
	CHECK_EQ(t.rows(), size_t(4));
	CHECK(t.text(3, 0).empty()); // a row that did not exist is empty, not stale
}

TEST(an_id_that_names_no_table_is_reported_rather_than_answered) {
	// at() hands back a shared empty table for a bad id, which is right for a layout
	// naming one that is not there and wrong for a caller that wants to be told -- the C
	// ABI is that caller, and this is what it asks.
	Tables tables;
	const uint32_t real = tables.define("products");
	CHECK(tables.valid(real));
	CHECK(!tables.valid(real + 1));
	CHECK(!tables.valid(0xFFFFFFFFu));
}

TEST(reading_past_the_end_is_empty_rather_than_a_crash) {
	Table t = makeCatalogue();
	CHECK_EQ(t.number(99, 1), 0.0);
	CHECK(t.text(0, 99).empty());
}

// ---- commands ------------------------------------------------------------------------

TEST(a_command_runs_what_was_bound_to_it) {
	const uint32_t id = commands().define("test_ran");
	int calls = 0;
	commands().bind(id, [&calls] { ++calls; });
	CHECK(commands().invoke(id));
	CHECK_EQ(calls, 1);
}

TEST(a_command_with_nothing_bound_reports_it) {
	const uint32_t id = commands().define("test_unbound");
	CHECK(!commands().invoke(id));
	CHECK_EQ(commands().find("test_never_declared"), kNoCommand);
}

TEST(calling_a_command_from_a_handler_runs_it) {
	Fixture fixture;
	const uint32_t id = commands().define("test_called");
	int calls = 0;
	commands().bind(id, [&calls] { ++calls; });

	const Run out = run("() => commands.test_called()", /*handler*/ true);
	CHECK(out.parsed);
	CHECK(out.ran);
	CHECK_EQ(calls, 1);
}

TEST(a_value_binding_may_not_call_a_command) {
	Fixture fixture;
	const Run out = run("() => commands.anything()", /*handler*/ false);
	CHECK(!out.parsed);
	CHECK(out.error.find("value binding") != std::string::npos);
}

TEST(a_command_takes_no_arguments) {
	Fixture fixture;
	const Run out = run("() => commands.anything(state.count)", /*handler*/ true);
	CHECK(!out.parsed);
	CHECK(out.error.find("no arguments") != std::string::npos);
}

TEST(a_handler_calling_an_unbound_command_fails_rather_than_pretending) {
	Fixture fixture;
	commands().define("test_declared_only");
	const Run out = run("() => commands.test_declared_only()", /*handler*/ true);
	CHECK(out.parsed);
	CHECK(!out.ran);
}

// ---- row templates ---------------------------------------------------------------------

TEST(a_row_template_reads_its_columns) {
	Fixture fixture;
	Table t = makeCatalogue();

	const Run title = run("() => item.title", false, "item", &t, 1);
	CHECK(title.ran);
	CHECK(title.value.asText() == "second");

	const Run price = run("() => item.price", false, "item", &t, 2);
	CHECK(price.ran);
	CHECK_EQ(price.value.asNumber(), 30.0);

	const Run stock = run("() => item.inStock", false, "item", &t, 0);
	CHECK(stock.ran);
	CHECK(stock.value.truthy());
}

TEST(a_row_template_mixes_columns_with_state_and_literals) {
	Fixture fixture;
	Table t = makeCatalogue();
	signals().define("discount", 5.0);

	const Run out = run("() => `${item.title} costs ${item.price - state.discount}`",
	                    false, "item", &t, 0);
	CHECK(out.ran);
	CHECK(out.value.asText() == "first costs 5");
}

TEST(a_column_that_does_not_exist_fails_rather_than_reading_zero) {
	Fixture fixture;
	Table t = makeCatalogue();
	const Run out = run("() => item.nothing", false, "item", &t, 0);
	CHECK(out.parsed);   // the grammar cannot know the columns
	CHECK(!out.ran);     // resolving them can, and does
}

TEST(reading_a_column_outside_a_template_is_not_in_scope) {
	Fixture fixture;
	const Run out = run("() => item.title");
	CHECK(!out.parsed);
	CHECK(out.error.find("not in scope") != std::string::npos);
}

TEST(a_row_is_one_level_deep) {
	Fixture fixture;
	Table t = makeCatalogue();
	const Run out = run("() => item.title.length", false, "item", &t, 0);
	CHECK(!out.parsed);
}

// ---- the handler parameter --------------------------------------------------------------

TEST(a_handler_may_name_the_value_it_was_given) {
	Fixture fixture;
	signals().define("text", std::string_view(""));

	const ParseResult parsed = parseBinding("(typed) => state.text = typed", true);
	CHECK(parsed.ok);

	std::vector<uint32_t> ids;
	for (const std::string& name : parsed.program.signals) ids.push_back(signals().find(name));

	const Value passed = Value::fromText("hello");
	const EvalResult result = evaluate(parsed.program, signals(), ids, &passed);
	CHECK(result.ok);
	CHECK(signals().text(signals().find("text")) == "hello");
}

TEST(a_value_binding_takes_no_parameters) {
	const ParseResult parsed = parseBinding("(v) => v", false);
	CHECK(!parsed.ok);
	CHECK(parsed.error.find("no parameters") != std::string::npos);
}

TEST(a_handler_takes_at_most_one_parameter) {
	const ParseResult parsed = parseBinding("(a, b) => state.x = a", true);
	CHECK(!parsed.ok);
	CHECK(parsed.error.find("at most one") != std::string::npos);
}
