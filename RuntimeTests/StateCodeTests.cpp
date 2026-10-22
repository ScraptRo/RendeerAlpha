#include "TestHarness.h"

#include <Core/Commands.h>
#include <Core/Json.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Layout/Blueprint.h>
#include <Layout/Expression.h>
#include <Layout/ExpressionParser.h>
#include <Layout/ScriptHost.h>
#include <vendor/quickjs/quickjs.h>

#include <limits>
#include <string>
#include <vector>

// Logic in the state: what a command carries, an action answering one, and a computed
// signal kept current. The module below is compiled here by hand in the shape the compiler
// writes for state.ts -- `c` for computed values, `a` for actions -- and loaded the way a
// generated state module loads its bytes. The compiler's half needs esbuild and is the
// rda_state_code test.
namespace {

	using namespace RDA;
	using namespace RDA::Layout;

	// Line numbers matter: the line map says "test.ts:<line>" for each, so an action that
	// throws on line 15 should say test.ts:15.
	const char* kStateModule =
		"(function (__rda) {\n"                                                                          // 1
		"\"use strict\";\n"                                                                              // 2
		"var state = new Proxy({}, { get: function (_, k) { return __rda.get(k); },\n"                   // 3
		"  set: function (_, k, v) { __rda.set(k, v); return true; } });\n"                              // 4
		"var commands = new Proxy({}, { get: function (_, k) { return function () {\n"                   // 5
		"  return __rda.command(k, Array.prototype.slice.call(arguments)); }; } });\n"                   // 6
		"return { f: {}, b: [],\n"                                                                       // 7
		"  c: { total: () => state.price * state.qty,\n"                                                 // 8
		"       label: () => state.flag ? \"on \" + state.total : \"off\",\n"                            // 9
		"       rows: () => __rda.rows(__rda.table(\"files\")),\n"                                       // 10
		"       capped: () => { if (state.qty > 5) throw new Error(\"too many\"); return state.qty; } },\n" // 11
		"  a: { add: (n) => { state.qty = state.qty + n; return state.qty; },\n"                         // 12
		"       twice: (n) => commands.add(n) + commands.add(n),\n"                                      // 13
		"       writeTotal: () => { state.total = 1; },\n"                                               // 14
		"       fail: () => { state.qty = 0; var none = null; return none.x; },\n"                       // 15
		"       echo: (o, s) => ({ got: o.k, s: s }) },\n"                                               // 16
		"  row: function () {} };\n"                                                                     // 17
		"})\n";                                                                                          // 18

	BlueprintScript compiledState(const std::string& engine = JS_GetVersion()) {
		BlueprintScript script;
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);
		const std::string text = kStateModule;
		JSValue compiled = JS_Eval(ctx, text.c_str(), text.size(), "rda:state",
		                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
		size_t size = 0;
		uint8_t* bytes = JS_WriteObject(ctx, &size, compiled, JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE);
		script.code.assign(bytes, bytes + size);
		js_free(ctx, bytes);
		JS_FreeValue(ctx, compiled);
		JS_FreeContext(ctx);
		JS_FreeRuntime(rt);
		script.engine = engine;
		for (int line = 1; line <= 18; ++line) {
			if (line > 1) script.lineMap += "\n";
			script.lineMap += "test.ts:" + std::to_string(line);
		}
		return script;
	}

	// What a backend's define() does first: the signals and commands exist, and then the
	// code that computes and answers them is loaded -- here from the bytes a generated
	// module carries.
	struct Fixture {
		uint32_t price = 0, qty = 0, flag = 0, total = 0, label = 0, rows = 0, capped = 0;
		uint32_t files = 0;
		bool loaded = false;
		std::string error;
		Fixture() {
			signals().clear();
			price = signals().define("price", 4.0);
			qty = signals().define("qty", 1.0);
			flag = signals().define("flag", false);
			total = signals().define("total", 0.0);
			label = signals().define("label", std::string_view(""));
			rows = signals().define("rows", 0.0);
			capped = signals().define("capped", 0.0);
			files = tables().define("files");
			tables().at(files).resize(0);
			for (const char* name : { "add", "twice", "writeTotal", "fail", "echo" }) {
				commands().bind(commands().define(name), CommandWork{});
			}
			const std::vector<uint8_t> bytes = writeScriptSection(compiledState());
			loaded = scriptHost().loadState(bytes.data(), bytes.size(), error);
		}
		~Fixture() {
			scriptHost().unloadState();
			signals().clear();
		}
	};

	std::string call(const char* name, const std::string& args, std::string* error = nullptr) {
		std::string result, why;
		const bool ok = commands().call(commands().find(name), CommandArgs(args), result, why);
		if (error) *error = ok ? std::string() : why;
		return ok ? result : std::string("<failed>");
	}
}

// ---- what a command carries -------------------------------------------------------------

TEST(json_reads_back_what_it_writes) {
	std::string written = "[";
	Json::appendNumber(written, 3);
	written += ",";
	Json::appendNumber(written, 0.1);
	written += ",";
	Json::appendString(written, "a \"quoted\" \\ line\n\t\x01 \xC3\xA9");
	written += ",";
	Json::appendNumber(written, std::numeric_limits<double>::quiet_NaN());
	written += "]";
	std::vector<Json::Item> items;
	CHECK(Json::readArray(written, items));
	CHECK(items.size() == 4);
	CHECK(items[0].raw == "3");
	CHECK(items[1].number == 0.1);
	CHECK(items[2].text == "a \"quoted\" \\ line\n\t\x01 \xC3\xA9");
	CHECK(items[3].kind == Json::Item::Kind::Null);

	CHECK(Json::readArray(" [ true , null, [1, [2]], {\"k\": [3]}, \"\\u00e9\\ud83d\\ude00\" ] ", items));
	CHECK(items.size() == 5);
	CHECK(items[0].flag);
	CHECK(items[2].kind == Json::Item::Kind::Array && items[2].raw == "[1, [2]]");
	CHECK(items[3].kind == Json::Item::Kind::Object && items[3].raw == "{\"k\": [3]}");
	CHECK(items[4].text == "\xC3\xA9\xF0\x9F\x98\x80");

	std::string why;
	for (const char* bad : { "", "[1,", "[1] x", "{}", "[\"\x01\"]", "[01x]", "[1,]", "[\"\\q\"]" }) {
		CHECK(!Json::readArray(bad, items, &why));
		CHECK(!why.empty());
	}
}

TEST(arguments_are_read_by_position_with_a_fallback) {
	CommandArgs args;
	args.addNumber(3).addText("notes.txt").addBool(true).addJson("{\"k\":1}");
	CHECK(args.json() == "[3,\"notes.txt\",true,{\"k\":1}]");
	CHECK(args.size() == 4);
	CHECK(args.number(0) == 3.0);
	CHECK(args.text(1) == "notes.txt");
	CHECK(args.boolean(2));
	CHECK(args.raw(3) == "{\"k\":1}");
	// Missing, or of another kind: the fallback, never a throw in the middle of a click.
	CHECK(args.number(9, -1.0) == -1.0);
	CHECK(args.text(9, "none") == "none");
	CHECK(args.text(0) == "3");
	CHECK(CommandArgs("[\"42\"]").number(0) == 42.0);
	CHECK(CommandArgs().empty());
	CHECK(CommandArgs().json() == "[]");
}

TEST(an_action_answers_first_and_a_backend_watches_after) {
	const uint32_t id = commands().define("test_watched");
	std::string order;
	commands().setAction(id, [&order](const CommandArgs& args, std::string& result, std::string&) {
		order += "action(" + args.json() + ") ";
		result = "7";
		return true;
	});
	commands().bind(id, CommandWork([&order](const CommandArgs& args) { order += "work(" + args.json() + ")"; }));
	std::string result, error;
	CHECK(commands().call(id, CommandArgs("[1]"), result, error));
	CHECK(order == "action([1]) work([1])");
	CHECK(result == "7");

	// An action that fails: nothing happened that a watcher should hear of.
	order.clear();
	commands().setAction(id, [&order](const CommandArgs&, std::string&, std::string& why) {
		order += "action ";
		why = "no";
		return false;
	});
	CHECK(!commands().call(id, CommandArgs("[]"), result, error));
	CHECK(error == "no");
	CHECK(order == "action ");
	CHECK(commands().invoke(id));   // answered, even if badly: not "nothing answers it"

	// A backend's command returns null to a caller that asks for a result.
	commands().setAction(id, nullptr);
	CHECK(commands().call(id, CommandArgs("[2]"), result, error));
	CHECK(result == "null");

	const uint32_t none = commands().define("test_nobody");
	CHECK(!commands().call(none, CommandArgs(), result, error));
	CHECK(error.find("nothing answers") != std::string::npos);
	CHECK(!commands().invoke(none));
	commands().bind(id, CommandWork{});
}

// ---- computed signals -------------------------------------------------------------------

TEST(an_eager_observer_runs_inside_the_write) {
	signals().clear();
	const uint32_t a = signals().define("a", 1.0);
	const uint32_t b = signals().define("b", 0.0);
	const uint32_t c = signals().define("c", 0.0);
	const ObserverId forB = signals().newObserver();
	const ObserverId forC = signals().newObserver();
	signals().setEager(forB, true);
	signals().setEager(forC, true);
	signals().observe(a, forB);
	signals().observe(b, forC);
	int runs = 0;
	signals().setEagerHandler([&](ObserverId who) {
		++runs;
		if (who == forB) signals().set(b, signals().number(a) * 2);
		if (who == forC) signals().set(c, signals().number(b) + 1);
	});
	signals().set(a, 5.0);
	// Both current before the write returned: a chain, run in order, in one loop.
	CHECK(signals().number(b) == 10.0);
	CHECK(signals().number(c) == 11.0);
	CHECK(runs == 2);
	CHECK(signals().dirty().empty());   // eager observers are not left for the frame

	// Two that keep changing each other are stopped rather than run for ever.
	signals().observe(c, forB);
	signals().setEagerHandler([&](ObserverId who) {
		++runs;
		if (who == forB) signals().set(b, signals().number(c) + 1);
		if (who == forC) signals().set(c, signals().number(b) + 1);
	});
	runs = 0;
	signals().set(a, 6.0);
	CHECK(runs > 1000);
	CHECK(runs <= 10001);
	signals().setEagerHandler(nullptr);
	signals().clear();
}

TEST(a_computed_signal_cannot_be_written_by_a_handler) {
	signals().clear();
	const uint32_t total = signals().define("total", 0.0);
	signals().setComputed(total, true);
	const ParseResult parsed = parseBinding("() => state.total = 3", /*handler*/ true);
	CHECK(parsed.ok);
	const EvalResult result = evaluate(parsed.program, signals(), { total });
	CHECK(!result.ok);
	CHECK(result.error.find("computed") != std::string::npos);
	CHECK(signals().number(total) == 0.0);
	signals().clear();
}

TEST(state_code_loads_and_works_out_its_values_at_once) {
	Fixture fx;
	CHECK(fx.loaded);
	CHECK(fx.error.empty());
	CHECK(scriptHost().stateLoaded());
	CHECK(signals().number(fx.total) == 4.0);
	CHECK(signals().text(fx.label) == "off");
	CHECK(signals().computed(fx.total));
	CHECK(!signals().computed(fx.price));
	CHECK(commands().hasAction(commands().find("add")));
}

TEST(a_computed_signal_is_current_before_the_write_returns) {
	Fixture fx;
	signals().set(fx.price, 10.0);
	CHECK(signals().number(fx.total) == 10.0);   // no frame in between
	signals().set(fx.qty, 3.0);
	CHECK(signals().number(fx.total) == 30.0);
	// What it reads changes with a branch: label reads only `flag` until it is on.
	CHECK(signals().text(fx.label) == "off");
	signals().set(fx.flag, true);
	CHECK(signals().text(fx.label) == "on 30");
	signals().set(fx.qty, 4.0);
	CHECK(signals().text(fx.label) == "on 40");   // and now total, through it
}

TEST(a_computed_signal_that_throws_keeps_its_last_value) {
	Fixture fx;
	signals().set(fx.qty, 5.0);
	CHECK(signals().number(fx.capped) == 5.0);
	signals().set(fx.qty, 6.0);
	CHECK(signals().number(fx.capped) == 5.0);
	signals().set(fx.qty, 2.0);
	CHECK(signals().number(fx.capped) == 2.0);   // and comes back when it can
}

TEST(a_computed_signal_follows_a_table_once_a_frame) {
	Fixture fx;
	CHECK(signals().number(fx.rows) == 0.0);
	tables().at(fx.files).resize(3);
	CHECK(signals().number(fx.rows) == 0.0);     // a table has no observers...
	scriptHost().tick();
	CHECK(signals().number(fx.rows) == 3.0);     // ...so it is asked, once a frame
}

TEST(an_action_answers_its_command_with_json_in_and_out) {
	Fixture fx;
	std::string error;
	CHECK(call("add", "[2]", &error) == "3");
	CHECK(signals().number(fx.qty) == 3.0);
	CHECK(signals().number(fx.total) == 12.0);
	CHECK(call("echo", "[{\"k\":5},\"x\"]") == "{\"got\":5,\"s\":\"x\"}");
	// From the grammar, as a handler calls it.
	const ParseResult parsed = parseBinding("() => commands.add(state.qty)", /*handler*/ true);
	CHECK(parsed.ok);
	std::vector<uint32_t> ids;
	for (const std::string& pooled : parsed.program.signals)
		ids.push_back(pooled.front() == '@' ? commands().find(pooled.substr(1)) : signals().find(pooled));
	CHECK(evaluate(parsed.program, signals(), ids).ok);
	CHECK(signals().number(fx.qty) == 6.0);
}

TEST(an_action_calling_an_action_keeps_javascript_values_and_tells_the_watcher) {
	Fixture fx;
	int watched = 0;
	std::string heard;
	commands().bind(commands().find("add"), CommandWork([&](const CommandArgs& args) { ++watched; heard = args.json(); }));
	CHECK(call("twice", "[1]") == "5");   // 2 + 3
	CHECK(signals().number(fx.qty) == 3.0);
	CHECK(watched == 2);
	CHECK(heard == "[1]");
	commands().bind(commands().find("add"), CommandWork{});
}

TEST(an_action_that_throws_says_where_and_its_writes_stand) {
	Fixture fx;
	signals().set(fx.qty, 4.0);
	std::string error;
	CHECK(call("fail", "[]", &error) == "<failed>");
	CHECK(error.find("test.ts:15") != std::string::npos);
	CHECK(signals().number(fx.qty) == 0.0);
	CHECK(call("writeTotal", "[]", &error) == "<failed>");
	CHECK(error.find("computed") != std::string::npos);
	CHECK(call("add", "not json", &error) == "<failed>");
	CHECK(error.find("JSON") != std::string::npos);
}

TEST(state_code_reloads_keeping_every_value_and_unloads_cleanly) {
	Fixture fx;
	signals().set(fx.qty, 3.0);
	const std::vector<uint8_t> bytes = writeScriptSection(compiledState());
	std::string error;
	CHECK(scriptHost().loadState(bytes.data(), bytes.size(), error));
	CHECK(signals().number(fx.qty) == 3.0);
	CHECK(signals().number(fx.total) == 12.0);
	signals().set(fx.price, 1.0);
	CHECK(signals().number(fx.total) == 3.0);    // the new code follows; the old one is gone

	scriptHost().unloadState();
	CHECK(!scriptHost().stateLoaded());
	CHECK(!signals().computed(fx.total));
	CHECK(!commands().hasAction(commands().find("add")));
	signals().set(fx.price, 2.0);
	CHECK(signals().number(fx.total) == 3.0);    // kept, and no longer followed
}

TEST(state_code_from_another_quickjs_is_refused_and_leaves_what_was_running) {
	Fixture fx;
	const std::vector<uint8_t> stale = writeScriptSection(compiledState("0.0.0-elsewhere"));
	std::string error;
	CHECK(!scriptHost().loadState(stale.data(), stale.size(), error));
	CHECK(error.find("regenerate") != std::string::npos);
	CHECK(scriptHost().stateLoaded());
	signals().set(fx.qty, 2.0);
	CHECK(signals().number(fx.total) == 8.0);
	const uint8_t junk[4] = { 1, 2, 3, 4 };
	CHECK(!scriptHost().loadState(junk, sizeof junk, error));
	CHECK(error.find("not well formed") != std::string::npos);
}

// ---- the C++ a declaration with code generates --------------------------------------------

#if defined(RDA_TEST_STATE_HEADER)
#include <RdaState.h>
#include <cmath>

TEST(the_generated_header_carries_its_code_and_types_its_handlers) {
	scriptHost().unloadState();
	signals().clear();
	State::define();
	CHECK(scriptHost().stateLoaded());
	// The starting values the build worked out, and the code that keeps them current.
	CHECK(State::total() == 5.0);
	CHECK(std::string(State::summary()) == "2 at 2.5");
	State::setPrice(1.25);
	CHECK(State::total() == 2.5);
	CHECK(std::string(State::summary()) == "2 at 1.25");
	CHECK(signals().computed(State::ids().total));

	// A command with arguments, handed to a handler typed from the declaration's params.
	double id = 0.0;
	std::string name;
	bool keep = false;
	State::onRename([&](double i, const std::string& n, bool k) { id = i; name = n; keep = k; });
	CHECK(commands().invoke(State::commandIds().rename,
	                        CommandArgs().addNumber(7).addText("seven").addBool(true)));
	CHECK(id == 7.0);
	CHECK(name == "seven");
	CHECK(keep);

	// An action, called from C++ the way any backend calls one, and its result.
	std::string result, error;
	CHECK(State::call("add", CommandArgs().addNumber(1), result, error));
	CHECK(result == "{\"qty\":3,\"total\":3.75}");
	CHECK(State::qty() == 3.0);

	// And one over a table, asked once a frame.
	tables().at(State::tableIds().lines).resize(4);
	scriptHost().tick();
	CHECK(State::lineCount() == 4.0);

	scriptHost().unloadState();
	signals().clear();
}
#endif
