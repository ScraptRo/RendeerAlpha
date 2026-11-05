#include "ScriptInternal.h"
#include <Core/Commands.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Logger/Logger.h>

namespace RDA::Layout {
	using namespace ScriptInternal;

	namespace {
		// The engine's values a layout may also write; the rest of state.rda is a report.
		// The same list ExpressionParser keeps, for the same reason.
		bool engineWritable(std::string_view name) {
			return name == "rda.maximized" || name == "rda.minimized" ||
			       name == "rda.fullscreen" || name == "rda.open";
		}

		// ---- the natives: the one object a module is handed --------------------------

		JSValue nGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			const uint32_t id = signals().find(name);
			if (id == kNoSignal) {
				return JS_ThrowReferenceError(ctx, "state.%s is not declared -- nothing in state.ts, "
				                              "nor a signal() in the layout, names it", name.c_str());
			}
			recordSignal(id);
			switch (signals().type(id)) {
			case SignalType::Number: return JS_NewFloat64(ctx, signals().number(id));
			case SignalType::Bool:   return JS_NewBool(ctx, signals().boolean(id));
			case SignalType::Text: {
				const std::string_view text = signals().text(id);
				return JS_NewStringLen(ctx, text.data(), text.size());
			}
			}
			return JS_UNDEFINED;
		}

		JSValue nHas(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			return JS_NewBool(ctx, signals().find(name) != kNoSignal);
		}

		JSValue nSet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			if (readOnly()) {
				return JS_ThrowTypeError(ctx, "a value binding cannot write state.%s -- it runs whenever "
				                         "what it reads changes. Write it from an event handler", name.c_str());
			}
			if (name.rfind("rda.", 0) == 0 && !engineWritable(name)) {
				return JS_ThrowTypeError(ctx, "state.%s is the engine's, and read-only", name.c_str());
			}
			const uint32_t id = signals().find(name);
			if (id == kNoSignal) {
				return JS_ThrowReferenceError(ctx, "state.%s is not declared, so there is nothing to write",
				                              name.c_str());
			}
			if (signals().computed(id)) {
				return JS_ThrowTypeError(ctx, "state.%s is computed by state.ts, and nothing else writes it",
				                         name.c_str());
			}
			JSValueConst value = argc > 1 ? argv[1] : JS_UNDEFINED;
			// Through the signal's own type, as StoreSignal does: `state.count = "3"` is the
			// number three, not a signal that has stopped being a number.
			switch (signals().type(id)) {
			case SignalType::Number: {
				double n = 0.0;
				if (JS_ToFloat64(ctx, &n, value) < 0) return JS_EXCEPTION;
				signals().set(id, n);
				break;
			}
			case SignalType::Bool: signals().set(id, JS_ToBool(ctx, value) == 1); break;
			case SignalType::Text: signals().set(id, std::string_view(toText(ctx, value))); break;
			}
			return JS_UNDEFINED;
		}

		JSValue nCommand(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			if (readOnly()) {
				return JS_ThrowTypeError(ctx, "a value binding cannot call commands.%s -- it runs whenever "
				                         "what it reads changes. Call it from an event handler", name.c_str());
			}
			const uint32_t id = commands().find(name);
			if (id == kNoCommand) {
				return JS_ThrowReferenceError(ctx, "commands.%s is not a command the application declared",
				                              name.c_str());
			}
			// The arguments, as an array. A module compiled before commands took any passes
			// none, which is the same as an empty list.
			JSValueConst list = argc > 1 ? argv[1] : JS_UNDEFINED;
			Runtime& r = runtime();
			// An action: called directly, in this frame, so what it is given and what it
			// returns stay JavaScript values and a throw is the caller's to see.
			if (commands().hasAction(id) && JS_IsObject(r.actions)) {
				JSValue fn = JS_GetPropertyStr(ctx, r.actions, name.c_str());
				if (JS_IsFunction(ctx, fn)) {
					std::vector<JSValue> args = elementsOf(ctx, list);
					JSValue out = JS_Call(ctx, fn, JS_UNDEFINED, static_cast<int>(args.size()), args.data());
					for (JSValue& one : args) JS_FreeValue(ctx, one);
					JS_FreeValue(ctx, fn);
					if (JS_IsException(out)) return out;
					// A backend that binds an action watches it: told afterwards, with the
					// same arguments.
					if (commands().hasWork(id)) {
						commands().runWork(id, CommandArgs(JS_IsArray(list) ? jsonOf(ctx, list) : std::string("[]")));
					}
					return out;
				}
				JS_FreeValue(ctx, fn);
			}
			const CommandArgs args(JS_IsArray(list) ? jsonOf(ctx, list) : std::string("[]"));
			if (!commands().invoke(id, args)) {
				return JS_ThrowReferenceError(ctx, "nothing answers commands.%s -- no handler is bound "
				                              "to it, and state.ts does not answer it", name.c_str());
			}
			return JS_UNDEFINED;
		}

		JSValue nTable(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			const uint32_t id = tables().find(name);
			return JS_NewInt32(ctx, id == kNoTable ? -1 : static_cast<int32_t>(id));
		}

		bool tableArg(JSContext* ctx, JSValueConst value, uint32_t& id) {
			int32_t raw = -1;
			if (JS_ToInt32(ctx, &raw, value) < 0 || raw < 0 || !tables().valid(static_cast<uint32_t>(raw)))
				return false;
			id = static_cast<uint32_t>(raw);
			return true;
		}

		JSValue nRows(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			uint32_t id = 0;
			if (argc < 1 || !tableArg(ctx, argv[0], id)) return JS_NewInt32(ctx, 0);
			recordTable(id);
			return JS_NewFloat64(ctx, static_cast<double>(tables().at(id).rows()));
		}

		JSValue nCell(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			uint32_t id = 0;
			if (argc < 3 || !tableArg(ctx, argv[0], id)) return JS_UNDEFINED;
			recordTable(id);
			const Table& table = tables().at(id);
			double rowNumber = -1.0;
			JS_ToFloat64(ctx, &rowNumber, argv[1]);
			if (rowNumber < 0.0 || rowNumber >= static_cast<double>(table.rows())) return JS_UNDEFINED;
			const size_t row = static_cast<size_t>(rowNumber);
			const uint32_t column = table.column(toText(ctx, argv[2]));
			if (column == kNoColumn) return JS_UNDEFINED;
			switch (table.columnType(column)) {
			case ColumnType::Number: return JS_NewFloat64(ctx, table.number(row, column));
			case ColumnType::Bool:   return JS_NewBool(ctx, table.boolean(row, column));
			case ColumnType::Text: {
				const std::string_view text = table.text(row, column);
				return JS_NewStringLen(ctx, text.data(), text.size());
			}
			}
			return JS_UNDEFINED;
		}

		JSValue nColumn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			uint32_t id = 0;
			if (argc < 2 || !tableArg(ctx, argv[0], id)) return JS_NewBool(ctx, false);
			return JS_NewBool(ctx, tables().at(id).column(toText(ctx, argv[1])) != kNoColumn);
		}

		JSValue nLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			int32_t level = 0;
			if (argc > 0) JS_ToInt32(ctx, &level, argv[0]);
			const std::string line = argc > 1 ? toText(ctx, argv[1]) : std::string();
			// console.log is information, which the console shows only when asked to; a
			// warning or an error is shown whatever the level is.
			if (level >= 2)      RDA_LOG_ERROR("script: " << line);
			else if (level == 1) RDA_LOG_WARNING("script: " << line);
			else                 RDA_LOG_INFO("script: " << line);
			return JS_UNDEFINED;
		}
	}

	namespace ScriptInternal {
		void installNatives(Runtime& r) {
			r.natives = JS_NewObject(r.ctx);
			const struct { const char* name; JSCFunction* fn; int length; } natives[] = {
				{ "get", &nGet, 1 }, { "has", &nHas, 1 }, { "set", &nSet, 2 },
				{ "command", &nCommand, 2 }, { "table", &nTable, 1 }, { "rows", &nRows, 1 },
				{ "cell", &nCell, 3 }, { "column", &nColumn, 2 }, { "log", &nLog, 2 },
				{ "timer", &nTimer, 5 }, { "clearTimer", &nClearTimer, 1 },
			};
			for (const auto& one : natives) {
				JS_SetPropertyStr(r.ctx, r.natives, one.name, JS_NewCFunction(r.ctx, one.fn, one.name, one.length));
			}
		}
	}
}
