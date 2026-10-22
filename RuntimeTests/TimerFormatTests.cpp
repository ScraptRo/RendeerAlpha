#include "TestHarness.h"

#include <Core/Signals.h>
#include <Layout/Blueprint.h>
#include <Layout/Expression.h>
#include <Layout/ScriptHost.h>
#include <Layout/ScriptPreludes.h>
#include <vendor/quickjs/quickjs.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

// What QuickJS lacks and an interface wants: `format`, run here exactly as it is shipped,
// and the timers, on a module compiled by hand in the shape the compiler writes -- the
// prelude's four functions asking the engine, naming the module they belong to.
namespace {

	using namespace RDA;
	using namespace RDA::Layout;

	// ---- format ---------------------------------------------------------------------

	struct Formatter {
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);
		Formatter() {
			const std::string prelude = formatPrelude();
			JS_FreeValue(ctx, JS_Eval(ctx, prelude.c_str(), prelude.size(), "<format>", JS_EVAL_TYPE_GLOBAL));
		}
		~Formatter() { JS_FreeContext(ctx); JS_FreeRuntime(rt); }
		std::string operator()(const std::string& expression) {
			JSValue v = JS_Eval(ctx, expression.c_str(), expression.size(), "<case>", JS_EVAL_TYPE_GLOBAL);
			const char* text = JS_ToCString(ctx, JS_IsException(v) ? JS_GetException(ctx) : v);
			std::string out = text ? text : "";
			if (text) JS_FreeCString(ctx, text);
			JS_FreeValue(ctx, v);
			return out;
		}
	};

	// ---- timers ---------------------------------------------------------------------

	const char* kTimerModule =
		"(function (__rda, __rdaOwner) {\n"
		"\"use strict\";\n"
		"var state = new Proxy({}, { get: function (_, k) { return __rda.get(k); },\n"
		"  set: function (_, k, v) { __rda.set(k, v); return true; } });\n"
		"function setTimeout(f, ms) { return __rda.timer(__rdaOwner, f, Number(ms) || 0, false, Array.prototype.slice.call(arguments, 2)); }\n"
		"function setInterval(f, ms) { return __rda.timer(__rdaOwner, f, Number(ms) || 0, true, Array.prototype.slice.call(arguments, 2)); }\n"
		"function clearTimeout(id) { __rda.clearTimer(Number(id) || 0); }\n"
		"return { f: {\n"
		"  later: function (ms, v) { return setTimeout(function (x) { state.a = x; }, ms, v); },\n"
		"  every: function (ms) { return setInterval(function () { state.b = state.b + 1; }, ms); },\n"
		"  stop: function (id) { clearTimeout(id); return 0; },\n"
		"  bad: function () { return setTimeout(function () { throw new Error(\"late\"); }, 0); },\n"
		"  text: function () { return setTimeout(\"state.a = 1\", 0); },\n"
		"  chain: function () { return setTimeout(function () { setTimeout(function () { state.a = 99; }, 0); }, 0); }\n"
		"}, b: [], row: function () {} };\n"
		"})\n";

	BlueprintScript compiledTimers() {
		BlueprintScript script;
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);
		const std::string text = kTimerModule;
		JSValue compiled = JS_Eval(ctx, text.c_str(), text.size(), "rda:timers",
		                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
		size_t size = 0;
		uint8_t* bytes = JS_WriteObject(ctx, &size, compiled, JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE);
		script.code.assign(bytes, bytes + size);
		js_free(ctx, bytes);
		JS_FreeValue(ctx, compiled);
		JS_FreeContext(ctx);
		JS_FreeRuntime(rt);
		script.engine = JS_GetVersion();
		for (int line = 1; line <= 16; ++line) {
			if (line > 1) script.lineMap += "\n";
			script.lineMap += "timers.ts:" + std::to_string(line);
		}
		return script;
	}

	struct Timers {
		std::shared_ptr<ScriptModule> module;
		uint32_t a = 0, b = 0;
		Timers() {
			signals().clear();
			a = signals().define("a", 0.0);
			b = signals().define("b", 0.0);
			BlueprintBuilder builder;
			builder.addNode(builder.intern("container"), builder.intern("root"), kNoParent);
			builder.setScript(compiledTimers());
			Blueprint blueprint;
			std::string error;
			blueprint.parse(builder.finish(), error);
			module = scriptHost().load(blueprint, "timers", error);
		}
		~Timers() { module.reset(); signals().clear(); }

		double call(const char* name, std::vector<Value> args = {}, std::string* error = nullptr) {
			Value out;
			std::string why;
			const bool ok = scriptHost().call(module->function(name), args.data(), args.size(), out, why);
			if (error) *error = ok ? std::string() : why;
			return out.asNumber();
		}
	};

	void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}

TEST(format_numbers_the_way_a_person_writes_them) {
	Formatter f;
	CHECK(f("format.number(1234567.891)") == "1,234,567.891");
	CHECK(f("format.number(1234.5, { decimals: 2 })") == "1,234.50");
	CHECK(f("format.number(2.5)") == "2.5");
	CHECK(f("format.number(-1234)") == "-1,234");
	CHECK(f("format.number(-0.001, { decimals: 2 })") == "0.00");
	CHECK(f("format.number(1234.5, { group: '.', point: ',' })") == "1.234,5");
	CHECK(f("format.number(1234.5, { grouping: false })") == "1234.5");
	CHECK(f("format.number(5, { sign: true })") == "+5");
	CHECK(f("format.number(NaN)") == "NaN");
	CHECK(f("format.percent(0.256)") == "26%");
	CHECK(f("format.percent(0.2567, { decimals: 1 })") == "25.7%");
}

TEST(format_sizes_and_durations) {
	Formatter f;
	CHECK(f("format.bytes(0)") == "0 B");
	CHECK(f("format.bytes(1023)") == "1,023 B");
	CHECK(f("format.bytes(1024)") == "1 KB");
	CHECK(f("format.bytes(1536)") == "1.5 KB");
	CHECK(f("format.bytes(15.4 * 1024 * 1024)") == "15.4 MB");
	CHECK(f("format.bytes(1500, { si: true })") == "1.5 kB");
	CHECK(f("format.bytes(1536, { decimals: 2 })") == "1.50 KB");
	CHECK(f("format.duration(3723000)") == "1:02:03");
	CHECK(f("format.duration(65000)") == "1:05");
	CHECK(f("format.duration(5000)") == "0:05");
	CHECK(f("format.duration(-5000)") == "-0:05");
	CHECK(f("format.duration(3723000, { style: 'short' })") == "1h 2m");
	CHECK(f("format.duration(65000, { style: 'short' })") == "1m 5s");
	CHECK(f("format.duration(0, { style: 'short' })") == "0s");
}

TEST(format_dates_by_pattern_and_relative_to_now) {
	Formatter f;
	const std::string t = "Date.UTC(2026, 0, 5, 9, 7, 3, 45)";   // a Monday
	CHECK(f("format.date(" + t + ", 'yyyy-MM-dd HH:mm:ss.SSS', { utc: true })") == "2026-01-05 09:07:03.045");
	CHECK(f("format.date(" + t + ", \"EEEE, MMMM d 'at' h:mm a\", { utc: true })") == "Monday, January 5 at 9:07 AM");
	CHECK(f("format.date(" + t + ", 'yy/M/d EEE MMM', { utc: true })") == "26/1/5 Mon Jan");
	CHECK(f("format.date(" + t + ", undefined, { utc: true })") == "2026-01-05 09:07");
	CHECK(f("format.date(" + t + ", 'MMMM', { utc: true, months: ['ianuarie','februarie','martie','aprilie',"
	        "'mai','iunie','iulie','august','septembrie','octombrie','noiembrie','decembrie'] })") == "ianuarie");
	CHECK(f("format.date('not a date')") == "");
	const std::string now = "1767600000000";
	auto ago = [&](const std::string& ms) { return f("format.relative(" + now + " + (" + ms + "), " + now + ")"); };
	CHECK(ago("-10000") == "just now");
	CHECK(ago("60000 * -1") == "1 minute ago");
	CHECK(ago("60000 * -5") == "5 minutes ago");
	CHECK(ago("3600000 * 2") == "in 2 hours");
	CHECK(ago("3600000 * -26") == "yesterday");
	CHECK(ago("86400000 * -3") == "3 days ago");
	CHECK(ago("86400000 * -10") == "1 week ago");
	CHECK(ago("86400000 * -400") == "1 year ago");
}

TEST(a_timer_goes_off_once_and_only_after_its_time) {
	Timers fx;
	CHECK(fx.module != nullptr);
	CHECK(scriptHost().secondsToNextTimer() < 0.0);
	fx.call("later", { Value::fromNumber(30), Value::fromNumber(7) });
	CHECK(scriptHost().timerCount() == 1);
	const double next = scriptHost().secondsToNextTimer();
	CHECK(next > 0.0 && next <= 0.03);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 0.0);    // not yet
	sleepMs(45);
	CHECK(scriptHost().secondsToNextTimer() == 0.0);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 7.0);    // its argument, handed over
	CHECK(scriptHost().timerCount() == 0);
	signals().set(fx.a, 0.0);
	sleepMs(10);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 0.0);    // once
}

TEST(an_interval_keeps_its_beat_until_cleared) {
	Timers fx;
	const double id = fx.call("every", { Value::fromNumber(10) });
	CHECK(id > 0.0);
	for (int i = 0; i < 4; ++i) { sleepMs(12); scriptHost().tick(); }
	CHECK(signals().number(fx.b) >= 3.0);
	CHECK(signals().number(fx.b) <= 4.0);    // once a tick at most, however late the tick
	fx.call("stop", { Value::fromNumber(id) });
	CHECK(scriptHost().timerCount() == 0);
	const double before = signals().number(fx.b);
	sleepMs(15);
	scriptHost().tick();
	CHECK(signals().number(fx.b) == before);
}

TEST(a_timer_is_refused_where_nothing_may_happen) {
	Timers fx;
	std::string error;
	{
		const ScriptHost::Frame frame(ScriptHost::Mode::Value);
		fx.call("later", { Value::fromNumber(0), Value::fromNumber(1) }, &error);
	}
	CHECK(error.find("cannot start a timer") != std::string::npos);
	CHECK(scriptHost().timerCount() == 0);
	fx.call("text", {}, &error);
	CHECK(error.find("needs a function") != std::string::npos);
}

TEST(a_timer_that_throws_is_said_and_the_rest_still_go_off) {
	Timers fx;
	fx.call("bad");
	fx.call("later", { Value::fromNumber(0), Value::fromNumber(5) });
	sleepMs(2);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 5.0);
	CHECK(scriptHost().timerCount() == 0);
}

TEST(a_timer_started_by_a_timer_waits_for_the_next_tick) {
	Timers fx;
	fx.call("chain");
	sleepMs(2);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 0.0);
	CHECK(scriptHost().timerCount() == 1);
	sleepMs(2);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 99.0);
}

TEST(a_module_that_goes_takes_its_timers_with_it) {
	Timers fx;
	fx.call("later", { Value::fromNumber(0), Value::fromNumber(1) });
	fx.call("every", { Value::fromNumber(5) });
	CHECK(scriptHost().timerCount() == 2);
	fx.module.reset();
	CHECK(scriptHost().timerCount() == 0);
	sleepMs(10);
	scriptHost().tick();
	CHECK(signals().number(fx.a) == 0.0);
}
