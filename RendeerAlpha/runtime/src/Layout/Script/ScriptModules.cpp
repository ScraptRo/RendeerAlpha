#include "ScriptInternal.h"
#include <Layout/Blueprint.h>
#include <Core/Tables.h>

namespace RDA::Layout {
	using namespace ScriptInternal;

	namespace {
		uint32_t addEntry(JSValue fn, ScriptModule* module) {
			Runtime& r = runtime();
			r.entries.push_back(Entry{ fn, module, true });
			return static_cast<uint32_t>(r.entries.size() - 1);
		}
	}

	// ---- ScriptModule --------------------------------------------------------------------

	ScriptModule::~ScriptModule() {
		Runtime& r = runtime();
		if (!r.ctx) return;
		r.modules.erase(mId);
		// Its timers go with it: a reloaded layout must not keep running the old one's.
		cancelTimers(mId);
		auto release = [&](uint32_t id) {
			if (id >= r.entries.size() || !r.entries[id].live) return;
			JS_FreeValue(r.ctx, r.entries[id].fn);
			r.entries[id] = Entry{};
		};
		for (const auto& [name, id] : mFunctions) release(id);
		for (const uint32_t id : mBindings) release(id);
		if (mState) {
			ModuleState* state = static_cast<ModuleState*>(mState);
			JS_FreeValue(r.ctx, state->row);
			JS_FreeValue(r.ctx, state->exports);
			delete state;
		}
	}

	uint32_t ScriptModule::function(std::string_view name) const {
		const auto found = mFunctions.find(std::string(name));
		return found == mFunctions.end() ? kNoScript : found->second;
	}

	uint32_t ScriptModule::binding(uint32_t index) const {
		return index < mBindings.size() ? mBindings[index] : kNoScript;
	}

	std::shared_ptr<ScriptModule> ScriptHost::load(const Blueprint& blueprint, const std::string& name,
	                                               std::string& error) {
		if (!blueprint.hasScript()) return nullptr;
		return loadModule(blueprint.script(), name, "rebuild the layout", error);
	}

	std::shared_ptr<ScriptModule> loadModule(const BlueprintScript& script, const std::string& name,
	                                         const char* remedy, std::string& error) {
		// Bytecode is QuickJS's own format, and changes between its releases. Refused rather
		// than read: what a mismatch does is undefined, and the fix is one rebuild.
		if (script.engine != JS_GetVersion()) {
			error = "its functions were compiled by QuickJS " + script.engine + " and this engine runs " +
			        JS_GetVersion() + " -- " + remedy;
			return nullptr;
		}
		if (!start(error)) return nullptr;
		Runtime& r = runtime();
		JSContext* ctx = r.ctx;

		std::shared_ptr<ScriptModule> module(new ScriptModule);
		module->mName = name;
		module->mId = ++r.nextModule;
		r.modules[module->mId] = module.get();
		{
			size_t from = 0;
			const std::string& map = script.lineMap;
			while (from <= map.size()) {
				const size_t end = map.find('\n', from);
				module->mLineMap.push_back(map.substr(from, end == std::string::npos ? std::string::npos : end - from));
				if (end == std::string::npos) break;
				from = end + 1;
			}
		}

		JSValue compiled = JS_ReadObject(ctx, script.code.data(), script.code.size(), JS_READ_OBJ_BYTECODE);
		if (JS_IsException(compiled)) {
			error = "its functions could not be read: " + describeException(ctx, module.get());
			return nullptr;
		}
		Call guard(r);
		JSValue wrapper = JS_EvalFunction(ctx, compiled);   // takes `compiled`
		if (JS_IsException(wrapper) || !JS_IsFunction(ctx, wrapper)) {
			error = "its functions could not be loaded: " + describeException(ctx, module.get());
			JS_FreeValue(ctx, wrapper);
			return nullptr;
		}
		// The natives, and which module this is -- what its timers are filed under.
		JSValue handed[2] = { r.natives, JS_NewInt64(ctx, module->mId) };
		JSValue exports = JS_Call(ctx, wrapper, JS_UNDEFINED, 2, handed);
		JS_FreeValue(ctx, wrapper);
		if (JS_IsException(exports)) {
			error = "its functions could not be started: " + describeException(ctx, module.get());
			return nullptr;
		}

		ModuleState* state = new ModuleState;
		state->exports = exports;
		state->row = JS_GetPropertyStr(ctx, exports, "row");
		module->mState = state;

		JSValue functions = JS_GetPropertyStr(ctx, exports, "f");
		JSPropertyEnum* names = nullptr;
		uint32_t count = 0;
		if (JS_IsObject(functions) &&
		    JS_GetOwnPropertyNames(ctx, &names, &count, functions, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
			for (uint32_t i = 0; i < count; ++i) {
				const char* key = JS_AtomToCString(ctx, names[i].atom);
				JSValue fn = JS_GetProperty(ctx, functions, names[i].atom);
				if (key && JS_IsFunction(ctx, fn)) module->mFunctions.emplace(key, addEntry(fn, module.get()));
				else JS_FreeValue(ctx, fn);
				if (key) JS_FreeCString(ctx, key);
			}
			JS_FreePropertyEnum(ctx, names, count);
		}
		JS_FreeValue(ctx, functions);

		JSValue bindings = JS_GetPropertyStr(ctx, exports, "b");
		uint32_t length = 0;
		JSValue lengthValue = JS_GetPropertyStr(ctx, bindings, "length");
		JS_ToUint32(ctx, &length, lengthValue);
		JS_FreeValue(ctx, lengthValue);
		for (uint32_t i = 0; i < length; ++i) {
			module->mBindings.push_back(addEntry(JS_GetPropertyUint32(ctx, bindings, i), module.get()));
		}
		JS_FreeValue(ctx, bindings);
		return module;
	}

	bool ScriptHost::call(uint32_t id, const Value* args, size_t argc, Value& out, std::string& error) {
		Runtime& r = runtime();
		if (!r.ctx || id >= r.entries.size() || !r.entries[id].live) {
			error = "the layout this function belonged to has been unloaded";
			return false;
		}
		JSContext* ctx = r.ctx;
		const Entry entry = r.entries[id];
		std::vector<JSValue> values;
		values.reserve(argc);
		for (size_t i = 0; i < argc; ++i) values.push_back(toJS(ctx, args[i]));

		Call guard(r);
		JSValue result = JS_Call(ctx, entry.fn, JS_UNDEFINED, static_cast<int>(argc), values.data());
		for (JSValue& one : values) JS_FreeValue(ctx, one);
		if (JS_IsException(result)) {
			error = describeException(ctx, entry.module);
			return false;
		}
		out = fromJS(ctx, result);
		JS_FreeValue(ctx, result);
		return true;
	}

	bool ScriptHost::run(uint32_t id, const RowRef* row, const Value* event, Value& out, std::string& error) {
		Runtime& r = runtime();
		if (!r.ctx || id >= r.entries.size() || !r.entries[id].live) {
			error = "the layout this binding belonged to has been unloaded";
			return false;
		}
		JSContext* ctx = r.ctx;
		const Entry entry = r.entries[id];

		Call guard(r);
		// The row, as the template's parameter: a view onto one row of the table, read
		// column by column as the binding asks.
		JSValue item = JS_UNDEFINED;
		if (row && row->table) {
			for (uint32_t t = 0; t < tables().count(); ++t) {
				if (&tables().at(t) != row->table) continue;
				JSValue args[2] = { JS_NewInt32(ctx, static_cast<int32_t>(t)),
				                    JS_NewFloat64(ctx, static_cast<double>(row->index)) };
				item = JS_Call(ctx, stateOf(*entry.module).row, JS_UNDEFINED, 2, args);
				break;
			}
			if (JS_IsException(item)) {
				error = describeException(ctx, entry.module);
				return false;
			}
		}
		JSValue binding = JS_Call(ctx, entry.fn, JS_UNDEFINED, 1, &item);
		JS_FreeValue(ctx, item);
		if (JS_IsException(binding)) {
			error = describeException(ctx, entry.module);
			return false;
		}
		JSValue passed = event ? toJS(ctx, *event) : JS_UNDEFINED;
		JSValue result = JS_Call(ctx, binding, JS_UNDEFINED, event ? 1 : 0, &passed);
		JS_FreeValue(ctx, passed);
		JS_FreeValue(ctx, binding);
		if (JS_IsException(result)) {
			error = describeException(ctx, entry.module);
			return false;
		}
		out = fromJS(ctx, result);
		JS_FreeValue(ctx, result);
		return true;
	}
}
