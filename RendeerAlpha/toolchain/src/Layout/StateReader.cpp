#include <Layout/StateSchema.h>
#include <Layout/ModuleTransform.h>
#include "JsContext.h"
#include "ScriptPreludes.h"
#include "SourceMap.h"
#include <vendor/tinyxml2/tinyxml2.h>
#include <vendor/quickjs/quickjs.h>

#include <cmath>
#include <sstream>

// Reading a state declaration: state.ts, and the XML it replaced.
//
// The TypeScript is compiled by esbuild and run once in QuickJS, and what it exported is
// walked into a StateSchema -- the model in <Layout/StateSchema.h>, and all the generators
// in Codegen/ ever see. Nothing here knows what a Python module or a C++ header looks like,
// and nothing there knows QuickJS was involved, which is what lets a generator be tested
// with a schema built by hand.
namespace RDA::Layout {

	namespace {
		// ---- the TypeScript form ---------------------------------------------------
		//
		// State is a declaration, and it used to be XML because that is what this project
		// already parsed. Everything else it declares moved to TypeScript, and keeping one
		// format out was costing a second thing to learn for no benefit -- the toolchain
		// that reads a theme reads this too.
		//
		//   export const state = { count: 0, details: false, notes: "hello" }
		//   export const commands = { save: "write the notes out" }
		//   export const tables = { products: { title: "", price: 0 } }
		//
		// A type is whatever the initial value is, which removes the ceremony of saying it
		// twice. Where a field wants documenting, the long form takes its place:
		//
		//   count: { value: 0, doc: "how many times a button has been pressed" }

		// The initial value as the generated C++ will spell it, and the type it implies.
		bool readValue(JSContext* ctx, JSValueConst value, StateField& field, std::string& error) {
			if (JS_IsBool(value)) {
				field.type = StateType::Bool;
				field.value = JS_ToBool(ctx, value) ? "true" : "false";
				return true;
			}
			if (JS_IsNumber(value)) {
				double number = 0.0;
				JS_ToFloat64(ctx, &number, value);
				// Every language spells NaN differently and C++ not at all; a computed value
				// that started as one -- an average over an empty table -- starts at zero,
				// and the engine works out the real one as soon as the code is loaded.
				if (!std::isfinite(number)) number = 0.0;
				std::ostringstream out;
				out << number;
				field.type = StateType::Number;
				field.value = out.str();
				return true;
			}
			if (JS_IsString(value)) {
				const char* text = JS_ToCString(ctx, value);
				field.type = StateType::Text;
				field.value = text ? text : "";
				if (text) JS_FreeCString(ctx, text);
				return true;
			}
			error = "'" + field.name + "' is " +
			        (JS_IsFunction(ctx, value) ? "a function -- a computed value is written { compute: () => ... }, and"
			         : JS_IsObject(value) ? "an object, and" : "neither a number, a boolean nor a string, and") +
			        " state holds a number, a boolean or a string";
			return false;
		}

		// Either the value itself, or { value, doc }, or -- where `computable` -- a computed
		// one, { compute, doc }, whose type is not known until it has been run. `pending` is
		// then set, and the caller runs it once the whole declaration has been read. Where
		// `computable`, either long form may also carry a `watch`.
		bool readField(JSContext* ctx, JSValueConst entry, StateField& field, std::string& error,
		               bool computable = false, bool* pending = nullptr) {
			if (JS_IsObject(entry) && !JS_IsFunction(ctx, entry)) {
				JSValue compute = JS_GetPropertyStr(ctx, entry, "compute");
				const bool computes = JS_IsFunction(ctx, compute);
				JS_FreeValue(ctx, compute);
				if (computes && !computable) {
					error = "'" + field.name + "' has a `compute`, and only a field of state can be computed";
					return false;
				}
				field.computed = computes;
				JSValue watch = JS_GetPropertyStr(ctx, entry, "watch");
				const bool watches = JS_IsFunction(ctx, watch);
				const bool watchGiven = !JS_IsUndefined(watch);
				JS_FreeValue(ctx, watch);
				if (watchGiven && !watches) {
					error = "'" + field.name + "' has a `watch` that is not a function";
					return false;
				}
				if (watches && !computable) {
					error = "'" + field.name + "' has a `watch`, and only a field of state can be watched";
					return false;
				}
				field.watched = watches;
				JSValue inner = JS_GetPropertyStr(ctx, entry, "value");
				if (JS_IsUndefined(inner) && computes) {
					// Typed by running it, once the rest is read.
					if (pending) *pending = true;
				} else if (JS_IsUndefined(inner)) {
					JS_FreeValue(ctx, inner);
					error = "'" + field.name + "' is an object without a `value`; write the "
					        "initial value, { value, doc }, or { compute, doc }";
					return false;
				} else {
					const bool ok = readValue(ctx, inner, field, error);
					if (!ok) { JS_FreeValue(ctx, inner); return false; }
				}
				JS_FreeValue(ctx, inner);

				JSValue doc = JS_GetPropertyStr(ctx, entry, "doc");
				if (JS_IsString(doc)) {
					const char* text = JS_ToCString(ctx, doc);
					if (text) { field.doc = text; JS_FreeCString(ctx, text); }
				}
				JS_FreeValue(ctx, doc);
				return true;
			}
			return readValue(ctx, entry, field, error);
		}

		// Walks one exported object, calling `each` with the key and its value.
		template <typename Fn>
		bool forEachEntry(JSContext* ctx, JSValueConst object, Fn each) {
			JSPropertyEnum* names = nullptr;
			uint32_t count = 0;
			if (!JS_IsObject(object) ||
			    JS_GetOwnPropertyNames(ctx, &names, &count, object,
			                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
				return true; // absent is not an error: a declaration may have no tables
			}
			bool ok = true;
			for (uint32_t i = 0; i < count && ok; ++i) {
				const char* rawKey = JS_AtomToCString(ctx, names[i].atom);
				if (!rawKey) continue;
				const std::string key = rawKey;
				JS_FreeCString(ctx, rawKey);

				JSValue value = JS_GetProperty(ctx, object, names[i].atom);
				ok = each(key, value);
				JS_FreeValue(ctx, value);
			}
			JS_FreePropertyEnum(ctx, names, count);
			return ok;
		}

		StateSchema loadFromTypeScript(const std::string& path) {
			StateSchema schema;
			schema.source = path;

			std::string js, sourceMap;
			if (!transformModule(path, findEsbuild(), /*withJsx*/ false, js, sourceMap, schema.error)) {
				return schema;
			}

			JsContext engine(JsContext::Options{ kCompileJsStack });
			if (!engine) { schema.error = engine.failure(); return schema; }
			JSContext* ctx = engine.get();

			// The helpers as a script of their own, so a line in an error is a line of the file.
			const std::string helpers = std::string(colourPrelude()) + formatPrelude() + buildTimerPrelude() +
				statePrelude();
			JSValue result = JS_Eval(ctx, helpers.c_str(), helpers.size(), "<rda helpers>",
			                         JS_EVAL_TYPE_GLOBAL);
			if (!JS_IsException(result)) {
				JS_FreeValue(ctx, result);
				result = JS_Eval(ctx, js.c_str(), js.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
			}
			JSValue global = JS_GetGlobalObject(ctx);

			if (JS_IsException(result)) {
				// With the stack, each frame at its line of state.ts. It used to be the
				// message alone, so a declaration that threw said what had gone wrong and not
				// where. The IIFE is evaluated as esbuild wrote it: nothing is in front.
				schema.error = thrownText(ctx, SourceMap(sourceMap, path));
			} else {
				JSValue exports = JS_GetPropertyStr(ctx, global, "__module");
				if (!JS_IsObject(exports)) {
					schema.error = "this file exports nothing. State is a declaration: "
					               "`export const state = { count: 0 }`";
				} else {
					JSValue stateObject = JS_GetPropertyStr(ctx, exports, "state");
					JSValue commandObject = JS_GetPropertyStr(ctx, exports, "commands");
					JSValue tableObject = JS_GetPropertyStr(ctx, exports, "tables");

					std::vector<size_t> pending;   // computed fields still to be run for a type
					bool ok = forEachEntry(ctx, stateObject, [&](const std::string& name, JSValueConst value) {
						StateField field;
						field.name = name;
						bool unknown = false;
						if (!readField(ctx, value, field, schema.error, /*computable*/ true, &unknown)) return false;
						if (unknown) pending.push_back(schema.fields.size());
						schema.fields.push_back(std::move(field));
						return true;
					});

					if (ok) {
						ok = forEachEntry(ctx, commandObject, [&](const std::string& name, JSValueConst value) {
							StateCommand command;
							command.name = name;
							if (JS_IsString(value)) {
								const char* text = JS_ToCString(ctx, value);
								if (text) { command.doc = text; JS_FreeCString(ctx, text); }
							} else if (JS_IsObject(value) && !JS_IsFunction(ctx, value)) {
								// The long form: { doc, params } for one the backend answers,
								// { doc, run } for one state.ts answers.
								JSValue doc = JS_GetPropertyStr(ctx, value, "doc");
								if (JS_IsString(doc)) {
									const char* text = JS_ToCString(ctx, doc);
									if (text) { command.doc = text; JS_FreeCString(ctx, text); }
								}
								JS_FreeValue(ctx, doc);
								JSValue run = JS_GetPropertyStr(ctx, value, "run");
								command.action = JS_IsFunction(ctx, run);
								const bool runGiven = !JS_IsUndefined(run);
								JS_FreeValue(ctx, run);
								if (runGiven && !command.action) {
									schema.error = "command '" + name + "' has a `run` that is not a function";
									return false;
								}
								JSValue params = JS_GetPropertyStr(ctx, value, "params");
								bool fine = true;
								if (!JS_IsUndefined(params)) {
									if (command.action) {
										schema.error = "command '" + name + "' has both `run` and `params`. An "
										               "action's parameters are its function's -- drop `params`";
										fine = false;
									} else if (!JS_IsObject(params) || JS_IsArray(params)) {
										schema.error = "command '" + name + "' has `params` that are not an object: "
										               "write them by example, in order -- params: { id: 0, name: \"\" }";
										fine = false;
									} else {
										fine = forEachEntry(ctx, params, [&](const std::string& param, JSValueConst example) {
											StateField one;
											one.name = param;
											if (!readField(ctx, example, one, schema.error)) {
												schema.error = "command '" + name + "': " + schema.error;
												return false;
											}
											command.params.push_back(std::move(one));
											return true;
										});
									}
								}
								JS_FreeValue(ctx, params);
								if (!fine) return false;
							} else if (!JS_IsUndefined(value) && !JS_IsNull(value)) {
								schema.error = "command '" + name + "' should be a description, an empty "
								               "string, or { doc, params } or { doc, run }";
								return false;
							}
							schema.commands.push_back(std::move(command));
							return true;
						});
					}

					if (ok) {
						ok = forEachEntry(ctx, tableObject, [&](const std::string& name, JSValueConst columns) {
							StateTable table;
							table.name = name;
							if (!JS_IsObject(columns)) {
								schema.error = "table '" + name + "' should be an object of "
								               "columns: { title: \"\", price: 0 }";
								return false;
							}
							const bool inner = forEachEntry(ctx, columns,
								[&](const std::string& columnName, JSValueConst value) {
									StateField column;
									column.name = columnName;
									if (!readField(ctx, value, column, schema.error)) return false;
									table.columns.push_back(std::move(column));
									return true;
								});
							if (!inner) return false;
							if (table.columns.empty()) {
								schema.error = "table '" + name + "' has no columns";
								return false;
							}
							schema.tables.push_back(std::move(table));
							return true;
						});
					}
					if (ok) {
						JSValue routeObject = JS_GetPropertyStr(ctx, exports, "routes");
						ok = forEachEntry(ctx, routeObject, [&](const std::string& name, JSValueConst value) {
							StateRoute route;
							route.name = name;
							// Either the layout's name, or the long form beside it.
							if (JS_IsString(value)) {
								const char* text = JS_ToCString(ctx, value);
								if (text) { route.layout = text; JS_FreeCString(ctx, text); }
							} else if (JS_IsObject(value)) {
								JSValue layout = JS_GetPropertyStr(ctx, value, "layout");
								const char* text = JS_ToCString(ctx, layout);
								if (text && JS_IsString(layout)) { route.layout = text; }
								if (text) JS_FreeCString(ctx, text);
								JS_FreeValue(ctx, layout);

								JSValue doc = JS_GetPropertyStr(ctx, value, "doc");
								if (JS_IsString(doc)) {
									const char* d = JS_ToCString(ctx, doc);
									if (d) { route.doc = d; JS_FreeCString(ctx, d); }
								}
								JS_FreeValue(ctx, doc);

								JSValue params = JS_GetPropertyStr(ctx, value, "params");
								if (JS_IsArray(params)) {
									uint32_t length = 0;
									JSValue lengthValue = JS_GetPropertyStr(ctx, params, "length");
									JS_ToUint32(ctx, &length, lengthValue);
									JS_FreeValue(ctx, lengthValue);
									for (uint32_t i = 0; i < length; ++i) {
										JSValue item = JS_GetPropertyUint32(ctx, params, i);
										const char* p = JS_ToCString(ctx, item);
										if (p) { route.params.push_back(p); JS_FreeCString(ctx, p); }
										JS_FreeValue(ctx, item);
									}
								}
								JS_FreeValue(ctx, params);
							} else {
								schema.error = "route '" + name + "' should name a layout: "
								               "`" + name + ": \"settings\"`, or the long form "
								               "`{ layout: \"settings\", params: [\"id\"] }`";
								return false;
							}
							if (route.layout.empty()) {
								schema.error = "route '" + name + "' names no layout";
								return false;
							}
							schema.routes.push_back(std::move(route));
							return true;
						});
						JS_FreeValue(ctx, routeObject);
					}

					// A computed field's type is what it returns, so each is run once, now,
					// against the declared starting values with every table empty. Last,
					// because every declaration -- the tables too -- has to have been read
					// first: the evaluator turns them into values in place.
					if (ok && !pending.empty()) {
						JSValue evaluate = JS_GetPropertyStr(ctx, global, "__rdaEvaluateComputed");
						JSValueConst args[3] = { stateObject, tableObject, commandObject };
						JSValue results = JS_Call(ctx, evaluate, JS_UNDEFINED, 3, args);
						JS_FreeValue(ctx, evaluate);
						if (JS_IsException(results)) {
							schema.error = "the computed values could not be worked out: " +
							               thrownText(ctx, SourceMap(sourceMap, path));
							ok = false;
						}
						for (size_t i = 0; ok && i < pending.size(); ++i) {
							StateField& field = schema.fields[pending[i]];
							JSValue one = JS_GetPropertyStr(ctx, results, field.name.c_str());
							JSValue value = JS_GetPropertyStr(ctx, one, "value");
							JSValue why = JS_GetPropertyStr(ctx, one, "error");
							if (JS_IsString(why)) {
								const char* text = JS_ToCString(ctx, why);
								JSValue plainValue = JS_GetPropertyStr(ctx, one, "plain");
								const bool plain = JS_ToBool(ctx, plainValue) == 1;
								JS_FreeValue(ctx, plainValue);
								// A refusal says what to do; anything else may just be data that
								// is not there yet, which a starting value answers.
								schema.error = "state." + field.name + " is computed, and " +
								               (plain ? std::string(text ? text : "")
								                      : "running it once while state.ts was built failed: " +
								                        std::string(text ? text : "") +
								                        ". If it needs data that is not there yet, give it a "
								                        "starting value as well -- { compute, value: 0 } -- "
								                        "which is also its type");
								if (text) JS_FreeCString(ctx, text);
								// And where: the frames of the stack that are in state.ts and
								// what it imports, which is the compute that failed and any
								// computed value it was reading. The rest is the evaluator.
								JSValue stackValue = JS_GetPropertyStr(ctx, one, "stack");
								if (JS_IsString(stackValue)) {
									const char* stack = JS_ToCString(ctx, stackValue);
									const std::string frames = SourceMap(sourceMap, path).ownFrames(stack ? stack : "");
									if (stack) JS_FreeCString(ctx, stack);
									if (!frames.empty()) schema.error += "\n" + frames;
								}
								JS_FreeValue(ctx, stackValue);
								ok = false;
							} else if (!readValue(ctx, value, field, schema.error)) {
								schema.error = "state." + field.name + " is computed and returned a value no signal "
								               "can hold: " + schema.error;
								ok = false;
							}
							JS_FreeValue(ctx, why);
							JS_FreeValue(ctx, value);
							JS_FreeValue(ctx, one);
						}
						JS_FreeValue(ctx, results);
					}

					schema.ok = ok && schema.error.empty();

					JS_FreeValue(ctx, tableObject);
					JS_FreeValue(ctx, commandObject);
					JS_FreeValue(ctx, stateObject);
				}
				JS_FreeValue(ctx, exports);
			}

			JS_FreeValue(ctx, global);
			JS_FreeValue(ctx, result);
			return schema;
		}

		// What declaring routes adds, once they have all been read.
		//
		// A route is a screen, and which screen is showing is state -- so it is held in an
		// ordinary signal, written the ordinary way. Nothing here is a second mechanism:
		// `route` is a signal like any other and `back` is a command like any other, which
		// is why navigation needed no new grammar and is reactive without being told to be.
		void deriveRoutes(StateSchema& schema) {
			if (schema.routes.empty()) return;

			for (const StateField& field : schema.fields) {
				if (field.name == "route") {
					schema.error = "'route' is declared by `routes` and cannot also be a "
					               "state field -- it holds the name of the screen showing";
					return;
				}
			}
			for (const StateCommand& command : schema.commands) {
				if (command.name == "back" || command.name == "forward") {
					schema.error = "'" + command.name + "' is a command `routes` provides; "
					               "declaring it again would leave two meanings for one name";
					return;
				}
			}
			// A param has to be a signal that exists, because restoring one on the way back
			// means writing it, and writing a name nothing declared writes nothing.
			for (const StateRoute& route : schema.routes) {
				for (const std::string& param : route.params) {
					bool found = false;
					for (const StateField& field : schema.fields) {
						if (field.name == param) { found = true; break; }
					}
					if (!found) {
						schema.error = "route '" + route.name + "' takes a param '" + param
						             + "', which is not a declared signal. A param is state, "
						               "so declare it in `state` first.";
						return;
					}
				}
			}

			StateField route;
			route.name = "route";
			route.type = StateType::Text;
			route.value = schema.routes.front().name; // the first declared route is where it opens
			route.doc = "which screen is showing; assign to it to navigate";
			for (const StateRoute& one : schema.routes) route.choices.push_back(one.name);
			schema.fields.push_back(std::move(route));

			StateCommand back;
			back.name = "back";
			back.doc = "the screen visited before this one";
			schema.commands.push_back(std::move(back));

			StateCommand forward;
			forward.name = "forward";
			forward.doc = "the screen gone back from";
			schema.commands.push_back(std::move(forward));
		}

		// ---- the XML form ---------------------------------------------------------
		//
		// Kept for a declaration written before the move to TypeScript. Nothing new should
		// be written in it.

		bool typeFromElement(const char* name, StateType& out) {
			const std::string element = name ? name : "";
			if (element == "number") { out = StateType::Number; return true; }
			if (element == "bool")   { out = StateType::Bool;   return true; }
			if (element == "text")   { out = StateType::Text;   return true; }
			return false;
		}

		StateSchema loadFromXml(const std::string& path) {
			StateSchema schema;
			schema.source = path;

			tinyxml2::XMLDocument document;
			if (document.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
				schema.error = "cannot read " + path;
				return schema;
			}

			const tinyxml2::XMLElement* root = document.FirstChildElement("state");
			if (!root) {
				schema.error = path + " has no <state> element";
				return schema;
			}

			for (const tinyxml2::XMLElement* entry = root->FirstChildElement();
			     entry; entry = entry->NextSiblingElement()) {
				const std::string element = entry->Name() ? entry->Name() : "";
				if (element == "table") {
					const char* tableName = entry->Attribute("name");
					if (!tableName || !*tableName) {
						schema.error = "a <table> has no name";
						return schema;
					}
					StateTable table;
					table.name = tableName;
					if (const char* doc = entry->Attribute("doc")) table.doc = doc;

					for (const tinyxml2::XMLElement* col = entry->FirstChildElement();
					     col; col = col->NextSiblingElement()) {
						StateField column;
						if (!typeFromElement(col->Name(), column.type)) {
							schema.error = std::string("unknown column type <") +
							               (col->Name() ? col->Name() : "?") + "> in table '" +
							               table.name + "'; expected <number>, <bool> or <text>";
							return schema;
						}
						const char* columnName = col->Attribute("name");
						if (!columnName || !*columnName) {
							schema.error = "a column of table '" + table.name + "' has no name";
							return schema;
						}
						column.name = columnName;
						if (const char* doc = col->Attribute("doc")) column.doc = doc;
						table.columns.push_back(std::move(column));
					}
					if (table.columns.empty()) {
						schema.error = "table '" + table.name + "' has no columns";
						return schema;
					}
					schema.tables.push_back(std::move(table));
					continue;
				}
				if (element == "command") {
					const char* commandName = entry->Attribute("name");
					if (!commandName || !*commandName) {
						schema.error = "a <command> has no name";
						return schema;
					}
					for (const StateCommand& existing : schema.commands) {
						if (existing.name == commandName) {
							schema.error = std::string("command '") + commandName + "' is declared twice";
							return schema;
						}
					}
					StateCommand command;
					command.name = commandName;
					if (const char* doc = entry->Attribute("doc")) command.doc = doc;
					schema.commands.push_back(std::move(command));
					continue;
				}

				StateField field;
				if (!typeFromElement(entry->Name(), field.type)) {
					schema.error = std::string("unknown state type <") + element +
					               ">; expected <number>, <bool>, <text> or <command>";
					return schema;
				}
				const char* name = entry->Attribute("name");
				if (!name || !*name) {
					schema.error = "a state entry has no name";
					return schema;
				}
				field.name = name;
				if (const char* value = entry->Attribute("value")) field.value = value;
				if (const char* doc = entry->Attribute("doc")) field.doc = doc;

				for (const StateField& existing : schema.fields) {
					if (existing.name == field.name) {
						schema.error = "state '" + field.name + "' is declared twice";
						return schema;
					}
				}
				schema.fields.push_back(std::move(field));
			}

			schema.ok = true;
			return schema;
		}
	}

	StateSchema loadStateSchema(const std::string& path) {
		// TypeScript is the format. The XML reader above is kept for a declaration written
		// before the move, and nothing new should be written in it.
		const size_t dot = path.rfind('.');
		const std::string extension = (dot == std::string::npos) ? "" : path.substr(dot);
		if (extension == ".ts" || extension == ".tsx") {
			// Where a recursion that does not end is an error with a line, as it is for a layout,
			// and not a crash on the main thread's one megabyte. See JsContext.h.
			StateSchema schema;
			const bool ran = onLargeStack([&] { schema = loadFromTypeScript(path); });
			if (!ran) schema = loadFromTypeScript(path);
			if (schema.ok) {
				deriveRoutes(schema);
				if (!schema.error.empty()) schema.ok = false;
			}
			return schema;
		}

		return loadFromXml(path);
	}
}
