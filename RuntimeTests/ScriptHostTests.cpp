#include "TestHarness.h"

#include <Core/Commands.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Layout/Blueprint.h>
#include <Layout/Expression.h>
#include <Layout/ExpressionParser.h>
#include <Layout/ScriptHost.h>
#include <vendor/quickjs/quickjs.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// The run-time half of a layout's functions, on its own: a module compiled here by hand in
// the shape the layout compiler writes, loaded the way a blueprint's is, and called the
// two ways the VM calls it. What is checked is the contract the rest of the engine leans
// on -- what a call reads is recorded, a value binding cannot write, a function that runs
// away is stopped, an error says where in the source it came from, and a module that has
// gone cannot be run.
//
// The compiler's half -- which functions a binding reaches, what ships as code and what as
// a value -- needs esbuild, and is the rda_runtime_code test.
namespace {

	using namespace RDA;
	using namespace RDA::Layout;

	// The same shape as a compiled module: a function of the natives, returning what it
	// exports. Its state proxy is cut down to what these tests read; the real one also
	// resolves a layout's own scoped names. Line numbers matter: the line map below is
	// "test.tsx:<line>" for each, so an error thrown on line 6 should say test.tsx:6.
	const char* kModule =
		"(function (__rda) {\n"                                                             // 1
		"\"use strict\";\n"                                                                 // 2
		"var state = new Proxy({}, { get: function (_, k) { return __rda.get(k); },\n"      // 3
		"  set: function (_, k, v) { __rda.set(k, v); return true; } });\n"                 // 4
		"function pick() { return state.flag ? state.a : state.b; }\n"                      // 5
		"function boom(n) { if (n > 1) throw new Error(\"too big\"); return n * 10; }\n"    // 6
		"function spin() { for (;;) {} }\n"                                                 // 7
		"function write() { state.a = 5; return 1; }\n"                                     // 8
		"function greet(name, times) { return name.repeat(times); }\n"                      // 9
		"function rows() { return __rda.rows(__rda.table(\"files\")); }\n"                  // 10
		"return { f: { pick: pick, boom: boom, spin: spin, write: write, greet: greet, rows: rows },\n" // 11
		"  b: [ (__rdaNoRow) => (() => state.a + 1),\n"                                     // 12
		"       (item) => ((v) => { state.b = v; return item; }) ],\n"                      // 13
		"  row: function (t, i) { return \"row \" + i; } };\n"                              // 14
		"})\n";                                                                             // 15

	// What the compiler would have written: the bytecode, its engine, and the line map.
	BlueprintScript compiledModule() {
		BlueprintScript script;
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);
		const std::string text = kModule;
		JSValue compiled = JS_Eval(ctx, text.c_str(), text.size(), "rda:test",
		                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
		size_t size = 0;
		uint8_t* bytes = JS_WriteObject(ctx, &size, compiled, JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE);
		script.code.assign(bytes, bytes + size);
		js_free(ctx, bytes);
		JS_FreeValue(ctx, compiled);
		JS_FreeContext(ctx);
		JS_FreeRuntime(rt);
		script.engine = JS_GetVersion();
		for (int line = 1; line <= 15; ++line) {
			if (line > 1) script.lineMap += "\n";
			script.lineMap += "test.tsx:" + std::to_string(line);
		}
		return script;
	}

	// A blueprint carrying it: one node, so it parses, and the script section after.
	Blueprint blueprintWith(const BlueprintScript& script) {
		BlueprintBuilder builder;
		builder.addNode(builder.intern("container"), builder.intern("root"), kNoParent);
		builder.setScript(script);
		Blueprint out;
		std::string error;
		out.parse(builder.finish(), error);
		return out;
	}

	struct Fixture {
		std::shared_ptr<ScriptModule> module;
		uint32_t flag = 0, a = 0, b = 0;
		Fixture() {
			signals().clear();
			flag = signals().define("flag", false);
			a = signals().define("a", 1.0);
			b = signals().define("b", 2.0);
			std::string error;
			const Blueprint blueprint = blueprintWith(compiledModule());
			module = scriptHost().load(blueprint, "test", error);
		}
		~Fixture() { module.reset(); signals().clear(); }

		Value call(const char* name, std::vector<Value> args, std::string* error = nullptr) {
			Value out;
			std::string why;
			const bool ok = scriptHost().call(module->function(name), args.data(), args.size(), out, why);
			if (error) *error = ok ? std::string() : why;
			return out;
		}
	};

	bool contains(const std::vector<uint32_t>& list, uint32_t id) {
		return std::find(list.begin(), list.end(), id) != list.end();
	}
}

TEST(a_blueprint_carries_its_functions_and_reads_them_back) {
	const BlueprintScript script = compiledModule();
	const Blueprint blueprint = blueprintWith(script);
	CHECK(blueprint.valid());
	CHECK(blueprint.hasScript());
	CHECK(blueprint.script().code == script.code);
	CHECK(blueprint.script().engine == script.engine);
	CHECK(blueprint.script().lineMap == script.lineMap);

	// One without functions is written exactly as before, as version 2.
	BlueprintBuilder plain;
	plain.addNode(plain.intern("container"), plain.intern("root"), kNoParent);
	const std::vector<uint8_t> bytes = plain.finish();
	BlueprintHeader header{};
	std::memcpy(&header, bytes.data(), sizeof(header));
	CHECK(header.version == kBlueprintVersion);
}

TEST(a_module_loads_and_names_its_functions) {
	Fixture fx;
	CHECK(fx.module != nullptr);
	CHECK(scriptHost().running());
	CHECK(fx.module->function("pick") != kNoScript);
	CHECK(fx.module->function("nothing") == kNoScript);
	CHECK(fx.module->binding(0) != kNoScript);
	CHECK(fx.module->binding(9) == kNoScript);
}

TEST(a_call_records_what_it_read_and_only_that) {
	Fixture fx;
	{
		ScriptHost::Frame frame(ScriptHost::Mode::Value);
		CHECK(fx.call("pick", {}).asNumber() == 2.0);
		CHECK(contains(frame.signals(), fx.flag));
		CHECK(contains(frame.signals(), fx.b));
		CHECK(!contains(frame.signals(), fx.a));   // the branch not taken
	}
	signals().set(fx.flag, true);
	{
		ScriptHost::Frame frame(ScriptHost::Mode::Value);
		CHECK(fx.call("pick", {}).asNumber() == 1.0);
		CHECK(contains(frame.signals(), fx.a));
		CHECK(!contains(frame.signals(), fx.b));
	}
}

TEST(arguments_and_results_cross_as_the_three_kinds_a_binding_has) {
	Fixture fx;
	const Value out = fx.call("greet", { Value::fromText("ab"), Value::fromNumber(3) });
	CHECK(out.kind == Value::Kind::Text);
	CHECK(out.text == "ababab");
}

TEST(a_value_binding_cannot_write_and_a_handler_can) {
	Fixture fx;
	std::string error;
	{
		ScriptHost::Frame frame(ScriptHost::Mode::Value);
		fx.call("write", {}, &error);
	}
	CHECK(error.find("cannot write state.a") != std::string::npos);
	CHECK(signals().number(fx.a) == 1.0);
	{
		ScriptHost::Frame frame(ScriptHost::Mode::Handler);
		fx.call("write", {}, &error);
	}
	CHECK(error.empty());
	CHECK(signals().number(fx.a) == 5.0);
}

TEST(an_error_says_where_in_the_source_it_was_thrown) {
	Fixture fx;
	std::string error;
	CHECK(fx.call("boom", { Value::fromNumber(1) }, &error).asNumber() == 10.0);
	fx.call("boom", { Value::fromNumber(2) }, &error);
	CHECK(error.find("too big") != std::string::npos);
	CHECK(error.find("test.tsx:6") != std::string::npos);
	CHECK(error.find("rda:test") == std::string::npos);
}

TEST(a_call_that_runs_away_is_stopped) {
	Fixture fx;
	std::string error;
	fx.call("spin", {}, &error);
	CHECK(error.find("was stopped") != std::string::npos);
	// And the runtime is still usable afterwards.
	CHECK(fx.call("boom", { Value::fromNumber(1) }, &error).asNumber() == 10.0);
	CHECK(error.empty());
}

TEST(the_vm_calls_a_function_and_runs_a_binding) {
	Fixture fx;
	signals().set(fx.a, 41.0);
	const CallableName callable = [](const std::string& name) { return name == "boom"; };
	const ParseResult parsed = parseBinding("() => boom(state.a / 41) + 1", false, {}, callable);
	CHECK(parsed.ok);
	std::vector<uint32_t> ids;
	for (const std::string& pooled : parsed.program.signals) {
		ids.push_back(pooled.front() == '$' ? fx.module->function(pooled.substr(1)) : signals().find(pooled));
	}
	const EvalResult called = evaluate(parsed.program, signals(), ids);
	CHECK(called.ok);
	CHECK(called.value.asNumber() == 11.0);

	// A whole binding in JavaScript: one instruction, naming it.
	Program program;
	program.code = { { Op::RunScript, 0 }, { Op::Halt, 0 } };
	program.signals = { "%0" };
	const EvalResult ran = evaluate(program, signals(), { fx.module->binding(0) });
	CHECK(ran.ok);
	CHECK(ran.value.asNumber() == 42.0);

	// A handler, with the event and the row.
	Table table;
	const RowRef row{ &table, 3 };
	const Value event = Value::fromNumber(9);
	program.signals = { "%r1" };
	const EvalResult handled = evaluate(program, signals(), { fx.module->binding(1) }, &event, &row);
	CHECK(handled.ok);
	CHECK(signals().number(fx.b) == 9.0);
}

TEST(a_function_that_throws_is_a_script_error_not_a_broken_program) {
	Fixture fx;
	const CallableName callable = [](const std::string& name) { return name == "boom"; };
	const ParseResult parsed = parseBinding("() => boom(5)", false, {}, callable);
	const std::vector<uint32_t> ids = { fx.module->function("boom") };
	const EvalResult result = evaluate(parsed.program, signals(), ids);
	CHECK(!result.ok);
	CHECK(result.scriptError);
}

TEST(a_released_module_cannot_be_run) {
	Fixture fx;
	const uint32_t id = fx.module->function("pick");
	fx.module.reset();
	Value out;
	std::string error;
	CHECK(!scriptHost().call(id, nullptr, 0, out, error));
	CHECK(error.find("unloaded") != std::string::npos);
}

TEST(bytecode_from_another_quickjs_is_refused) {
	BlueprintScript script = compiledModule();
	script.engine = "0.0.0-elsewhere";
	const Blueprint blueprint = blueprintWith(script);
	std::string error;
	CHECK(scriptHost().load(blueprint, "old", error) == nullptr);
	CHECK(error.find("rebuild") != std::string::npos);
}
