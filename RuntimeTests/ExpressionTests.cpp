#include "TestHarness.h"
#include <Core/Signals.h>
#include <Layout/Expression.h>
#include <Layout/ExpressionParser.h>

#include <string>

// What a binding is allowed to say, and what it means.
//
// These are the contract for the grammar. The refusals matter as much as the successes:
// a construct that is accepted and quietly evaluates once is worse than one that fails
// to compile, because it looks right until the day the value changes.
using namespace RDA;
using namespace RDA::Layout;

namespace {

	// Parses, resolves the program's signals against the table, and runs it. Signals the
	// program names are created as numbers if they do not exist, which keeps a test that
	// only cares about arithmetic from having to declare anything.
	struct Run {
		bool        parsed = false;
		bool        ran = false;
		std::string error;
		Value       value;

		std::string text() const { return value.asText(); }
		double      number() const { return value.asNumber(); }
	};

	Run run(const std::string& source) {
		Run out;
		const ParseResult parsed = parseBinding(source);
		if (!parsed.ok) {
			out.error = parsed.error;
			return out;
		}
		out.parsed = true;

		std::vector<uint32_t> ids;
		for (const std::string& name : parsed.program.signals) {
			uint32_t id = signals().find(name);
			if (id == kNoSignal) id = signals().define(name, 0.0);
			ids.push_back(id);
		}

		const EvalResult result = evaluate(parsed.program, signals(), ids);
		out.ran = result.ok;
		out.error = result.error;
		out.value = result.value;
		return out;
	}

	// The error a source is refused with, or empty if it was accepted.
	std::string refusal(const std::string& source) {
		const ParseResult parsed = parseBinding(source);
		return parsed.ok ? std::string() : parsed.error;
	}

	struct Fixture {
		Fixture()  { signals().clear(); }
		~Fixture() { signals().clear(); }
	};
}

TEST(a_binding_evaluates_a_number) {
	Fixture fx;
	const Run r = run("() => 42");
	CHECK(r.ran);
	CHECK_EQ(r.number(), 42.0);
}

TEST(arithmetic_respects_precedence) {
	Fixture fx;
	CHECK_EQ(run("() => 2 + 3 * 4").number(), 14.0);
	CHECK_EQ(run("() => (2 + 3) * 4").number(), 20.0);
	CHECK_EQ(run("() => 10 - 4 - 3").number(), 3.0);   // left associative
	CHECK_EQ(run("() => -5 + 2").number(), -3.0);
}

TEST(a_binding_reads_a_signal) {
	Fixture fx;
	const uint32_t count = signals().define("count", 7.0);
	const Run r = run("() => state.count");
	CHECK(r.ran);
	CHECK_EQ(r.number(), 7.0);
	CHECK(count != kNoSignal);
}

TEST(a_binding_records_what_it_reads) {
	Fixture fx;
	const ParseResult parsed = parseBinding("() => state.width * state.scale + state.width");
	CHECK(parsed.ok);
	// Two distinct signals, and the one read twice appears once: this list is the
	// dependency set, so a duplicate would register the binding twice.
	CHECK_EQ(parsed.program.signals.size(), size_t(2));
	CHECK(parsed.program.signals[0] == "width");
	CHECK(parsed.program.signals[1] == "scale");
}

TEST(text_and_numbers_join_the_way_they_are_written) {
	Fixture fx;
	signals().define("count", 3.0);
	CHECK(run("() => 'items: ' + state.count").text() == "items: 3");
	// A whole number does not acquire decimals on its way to the screen.
	CHECK(run("() => state.count").value.asText() == "3");
}

TEST(a_template_literal_becomes_one_string) {
	Fixture fx;
	signals().define("count", 5.0);
	CHECK(run("() => `${state.count} items`").text() == "5 items");
	CHECK(run("() => `a${1 + 1}b${2 + 2}c`").text() == "a2b4c");
	CHECK(run("() => `no substitution`").text() == "no substitution");
	CHECK(run("() => ``").text() == "");
}

TEST(comparisons_and_logic_produce_booleans) {
	Fixture fx;
	signals().define("count", 4.0);
	CHECK(run("() => state.count > 3").value.truthy());
	CHECK(!run("() => state.count > 10").value.truthy());
	CHECK(run("() => state.count >= 4 && state.count < 5").value.truthy());
	CHECK(run("() => state.count === 4").value.truthy());
	CHECK(run("() => state.count !== 4").value.truthy() == false);
	CHECK(run("() => !(state.count > 10)").value.truthy());
}

TEST(a_ternary_picks_a_branch) {
	Fixture fx;
	signals().define("count", 1.0);
	CHECK(run("() => state.count > 3 ? 'many' : 'few'").text() == "few");
	signals().set(signals().find("count"), 9.0);
	CHECK(run("() => state.count > 3 ? 'many' : 'few'").text() == "many");
}

TEST(a_handler_writes_a_signal) {
	Fixture fx;
	const uint32_t count = signals().define("count", 0.0);
	const Run r = run("() => state.count = 5");
	CHECK(r.ran);
	CHECK_EQ(signals().number(count), 5.0);
}

TEST(a_handler_can_increment_and_compound_assign) {
	Fixture fx;
	const uint32_t count = signals().define("count", 10.0);
	run("() => state.count++");
	CHECK_EQ(signals().number(count), 11.0);
	run("() => state.count += 4");
	CHECK_EQ(signals().number(count), 15.0);
	run("() => state.count--");
	CHECK_EQ(signals().number(count), 14.0);
	run("() => state.count *= 2");
	CHECK_EQ(signals().number(count), 28.0);
}

TEST(a_write_goes_through_the_signals_own_type) {
	Fixture fx;
	const uint32_t open = signals().define("open", false);
	run("() => state.open = !state.open");
	CHECK(signals().boolean(open));
	run("() => state.open = !state.open");
	CHECK(!signals().boolean(open));
}

TEST(a_block_runs_every_statement) {
	Fixture fx;
	const uint32_t a = signals().define("a", 0.0);
	const uint32_t b = signals().define("b", 0.0);
	const Run r = run("() => { state.a = 1; state.b = state.a + 41 }");
	CHECK(r.ran);
	CHECK_EQ(signals().number(a), 1.0);
	CHECK_EQ(signals().number(b), 42.0);
}

TEST(writing_a_signal_marks_the_bindings_that_read_it) {
	Fixture fx;
	const uint32_t count = signals().define("count", 0.0);
	signals().observe(count, 1);

	run("() => state.count = 3");
	CHECK(signals().anyDirty());
	signals().clearDirty();

	// The same value again is not a change, even written through a handler.
	run("() => state.count = 3");
	CHECK(!signals().anyDirty());
}

// ---- what the grammar refuses ---------------------------------------------------

TEST(a_call_is_refused_by_name) {
	Fixture fx;
	const std::string message = refusal("() => formatDate(state.when)");
	CHECK(!message.empty());
	CHECK(message.find("calls are not compiled") != std::string::npos);
}

TEST(props_are_refused_with_the_reason) {
	Fixture fx;
	const std::string message = refusal("() => props.title");
	CHECK(!message.empty());
	CHECK(message.find("compiled") != std::string::npos);
}

TEST(an_unknown_identifier_is_refused) {
	Fixture fx;
	const std::string message = refusal("() => somethingElse");
	CHECK(!message.empty());
	CHECK(message.find("state.<name>") != std::string::npos);
}

TEST(nested_state_is_refused) {
	Fixture fx;
	CHECK(!refusal("() => state.user.name").empty());
}

TEST(arrays_and_objects_are_refused) {
	Fixture fx;
	CHECK(!refusal("() => [1, 2, 3]").empty());
	CHECK(!refusal("() => ({ a: 1 })").empty());
}

TEST(a_binding_with_parameters_is_refused) {
	Fixture fx;
	const std::string message = refusal("(event) => state.count++");
	CHECK(!message.empty());
	CHECK(message.find("no parameters") != std::string::npos);
}

TEST(unterminated_text_is_refused_rather_than_guessed) {
	Fixture fx;
	CHECK(!refusal("() => 'unfinished").empty());
	CHECK(!refusal("() => `unfinished").empty());
}

TEST(a_refusal_can_point_at_the_spot) {
	Fixture fx;
	const ParseResult parsed = parseBinding("() => 1 + nope");
	CHECK(!parsed.ok);
	const std::string pointed = pointAt("() => 1 + nope", parsed.position);
	// The caret lands under the offending word rather than at the start of the line.
	CHECK(pointed.find("\n          ^") != std::string::npos);
}

TEST(a_program_can_be_read_back) {
	Fixture fx;
	const ParseResult parsed = parseBinding("() => state.count + 1");
	CHECK(parsed.ok);
	const std::string listing = disassemble(parsed.program);
	CHECK(listing.find("load count") != std::string::npos);
	CHECK(listing.find("push.num 1") != std::string::npos);
	CHECK(listing.find("add") != std::string::npos);
}
