#include "ScriptCompiler.h"
#include <Layout/ModuleTransform.h>
#include "JsContext.h"
#include "ScriptPreludes.h"
#include <Logger/Logger.h>
#include <vendor/quickjs/quickjs.h>
// acorn, and the two texts of ScriptCompiler's own: the module's prelude and the analysis,
// as the bytes of the files in js/, embedded by rda_embed_js.
#include <AcornSource.h>
#include <AnalyzerSource.h>
#include <RuntimePreludeSource.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace RDA::Layout {

	namespace {

		// ---- what ships: the module a layout's run-time code lives in -------------------
		//
		// Written into every module ahead of the layout's own code and compiled with it, so
		// nothing at run time is parsed: the engine hands the module one object, `__rda`,
		// of native functions, and this is what turns that into `state`, `commands`,
		// `tables` and `console` the way a layout writes them. See Layout/Script/ScriptNatives.cpp
		// for the other side. The text is js/runtime-prelude.js.
		//
		// `state` resolves a name through the layout's own scope first, because a signal the
		// layout declared with signal("open") is called "main/open" once scoped -- the same
		// rewrite the compiler does to a grammar binding's names, done here at run time.
		const char* const kRuntimePrelude = reinterpret_cast<const char*>(kRuntimePreludeSource);

		// ---- the analysis, which runs at build time ----------------------------------
		//
		// js/analyzer.js. JavaScript because acorn is, and because the values it snapshots
		// live in the context the layout was evaluated in. What follows is how it is called,
		// and how it is heard when it throws.

		std::string text(JSContext* ctx, JSValueConst value) {
			const char* raw = JS_ToCString(ctx, value);
			std::string out = raw ? raw : "";
			if (raw) JS_FreeCString(ctx, raw);
			return out;
		}

		// How an exception from the analysis is said. The message is what the analysis says,
		// less the "Error: " its throws begin with; its stack is the analysis's own, and of
		// no use to whoever wrote the layout. And if it cannot be said at all, nothing, rather
		// than a placeholder: acorn and the analysis throw Errors, which always have a message.
		ThrownFormat analysisThrown() {
			ThrownFormat format;
			format.stack = false;
			format.dropErrorPrefix = true;
			format.unprintable = "";
			return format;
		}

		std::string exception(JSContext* ctx) {
			return thrownText(ctx, analysisThrown());
		}

		// __rdaScript[method](...args), or an exception value.
		JSValue callAnalysis(JSContext* ctx, const char* method, int argc, JSValueConst* argv) {
			JSValue global = JS_GetGlobalObject(ctx);
			JSValue api = JS_GetPropertyStr(ctx, global, "__rdaScript");
			JSValue fn = JS_GetPropertyStr(ctx, api, method);
			JSValue result = JS_Call(ctx, fn, api, argc, argv);
			JS_FreeValue(ctx, fn);
			JS_FreeValue(ctx, api);
			JS_FreeValue(ctx, global);
			return result;
		}

		std::string trim(std::string s) {
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
			size_t at = 0;
			while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
			return s.substr(at);
		}
	}

	bool wrapBundle(const std::string& bundle, std::string& script, std::string& error) {
		std::unordered_map<std::string, std::string> exports;
		return wrapBundle(bundle, script, exports, error);
	}

	bool wrapBundle(const std::string& bundle, std::string& script,
	                std::unordered_map<std::string, std::string>& exports, std::string& error) {
		// esbuild ends a bundle with one export clause -- `export { Main as default, f };` --
		// whatever the files it bundled wrote. It is taken off, and remembered for the name
		// that is the default.
		exports.clear();
		std::string body = bundle;
		std::string defaultName = "undefined";
		size_t at = body.rfind("\nexport {");
		if (at == std::string::npos && body.rfind("export {", 0) == 0) at = 0;
		if (at != std::string::npos) {
			const size_t open = body.find('{', at);
			const size_t close = body.find('}', open);
			if (close == std::string::npos) {
				error = "the bundle's export clause is not closed";
				return false;
			}
			const std::string inside = body.substr(open + 1, close - open - 1);
			size_t from = 0;
			while (from <= inside.size()) {
				const size_t comma = inside.find(',', from);
				const std::string one = trim(inside.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
				const size_t as = one.find(" as ");
				const std::string local = trim(as == std::string::npos ? one : one.substr(0, as));
				const std::string exported = trim(as == std::string::npos ? one : one.substr(as + 4));
				if (!local.empty() && !exported.empty()) exports[exported] = local;
				if (exported == "default") defaultName = local;
				if (comma == std::string::npos) break;
				from = comma + 1;
			}
			size_t end = close + 1;
			if (end < body.size() && body[end] == ';') ++end;
			body.erase(at, end - at);
		}
		// Wrapped, so the bundle's top level stays its own -- as it was when this was an
		// IIFE -- and read afterwards through a direct eval, which is the one thing that can
		// see a function's local names from outside. Only while the layout is built.
		std::string table;
		for (const auto& [exported, local] : exports) {
			if (exported == "default") continue;
			if (!table.empty()) table += ", ";
			table += "\"" + exported + "\": " + local;
		}
		// What goes in front is kWrapLinesBefore lines long, and an error's line is mapped
		// back by that; the two are held together here rather than by remembering.
		constexpr std::string_view head = "var __rdaModule = (function () {\n";
		static_assert(static_cast<size_t>(std::count(head.begin(), head.end(), '\n')) == kWrapLinesBefore,
		              "kWrapLinesBefore has to say how many lines wrapBundle writes before the bundle");
		script = std::string(head) + body +
		         "\nreturn { default: " + defaultName + ", exports: { " + table + " }" +
		         ", get: function (__rdaName) { return eval(__rdaName); } };\n})();\n";
		return true;
	}

	ScriptPlanner::ScriptPlanner(JSContext* ctx, std::string bundle, std::string sourceMap,
	                             std::string entry, bool allowRuntimeCode)
		: mCtx(ctx), mBundle(std::move(bundle)), mSourceMap(std::move(sourceMap)),
		  mEntry(std::move(entry)), mAllow(allowRuntimeCode) {}

	ScriptPlanner::~ScriptPlanner() = default;

	bool ScriptPlanner::ensureAnalysis(std::string& error) {
		if (mAnalysed) return true;
		if (mAnalysisFailed) { error = mAnalysisError; return false; }

		// acorn and the analysis are loaded only now, the first time a binding needs them:
		// a layout whose bindings are all grammar never pays for parsing either.
		auto fail = [&](const std::string& why) {
			mAnalysisFailed = true;
			mAnalysisError = why;
			error = why;
			return false;
		};
		std::string why;
		if (!evalScript(mCtx, reinterpret_cast<const char*>(kAcornSource), kAcornSourceSize, "acorn.js", why,
		                analysisThrown()))
			return fail("cannot load acorn: " + why);
		if (!evalScript(mCtx, reinterpret_cast<const char*>(kAnalyzerSource), kAnalyzerSourceSize, "analysis.js", why,
		                analysisThrown()))
			return fail("cannot load the analysis: " + why);

		JSValue global = JS_GetGlobalObject(mCtx);
		JS_SetPropertyStr(mCtx, global, "__rdaRuntimePrelude", JS_NewString(mCtx, kRuntimePrelude));
		JS_SetPropertyStr(mCtx, global, "__rdaColourPrelude", JS_NewString(mCtx, colourPrelude()));
		JS_SetPropertyStr(mCtx, global, "__rdaFormatPrelude", JS_NewString(mCtx, formatPrelude()));
		JS_SetPropertyStr(mCtx, global, "__rdaTimerPrelude", JS_NewString(mCtx, timerPrelude()));
		JS_FreeValue(mCtx, global);

		JSValue args[3] = {
			JS_NewStringLen(mCtx, mBundle.data(), mBundle.size()),
			JS_NewStringLen(mCtx, mSourceMap.data(), mSourceMap.size()),
			JS_NewString(mCtx, mEntry.c_str()),
		};
		JSValue result = callAnalysis(mCtx, "analyse", 3, args);
		for (JSValue& one : args) JS_FreeValue(mCtx, one);
		if (JS_IsException(result)) return fail("cannot read the bundled layout: " + exception(mCtx));
		JS_FreeValue(mCtx, result);
		mAnalysed = true;
		return true;
	}

	bool ScriptPlanner::isFunction(const std::string& name) {
		std::string error;
		if (!ensureAnalysis(error)) return false;
		JSValue arg = JS_NewString(mCtx, name.c_str());
		JSValue result = callAnalysis(mCtx, "isFunction", 1, &arg);
		JS_FreeValue(mCtx, arg);
		const bool yes = !JS_IsException(result) && JS_ToBool(mCtx, result) == 1;
		if (JS_IsException(result)) (void)exception(mCtx);
		JS_FreeValue(mCtx, result);
		return yes;
	}

	bool ScriptPlanner::noteCalls(const std::string& source, const std::string& rowParam,
	                              const std::vector<std::string>& names, std::string& error) {
		if (!ensureAnalysis(error)) return false;
		// A name the code around the binding declares too would be the wrong function.
		JSValue list = JS_NewArray(mCtx);
		for (uint32_t i = 0; i < names.size(); ++i)
			JS_SetPropertyUint32(mCtx, list, i, JS_NewString(mCtx, names[i].c_str()));
		JSValue args[3] = {
			JS_NewStringLen(mCtx, source.data(), source.size()),
			list,
			rowParam.empty() ? JS_UNDEFINED : JS_NewString(mCtx, rowParam.c_str()),
		};
		JSValue result = callAnalysis(mCtx, "calls", 3, args);
		for (JSValue& one : args) JS_FreeValue(mCtx, one);
		if (JS_IsException(result)) { error = exception(mCtx); return false; }
		JSValue okValue = JS_GetPropertyStr(mCtx, result, "ok");
		const bool ok = JS_ToBool(mCtx, okValue) == 1;
		JS_FreeValue(mCtx, okValue);
		if (!ok) {
			JSValue why = JS_GetPropertyStr(mCtx, result, "error");
			error = text(mCtx, why);
			JS_FreeValue(mCtx, why);
		}
		JS_FreeValue(mCtx, result);
		if (!ok) return false;
		for (const std::string& name : names) {
			if (std::find(mCalls.begin(), mCalls.end(), name) == mCalls.end()) mCalls.push_back(name);
		}
		return true;
	}

	bool ScriptPlanner::setStateAliases(const std::unordered_map<std::string, std::string>& localToRuntime,
	                                    std::string& error) {
		if (!ensureAnalysis(error)) return false;
		mState = true;
		JSValue map = JS_NewObject(mCtx);
		for (const auto& [local, runtime] : localToRuntime)
			JS_SetPropertyStr(mCtx, map, local.c_str(), JS_NewString(mCtx, runtime.c_str()));
		JSValue result = callAnalysis(mCtx, "aliases", 1, &map);
		JS_FreeValue(mCtx, map);
		if (JS_IsException(result)) { error = exception(mCtx); return false; }
		JS_FreeValue(mCtx, result);
		return true;
	}

	bool ScriptPlanner::check(const std::string& source, bool isEvent, bool action, std::string& error) {
		JSValue args[5] = {
			JS_NewStringLen(mCtx, source.data(), source.size()),
			JS_UNDEFINED,
			JS_NewBool(mCtx, isEvent),
			JS_NewBool(mCtx, action),
			JS_NewBool(mCtx, !isEvent && !action),   // a computed value
		};
		JSValue result = callAnalysis(mCtx, "binding", 5, args);
		for (JSValue& one : args) JS_FreeValue(mCtx, one);
		if (JS_IsException(result)) { error = exception(mCtx); return false; }
		JSValue okValue = JS_GetPropertyStr(mCtx, result, "ok");
		const bool ok = JS_ToBool(mCtx, okValue) == 1;
		JS_FreeValue(mCtx, okValue);
		if (!ok) {
			JSValue why = JS_GetPropertyStr(mCtx, result, "error");
			error = text(mCtx, why);
			JS_FreeValue(mCtx, why);
		}
		JS_FreeValue(mCtx, result);
		return ok;
	}

	bool ScriptPlanner::addComputed(const std::string& name, const std::string& source, std::string& error) {
		if (!ensureAnalysis(error)) return false;
		std::string why;
		if (!check(source, /*isEvent*/ false, /*action*/ false, why)) {
			error = "state." + name + " is computed, and " + why;
			return false;
		}
		mComputed.push_back(Named{ name, source });
		return true;
	}

	bool ScriptPlanner::addAction(const std::string& name, const std::string& source, std::string& error) {
		if (!ensureAnalysis(error)) return false;
		std::string why;
		if (!check(source, /*isEvent*/ true, /*action*/ true, why)) {
			error = "commands." + name + ": " + why;
			return false;
		}
		mActions.push_back(Named{ name, source });
		return true;
	}

	bool ScriptPlanner::addWatch(const std::string& name, const std::string& source, std::string& error) {
		if (!ensureAnalysis(error)) return false;
		std::string why;
		if (!check(source, /*isEvent*/ true, /*action*/ true, why)) {
			error = "state." + name + "'s watch: " + why;
			return false;
		}
		mWatches.push_back(Named{ name, source });
		return true;
	}

	bool ScriptPlanner::addBinding(const std::string& source, const std::string& rowParam,
	                               bool isEvent, const std::string& grammarError,
	                               std::string& slot, std::string& error) {
		if (!mAllow) {
			error = grammarError + " -- and this build was asked for no run-time code "
			        "(--no-runtime-code), which is what it would otherwise have become";
			return false;
		}
		if (!ensureAnalysis(error)) return false;

		JSValue args[3] = {
			JS_NewStringLen(mCtx, source.data(), source.size()),
			rowParam.empty() ? JS_UNDEFINED : JS_NewString(mCtx, rowParam.c_str()),
			JS_NewBool(mCtx, isEvent),
		};
		JSValue result = callAnalysis(mCtx, "binding", 3, args);
		for (JSValue& one : args) JS_FreeValue(mCtx, one);
		if (JS_IsException(result)) {
			error = exception(mCtx);
			return false;
		}
		JSValue okValue = JS_GetPropertyStr(mCtx, result, "ok");
		const bool ok = JS_ToBool(mCtx, okValue) == 1;
		JS_FreeValue(mCtx, okValue);
		if (!ok) {
			JSValue why = JS_GetPropertyStr(mCtx, result, "error");
			error = text(mCtx, why);
			JS_FreeValue(mCtx, why);
			JS_FreeValue(mCtx, result);
			return false;
		}
		JSValue rowValue = JS_GetPropertyStr(mCtx, result, "row");
		const bool readsRow = JS_ToBool(mCtx, rowValue) == 1;
		JS_FreeValue(mCtx, rowValue);
		JS_FreeValue(mCtx, result);

		slot = (readsRow ? "%r" : "%") + std::to_string(mBindings.size());
		mBindings.push_back(Binding{ source, readsRow ? rowParam : std::string(), isEvent });
		return true;
	}

	bool ScriptPlanner::emit(const std::unordered_map<std::string, std::string>& declared,
	                         BlueprintScript& out, std::string& error) {
		if (!needed()) return true;
		if (!ensureAnalysis(error)) return false;

		// The plan, as a JavaScript object rather than text: nothing here has to be escaped.
		JSContext* ctx = mCtx;
		JSValue plan = JS_NewObject(ctx);
		JSValue calls = JS_NewArray(ctx);
		for (uint32_t i = 0; i < mCalls.size(); ++i)
			JS_SetPropertyUint32(ctx, calls, i, JS_NewString(ctx, mCalls[i].c_str()));
		JS_SetPropertyStr(ctx, plan, "calls", calls);
		JSValue bindings = JS_NewArray(ctx);
		for (uint32_t i = 0; i < mBindings.size(); ++i) {
			JSValue one = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, one, "src", JS_NewStringLen(ctx, mBindings[i].source.data(), mBindings[i].source.size()));
			JS_SetPropertyStr(ctx, one, "row", mBindings[i].row.empty() ? JS_UNDEFINED : JS_NewString(ctx, mBindings[i].row.c_str()));
			JS_SetPropertyStr(ctx, one, "event", JS_NewBool(ctx, mBindings[i].event));
			JS_SetPropertyUint32(ctx, bindings, i, one);
		}
		JS_SetPropertyStr(ctx, plan, "bindings", bindings);
		JSValue scope = JS_NewObject(ctx);
		for (const auto& [authored, scoped] : declared)
			JS_SetPropertyStr(ctx, scope, authored.c_str(), JS_NewString(ctx, scoped.c_str()));
		JS_SetPropertyStr(ctx, plan, "declared", scope);
		JS_SetPropertyStr(ctx, plan, "state", JS_NewBool(ctx, mState));
		auto named = [&](const std::vector<Named>& list) {
			JSValue array = JS_NewArray(ctx);
			for (uint32_t i = 0; i < list.size(); ++i) {
				JSValue one = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, one, "name", JS_NewString(ctx, list[i].name.c_str()));
				JS_SetPropertyStr(ctx, one, "src", JS_NewStringLen(ctx, list[i].source.data(), list[i].source.size()));
				JS_SetPropertyUint32(ctx, array, i, one);
			}
			return array;
		};
		JS_SetPropertyStr(ctx, plan, "computed", named(mComputed));
		JS_SetPropertyStr(ctx, plan, "actions", named(mActions));
		JS_SetPropertyStr(ctx, plan, "watches", named(mWatches));

		JSValue result = callAnalysis(ctx, "emit", 1, &plan);
		JS_FreeValue(ctx, plan);
		if (JS_IsException(result)) {
			error = exception(ctx);
			return false;
		}
		JSValue moduleValue = JS_GetPropertyStr(ctx, result, "module");
		JSValue mapValue = JS_GetPropertyStr(ctx, result, "map");
		JSValue warningsValue = JS_GetPropertyStr(ctx, result, "warnings");
		const std::string module = text(ctx, moduleValue);
		out.lineMap = text(ctx, mapValue);
		uint32_t count = 0;
		JSValue lengthValue = JS_GetPropertyStr(ctx, warningsValue, "length");
		JS_ToUint32(ctx, &count, lengthValue);
		JS_FreeValue(ctx, lengthValue);
		for (uint32_t i = 0; i < count; ++i) {
			JSValue one = JS_GetPropertyUint32(ctx, warningsValue, i);
			mWarnings.push_back(text(ctx, one));
			JS_FreeValue(ctx, one);
		}
		JS_FreeValue(ctx, warningsValue);
		JS_FreeValue(ctx, mapValue);
		JS_FreeValue(ctx, moduleValue);
		JS_FreeValue(ctx, result);

		// What was written, before it becomes bytecode that no longer says: for whoever is
		// working on this compiler, or wondering what of their layout runs later.
		if (const char* show = std::getenv("RDA_SHOW_RUNTIME_MODULE"); show && *show == '1') {
			RDA_LOG_WARNING("run-time module for " << mEntry << ":\n" << module);
		}

		// Compiled in a runtime of its own, so nothing of the build's context comes along:
		// what ships is the module and only the module.
		JsContext bytecode;
		if (!bytecode) {
			error = "cannot create a JS runtime to compile the layout's functions in";
			return false;
		}
		JSContext* clean = bytecode.get();
		const std::string fileName = "rda:" + std::filesystem::path(mEntry).stem().string();
		JSValue compiled = JS_Eval(clean, module.c_str(), module.size(), fileName.c_str(),
		                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
		bool ok = true;
		if (JS_IsException(compiled)) {
			// The module is generated, so a syntax error in it is the compiler's -- said with
			// the module, which is the only place it can be found.
			error = "the run-time module did not compile: " + exception(clean) + "\n" + module;
			ok = false;
		} else {
			size_t size = 0;
			uint8_t* bytes = JS_WriteObject(clean, &size, compiled,
			                                JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE);
			if (!bytes) {
				error = "cannot write the run-time module as bytecode";
				ok = false;
			} else {
				out.code.assign(bytes, bytes + size);
				js_free(clean, bytes);
				out.engine = JS_GetVersion();
			}
		}
		JS_FreeValue(clean, compiled);
		return ok;
	}
}
