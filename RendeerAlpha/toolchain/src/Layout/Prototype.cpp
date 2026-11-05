#include <Layout/Prototype.h>
#include "JsContext.h"

#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Core/Commands.h>
#include <vendor/quickjs/quickjs.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace RDA::Layout {

	namespace {

		ColumnType columnTypeOf(StateType type) {
			switch (type) {
			case StateType::Bool: return ColumnType::Bool;
			case StateType::Text: return ColumnType::Text;
			default:              return ColumnType::Number;
			}
		}

		// The declared initial value, as written in the .ts, turned into the signal.
		//
		// `value` is the source text rather than a parsed value, because that is what the
		// schema keeps -- it is enough for every generator, and it is enough here.
		void defineFrom(const StateField& field) {
			switch (field.type) {
			case StateType::Bool:
				signals().define(field.name, field.value == "true");
				break;
			case StateType::Text: {
				// Quoted in the source; the quotes are not part of the value.
				std::string text = field.value;
				if (text.size() >= 2 &&
				    ((text.front() == '"' && text.back() == '"') ||
				     (text.front() == '\'' && text.back() == '\''))) {
					text = text.substr(1, text.size() - 2);
				}
				signals().define(field.name, std::string_view(text));
				break;
			}
			default:
				signals().define(field.name, std::atof(field.value.c_str()));
				break;
			}
		}

		std::string readWholeFile(const std::string& path, bool& ok) {
			std::ifstream file(path, std::ios::binary);
			if (!file) { ok = false; return {}; }
			std::ostringstream all;
			all << file.rdbuf();
			ok = true;

			// A UTF-8 byte order mark, dropped rather than handed to the JSON reader.
			//
			// Every editor on Windows writes one and JSON has no idea what it is -- the
			// report is `Unexpected token '﻿'`, which names a character nobody can
			// see and no file appears to contain. Worth three lines to never have that
			// conversation.
			std::string text = all.str();
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
			    static_cast<unsigned char>(text[1]) == 0xBB &&
			    static_cast<unsigned char>(text[2]) == 0xBF) {
				text.erase(0, 3);
			}
			return text;
		}

		// The message of a JSON file that would not parse, and nothing under it: the stack of
		// a reader is the engine's own, and the person who has to fix the file has no use for it.
		// Said as "unknown error" when it cannot be said at all, which is this file's wording.
		std::string exceptionText(JSContext* ctx) {
			ThrownFormat format;
			format.stack = false;
			format.unprintable = "unknown error";
			return thrownText(ctx, format);
		}

		// Writes one JSON value into whatever `name` turned out to be.
		bool applySignal(JSContext* ctx, uint32_t signal, JSValueConst value,
		                 const std::string& name, std::string& error) {
			if (signals().computed(signal)) {
				error = "`" + name + "` is computed by state.ts -- set what it is computed from";
				return false;
			}
			switch (signals().type(signal)) {
			case SignalType::Bool:
				signals().set(signal, JS_ToBool(ctx, value) != 0);
				return true;
			case SignalType::Text: {
				const char* text = JS_ToCString(ctx, value);
				signals().set(signal, std::string_view(text ? text : ""));
				if (text) JS_FreeCString(ctx, text);
				return true;
			}
			default: {
				double number = 0.0;
				if (JS_ToFloat64(ctx, &number, value) < 0) {
					error = "`" + name + "` is a number and this is not one";
					return false;
				}
				signals().set(signal, number);
				return true;
			}
			}
		}

		bool applyTable(JSContext* ctx, Table& table, JSValueConst rowsValue,
		                const std::string& name, size_t& rowsWritten, std::string& error) {
			if (!JS_IsArray(rowsValue)) {
				error = "`" + name + "` is a table, so it wants an array of rows";
				return false;
			}
			uint32_t count = 0;
			{
				JSValue length = JS_GetPropertyStr(ctx, rowsValue, "length");
				JS_ToUint32(ctx, &count, length);
				JS_FreeValue(ctx, length);
			}
			table.resize(count);

			for (uint32_t row = 0; row < count; ++row) {
				JSValue rowValue = JS_GetPropertyUint32(ctx, rowsValue, row);
				if (!JS_IsObject(rowValue)) {
					JS_FreeValue(ctx, rowValue);
					error = "row " + std::to_string(row) + " of `" + name + "` is not an object";
					return false;
				}
				for (size_t c = 0; c < table.columns(); ++c) {
					// Every column by name, so a row that leaves one out keeps whatever
					// resize() gave it rather than shifting the others along.
					const uint32_t column = static_cast<uint32_t>(c);
					const std::string columnName = std::string(table.columnName(column));
					JSValue cell = JS_GetPropertyStr(ctx, rowValue, columnName.c_str());
					if (JS_IsUndefined(cell)) { JS_FreeValue(ctx, cell); continue; }

					switch (table.columnType(column)) {
					case ColumnType::Bool:
						table.setBool(row, column, JS_ToBool(ctx, cell) != 0);
						break;
					case ColumnType::Text: {
						const char* text = JS_ToCString(ctx, cell);
						table.setText(row, column, text ? text : "");
						if (text) JS_FreeCString(ctx, text);
						break;
					}
					default: {
						double number = 0.0;
						JS_ToFloat64(ctx, &number, cell);
						table.setNumber(row, column, number);
						break;
					}
					}
					JS_FreeValue(ctx, cell);
				}
				JS_FreeValue(ctx, rowValue);
				++rowsWritten;
			}
			return true;
		}
	}

	PrototypeResult definePrototypeState(const StateSchema& schema,
	                                     void (*onCommand)(const char*, const char*), bool onlyNew) {
		PrototypeResult result;
		if (!schema.ok) {
			result.error = schema.error.empty() ? "the declaration did not load" : schema.error;
			return result;
		}

		for (const StateField& field : schema.fields) {
			if (onlyNew && signals().find(field.name) != kNoSignal) continue;
			defineFrom(field);
			++result.signals;
		}

		// Declaring routes is what creates the `route` signal and the two commands that
		// go with it, and the Router expects to find them. The generated C++ does this
		// too; a prototype needs it for the same reason -- without `route` there is no
		// screen to be on.
		if (!schema.routes.empty() && !(onlyNew && signals().find("route") != kNoSignal)) {
			signals().define("route", std::string_view(schema.routes.front().name));
			++result.signals;
			commands().define("back");
			commands().define("forward");
			result.commands += 2;
		}

		for (const StateTable& declared : schema.tables) {
			if (onlyNew && tables().find(declared.name) != kNoTable) continue;
			const uint32_t id = tables().define(declared.name);
			Table& table = tables().at(id);
			for (const StateField& column : declared.columns) {
				table.defineColumn(column.name, columnTypeOf(column.type));
			}
			++result.tables;
		}

		for (const StateCommand& declared : schema.commands) {
			if (onlyNew && commands().find(declared.name) != kNoCommand) continue;
			const uint32_t id = commands().define(declared.name);
			if (onCommand) {
				// Copied into the closure: the schema is the caller's and may not outlive
				// the run.
				const std::string name = declared.name;
				commands().bind(id, CommandWork([onCommand, name](const CommandArgs& args) {
					onCommand(name.c_str(), args.json().c_str());
				}));
			}
			++result.commands;
		}

		result.ok = true;
		return result;
	}

	PrototypeResult loadPrototypeData(const std::string& jsonPath) {
		PrototypeResult result;

		bool read = false;
		const std::string text = readWholeFile(jsonPath, read);
		if (!read) {
			result.error = "cannot open " + jsonPath;
			return result;
		}

		JsContext engine;
		if (!engine) { result.error = engine.failure(); return result; }
		JSContext* ctx = engine.get();

		JSValue root = JS_ParseJSON(ctx, text.c_str(), text.size(), jsonPath.c_str());
		if (JS_IsException(root)) {
			result.error = exceptionText(ctx);
		} else if (!JS_IsObject(root) || JS_IsArray(root)) {
			result.error = "the top level is an object of names: "
			               "{ \"count\": 12, \"products\": [ ... ] }";
		} else {
			JSPropertyEnum* keys = nullptr;
			uint32_t keyCount = 0;
			if (JS_GetOwnPropertyNames(ctx, &keys, &keyCount, root,
			                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
				result.ok = true;
				for (uint32_t i = 0; i < keyCount && result.ok; ++i) {
					const char* raw = JS_AtomToCString(ctx, keys[i].atom);
					const std::string name = raw ? raw : "";
					if (raw) JS_FreeCString(ctx, raw);

					JSValue value = JS_GetProperty(ctx, root, keys[i].atom);

					const uint32_t signal = signals().find(name);
					const uint32_t table = tables().find(name);
					if (signal != kNoSignal) {
						if (applySignal(ctx, signal, value, name, result.error)) ++result.signals;
						else result.ok = false;
					} else if (table != kNoTable) {
						if (applyTable(ctx, tables().at(table), value, name,
						               result.rows, result.error)) {
							++result.tables;
						} else {
							result.ok = false;
						}
					} else {
						// Named nothing. Refused rather than skipped: a misspelling here
						// is a screen that stays empty for a reason nobody can see.
						result.error = "nothing is called `" + name +
						               "` -- res/state.ts declares the names this may set";
						result.ok = false;
					}
					JS_FreeValue(ctx, value);
				}
				for (uint32_t i = 0; i < keyCount; ++i) JS_FreeAtom(ctx, keys[i].atom);
				js_free(ctx, keys);
			} else {
				result.error = "cannot read the names in " + jsonPath;
			}
		}

		JS_FreeValue(ctx, root);
		return result;
	}

	bool setPrototypeValue(const std::string& assignment, std::string& error) {
		const size_t equals = assignment.find('=');
		if (equals == std::string::npos || equals == 0) {
			error = "`" + assignment + "` is not name=value";
			return false;
		}
		const std::string name = assignment.substr(0, equals);
		const std::string value = assignment.substr(equals + 1);

		const uint32_t signal = signals().find(name);
		if (signal == kNoSignal) {
			error = "nothing is called `" + name + "`";
			return false;
		}
		if (signals().computed(signal)) {
			error = "`" + name + "` is computed by state.ts -- set what it is computed from";
			return false;
		}
		switch (signals().type(signal)) {
		case SignalType::Bool:
			// Anything that is not plainly false is true, so `--set details=1` and
			// `--set details=yes` both do what they look like.
			signals().set(signal, !(value == "false" || value == "0" || value.empty()));
			break;
		case SignalType::Text:
			signals().set(signal, std::string_view(value));
			break;
		default: {
			char* stop = nullptr;
			const double number = std::strtod(value.c_str(), &stop);
			if (stop == value.c_str()) {
				error = "`" + name + "` is a number and `" + value + "` is not one";
				return false;
			}
			signals().set(signal, number);
			break;
		}
		}
		return true;
	}

	bool fillPrototypeRows(const std::string& assignment, std::string& error) {
		const size_t equals = assignment.find('=');
		if (equals == std::string::npos || equals == 0) {
			error = "`" + assignment + "` is not table=count";
			return false;
		}
		const std::string name = assignment.substr(0, equals);
		const long long count = std::atoll(assignment.substr(equals + 1).c_str());
		if (count < 0) {
			error = "`" + name + "` cannot have fewer than no rows";
			return false;
		}

		const uint32_t id = tables().find(name);
		if (id == kNoTable) {
			error = "no table is called `" + name + "`";
			return false;
		}
		Table& table = tables().at(id);
		table.resize(static_cast<size_t>(count));

		for (size_t row = 0; row < static_cast<size_t>(count); ++row) {
			for (size_t c = 0; c < table.columns(); ++c) {
				const uint32_t column = static_cast<uint32_t>(c);
				switch (table.columnType(column)) {
				case ColumnType::Bool:
					// Alternating rather than all true: a column that is the same in
					// every row tells you nothing about how the screen handles both.
					table.setBool(row, column, (row % 3) != 0);
					break;
				case ColumnType::Text:
					table.setText(row, column,
					              std::string(table.columnName(column)) + " " + std::to_string(row + 1));
					break;
				default:
					table.setNumber(row, column, static_cast<double>(row + 1));
					break;
				}
			}
		}
		return true;
	}
}
