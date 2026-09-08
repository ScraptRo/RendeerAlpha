#include <Layout/StateSchema.h>
#include "ModuleTransform.h"
#include <vendor/tinyxml2/tinyxml2.h>
#include <vendor/quickjs/quickjs.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace RDA::Layout {

	namespace {
		bool typeFromElement(const char* name, StateType& out) {
			const std::string element = name ? name : "";
			if (element == "number") { out = StateType::Number; return true; }
			if (element == "bool")   { out = StateType::Bool;   return true; }
			if (element == "text")   { out = StateType::Text;   return true; }
			return false;
		}

		// "count" -> "setCount". Only the first letter moves; a name already capitalised
		// keeps its shape.
		// A command is registered rather than assigned, so it reads as one: onSave(...)
		// says what the function is for, where setSave(...) would suggest it holds a value.
		// "products" -> "ProductsRow"
		std::string rowStructName(const std::string& table) {
			std::string out = table;
			if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
			return out + "Row";
		}

		std::string handlerName(const std::string& name) {
			std::string out = "on" + name;
			if (out.size() > 2 && out[2] >= 'a' && out[2] <= 'z') out[2] -= 32;
			return out;
		}

		std::string setterName(const std::string& field) {
			std::string out = "set" + field;
			if (out.size() > 3 && out[3] >= 'a' && out[3] <= 'z') out[3] -= 32;
			return out;
		}

		// The value as C++ writes it, and as TypeScript reads it.
		std::string cppLiteral(const StateField& field) {
			switch (field.type) {
			case StateType::Number: {
				// Spelled as a double on purpose. `define("count", 0)` would be ambiguous
				// between the number and boolean overloads, and the error it produces
				// points at generated code nobody wrote.
				if (field.value.empty()) return "0.0";
				const bool alreadyReal = field.value.find('.') != std::string::npos ||
				                         field.value.find('e') != std::string::npos ||
				                         field.value.find('E') != std::string::npos;
				return alreadyReal ? field.value : field.value + ".0";
			}
			case StateType::Bool:
				return field.value == "true" || field.value == "1" ? "true" : "false";
			case StateType::Text: {
				// Escaped, because the value is now whatever a TypeScript string held and
				// a quote in it would otherwise end the literal in generated code.
				std::string escaped;
				for (const char c : field.value) {
					if (c == '\\' || c == '\"') escaped += '\\';
					if (c == '\n') { escaped += "\\n"; continue; }
					escaped += c;
				}
				return "std::string_view(\"" + escaped + "\")";
			}
			}
			return "0.0";
		}

		const char* cppType(StateType type) {
			switch (type) {
			case StateType::Number: return "double";
			case StateType::Bool:   return "bool";
			case StateType::Text:   return "std::string_view";
			}
			return "double";
		}

		const char* readerCall(StateType type) {
			switch (type) {
			case StateType::Number: return "number";
			case StateType::Bool:   return "boolean";
			case StateType::Text:   return "text";
			}
			return "number";
		}

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

		std::string exceptionText(JSContext* ctx) {
			JSValue exception = JS_GetException(ctx);
			const char* text = JS_ToCString(ctx, exception);
			std::string out = text ? text : "unknown error";
			if (text) JS_FreeCString(ctx, text);
			JS_FreeValue(ctx, exception);
			return out;
		}

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
			error = "'" + field.name + "' is a " +
			        (JS_IsObject(value) ? "object" : "value") +
			        "; state holds a number, a boolean or a string";
			return false;
		}

		// Either the value itself, or { value, doc }.
		bool readField(JSContext* ctx, JSValueConst entry, StateField& field, std::string& error) {
			if (JS_IsObject(entry) && !JS_IsFunction(ctx, entry)) {
				JSValue inner = JS_GetPropertyStr(ctx, entry, "value");
				if (JS_IsUndefined(inner)) {
					JS_FreeValue(ctx, inner);
					error = "'" + field.name + "' is an object without a `value`; write the "
					        "initial value, or { value, doc }";
					return false;
				}
				const bool ok = readValue(ctx, inner, field, error);
				JS_FreeValue(ctx, inner);
				if (!ok) return false;

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

			std::string js;
			if (!transformModule(path, findEsbuild(), /*withJsx*/ false, js, schema.error)) {
				return schema;
			}

			JSRuntime* runtime = JS_NewRuntime();
			if (!runtime) { schema.error = "cannot create a JS runtime"; return schema; }
			JSContext* ctx = JS_NewContext(runtime);
			if (!ctx) { JS_FreeRuntime(runtime); schema.error = "cannot create a JS context"; return schema; }

			const std::string source = std::string(colourPrelude()) + js;
			JSValue result = JS_Eval(ctx, source.c_str(), source.size(), path.c_str(),
			                         JS_EVAL_TYPE_GLOBAL);
			JSValue global = JS_GetGlobalObject(ctx);

			if (JS_IsException(result)) {
				schema.error = exceptionText(ctx);
			} else {
				JSValue exports = JS_GetPropertyStr(ctx, global, "__module");
				if (!JS_IsObject(exports)) {
					schema.error = "this file exports nothing. State is a declaration: "
					               "`export const state = { count: 0 }`";
				} else {
					JSValue stateObject = JS_GetPropertyStr(ctx, exports, "state");
					JSValue commandObject = JS_GetPropertyStr(ctx, exports, "commands");
					JSValue tableObject = JS_GetPropertyStr(ctx, exports, "tables");

					bool ok = forEachEntry(ctx, stateObject, [&](const std::string& name, JSValueConst value) {
						StateField field;
						field.name = name;
						if (!readField(ctx, value, field, schema.error)) return false;
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
							} else if (!JS_IsUndefined(value) && !JS_IsNull(value)) {
								schema.error = "command '" + name + "' should be a description, "
								               "or an empty string";
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

					schema.ok = ok && schema.error.empty();

					JS_FreeValue(ctx, tableObject);
					JS_FreeValue(ctx, commandObject);
					JS_FreeValue(ctx, stateObject);
				}
				JS_FreeValue(ctx, exports);
			}

			JS_FreeValue(ctx, global);
			JS_FreeValue(ctx, result);
			JS_FreeContext(ctx);
			JS_FreeRuntime(runtime);
			return schema;
		}
	}

	namespace {
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
	}

	StateSchema loadStateSchema(const std::string& path) {
		// TypeScript is the format. The XML reader below is kept for a declaration written
		// before the move, and nothing new should be written in it.
		const size_t dot = path.rfind('.');
		const std::string extension = (dot == std::string::npos) ? "" : path.substr(dot);
		if (extension == ".ts" || extension == ".tsx") {
			StateSchema schema = loadFromTypeScript(path);
			if (schema.ok) {
				deriveRoutes(schema);
				if (!schema.error.empty()) schema.ok = false;
			}
			return schema;
		}


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

	namespace {
		const char* pythonType(StateType type) {
			switch (type) {
			case StateType::Bool: return "bool";
			case StateType::Text: return "str";
			default:              return "float";
			}
		}
		const char* pythonAccessor(StateType type) {
			switch (type) {
			case StateType::Bool: return "bool";
			case StateType::Text: return "text";
			default:              return "number";
			}
		}
		// The declared initial value, as Python spells it. The declaration keeps it as
		// written, which is TypeScript -- close enough for a number, wrong for a boolean
		// and wrong for anything quoted.
		std::string pythonLiteral(const StateField& field) {
			if (field.type == StateType::Bool) {
				return field.value == "true" ? "True" : "False";
			}
			if (field.type == StateType::Text) {
				std::string quoted = "\"";
				for (const char c : field.value) {
					if (c == '"' || c == '\\') quoted += '\\';
					quoted += c;
				}
				return quoted + "\"";
			}
			return field.value.empty() ? "0" : field.value;
		}
		// ---- JavaScript ----
		const char* jsAccessor(StateType type) {
			switch (type) {
			case StateType::Bool: return "Bool";
			case StateType::Text: return "Text";
			default:              return "Number";
			}
		}
		// The declared initial value, as JavaScript spells it. TypeScript's `true` and a
		// number already read correctly; a string has to be quoted the way JS quotes one.
		std::string jsLiteral(const StateField& field) {
			if (field.type == StateType::Bool) {
				return field.value == "true" ? "true" : "false";
			}
			if (field.type == StateType::Text) {
				std::string quoted = "\"";
				for (const char c : field.value) {
					if (c == '"' || c == '\\') quoted += '\\';
					quoted += c;
				}
				return quoted + "\"";
			}
			return field.value.empty() ? "0" : field.value;
		}
		// A doc comment ends at the first `*/`, so a doc containing one would close the
		// comment early and spill the rest of it into the code as syntax.
		std::string jsDoc(const std::string& doc) {
			std::string out;
			for (size_t i = 0; i < doc.size(); ++i) {
				if (doc[i] == '*' && i + 1 < doc.size() && doc[i + 1] == '/') {
					out += "*\\/";
					++i;
					continue;
				}
				out += doc[i];
			}
			return out;
		}

		// ---- C# ----
		const char* csharpType(StateType type) {
			switch (type) {
			case StateType::Bool: return "bool";
			case StateType::Text: return "string";
			default:              return "double";
			}
		}
		const char* csharpAccessor(StateType type) {
			switch (type) {
			case StateType::Bool: return "Bool";
			case StateType::Text: return "Text";
			default:              return "Number";
			}
		}
		std::string csharpLiteral(const StateField& field) {
			if (field.type == StateType::Bool) {
				return field.value == "true" ? "true" : "false";
			}
			if (field.type == StateType::Text) {
				std::string quoted = "\"";
				for (const char c : field.value) {
					if (c == '"' || c == '\\') quoted += '\\';
					quoted += c;
				}
				return quoted + "\"";
			}
			return field.value.empty() ? "0" : field.value;
		}
		// A declared name used as a C# identifier. The declaration's names are its own --
		// `count`, not `Count` -- so one of them can collide with a keyword, and C#'s
		// answer to that is a verbatim identifier rather than a rename.
		std::string csharpName(const std::string& name) {
			static const char* kKeywords[] = {
				"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char",
				"checked", "class", "const", "continue", "decimal", "default", "delegate",
				"do", "double", "else", "enum", "event", "explicit", "extern", "false",
				"finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit",
				"in", "int", "interface", "internal", "is", "lock", "long", "namespace",
				"new", "null", "object", "operator", "out", "override", "params", "private",
				"protected", "public", "readonly", "ref", "return", "sbyte", "sealed",
				"short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
				"this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked",
				"unsafe", "ushort", "using", "virtual", "void", "volatile", "while",
			};
			for (const char* keyword : kKeywords) {
				if (name == keyword) return "@" + name;
			}
			return name;
		}
		// An XML doc comment is XML, so the three characters that end an element early
		// have to stop being those characters.
		std::string csharpDoc(const std::string& doc) {
			std::string out;
			for (const char c : doc) {
				if (c == '&')      out += "&amp;";
				else if (c == '<') out += "&lt;";
				else if (c == '>') out += "&gt;";
				else               out += c;
			}
			return out;
		}
		// `products` -> `Products`, for a nested class name where the declared spelling
		// would collide with the members inside it.
		std::string csharpClass(const std::string& name) {
			std::string out = name;
			if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
			return out;
		}

		// `products` -> `_Products`. The C++ side names a *row* struct; this names the
		// table itself, which is what a Python application holds.
		std::string pythonTableClass(const std::string& table) {
			std::string out = table;
			if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
			return "_" + out;
		}
		// Python has no /** */ and a doc string with a quote in it ends early.
		std::string pythonDoc(const std::string& doc) {
			std::string out;
			for (const char c : doc) {
				if (c == '"' || c == '\\') out += '\\';
				out += c;
			}
			return out;
		}
	}

	bool emitStatePython(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error) {
		std::ostringstream out;
		// The source is a path, and on Windows a path is full of backslashes -- which
		// in a Python string are escapes. Left raw, the generated module fails to
		// import on the very platform it was generated on.
		out << "\"\"\"Generated by `rda python` from " << pythonDoc(schema.source)
		    << ". Do not edit.\n"
		       "\n"
		       "The signals this application's interface is written against, and the work it\n"
		       "can ask for. The same declaration produced the C++ and the TypeScript, so a\n"
		       "name that exists here exists on every side.\n"
		       "\n"
		       "Every attribute below is a real property rather than a dynamic lookup: a\n"
		       "misspelling is an AttributeError where it is written, not a value that\n"
		       "quietly stays zero.\n"
		       "\"\"\"\n\n"
		       "from rda import _engine\n\n\n"
		       "def define() -> None:\n"
		       "\t\"\"\"Creates every signal, command and table this declaration names.\n"
		       "\n"
		       "\tA layout is compiled against these names and cannot load before they\n"
		       "\texist, so this runs first -- from on_start, ahead of load_interface.\n"
		       "\t\"\"\"\n";
		if (schema.fields.empty() && schema.commands.empty() && schema.tables.empty()) {
			out << "\tpass\n";
		}
		for (const StateField& field : schema.fields) {
			out << "\t_engine.define_" << pythonAccessor(field.type) << "(\""
			    << field.name << "\", " << pythonLiteral(field) << ")\n";
		}
		for (const StateCommand& command : schema.commands) {
			out << "\t_engine.define_command(\"" << command.name << "\")\n";
		}
		for (const StateTable& table : schema.tables) {
			out << "\t_engine.define_table(\"" << table.name << "\")\n";
			for (const StateField& column : table.columns) {
				const int kind = column.type == StateType::Bool ? 1
				               : column.type == StateType::Text ? 2 : 0;
				out << "\t_engine.define_column(\"" << table.name << "\", \""
				    << column.name << "\", " << kind << ")\n";
			}
		}

		out << "\n\nclass _State:\n"
		       "\t__slots__ = ()\n";

		if (schema.fields.empty()) {
			out << "\tpass\n";
		}
		for (const StateField& field : schema.fields) {
			const char* type = pythonType(field.type);
			const char* accessor = pythonAccessor(field.type);
			out << "\n\t@property\n"
			    << "\tdef " << field.name << "(self) -> " << type << ":\n";
			if (!field.doc.empty()) {
				out << "\t\t\"\"\"" << pythonDoc(field.doc) << "\"\"\"\n";
			}
			out << "\t\treturn _engine.get_" << accessor << "(\"" << field.name << "\")\n"
			    << "\n\t@" << field.name << ".setter\n"
			    << "\tdef " << field.name << "(self, value: " << type << ") -> None:\n"
			    << "\t\t_engine.set_" << accessor << "(\"" << field.name << "\", value)\n";
		}

		out << "\n\n# The state, as one object. Import it and assign to it.\n"
		       "State = _State()\n";

		if (!schema.commands.empty()) {
			out << "\n\nclass _Commands:\n"
			       "\t\"\"\"The work the interface can ask for. Each name is a decorator that\n"
			       "\tbinds the function under it, so a handler is written where it belongs.\n"
			       "\t\"\"\"\n"
			       "\t__slots__ = ()\n";
			for (const StateCommand& command : schema.commands) {
				out << "\n\tdef " << command.name << "(self, fn):\n";
				if (!command.doc.empty()) {
					out << "\t\t\"\"\"" << pythonDoc(command.doc) << "\"\"\"\n";
				}
				out << "\t\treturn _engine.bind_command(\"" << command.name << "\", fn)\n";
			}
			out << "\n\nCommands = _Commands()\n";
		}

		// ---- tables ----
		// A class per table rather than a dictionary of them, for the same reason the
		// state is properties rather than a lookup: a misspelled column is an error where
		// it is written. fill() writes a column at a time because every call here crosses
		// to the loop thread -- ten thousand rows is one call per column, not per cell.
		if (!schema.tables.empty()) {
			for (const StateTable& table : schema.tables) {
				const std::string className = pythonTableClass(table.name);
				out << "\n\nclass " << className << ":\n\t\"\"\"";
				if (!table.doc.empty()) out << pythonDoc(table.doc) << "\n\n\t";
				out << "The rows a <list> shows.\n"
				       "\n"
				       "\tA signal holds one value; this holds ten thousand, and showing them costs\n"
				       "\tabout as many widgets as fit on screen rather than ten thousand.\n"
				       "\t\"\"\"\n"
				       "\t__slots__ = ()\n\n"
				       "\tNAME = \"" << table.name << "\"\n"
				       "\tCOLUMNS = (";
				for (size_t i = 0; i < table.columns.size(); ++i) {
					if (i) out << ", ";
					out << "\"" << table.columns[i].name << "\"";
				}
				out << (table.columns.size() == 1 ? ",)\n" : ")\n");

				out << "\n\t@property\n"
				       "\tdef rows(self) -> int:\n"
				       "\t\t\"\"\"How many rows it holds. Assigning grows or shrinks it; new rows\n"
				       "\t\tare zero, false and empty.\n"
				       "\t\t\"\"\"\n"
				       "\t\treturn _engine.table_rows(\"" << table.name << "\")\n"
				       "\n\t@rows.setter\n"
				       "\tdef rows(self, count: int) -> None:\n"
				       "\t\t_engine.table_resize(\"" << table.name << "\", count)\n";

				out << "\n\tdef fill(self, rows) -> None:\n"
				       "\t\t\"\"\"Replaces every row.\n"
				       "\n"
				       "\t\tA row may be a mapping, an object with these attributes, or a sequence\n"
				       "\t\tin column order -- whichever the application already has.\n"
				       "\t\t\"\"\"\n"
				       "\t\trows = list(rows)\n"
				       "\t\t_engine.table_resize(\"" << table.name << "\", len(rows))\n"
				       "\t\tif not rows:\n"
				       "\t\t\treturn\n";
				for (size_t i = 0; i < table.columns.size(); ++i) {
					const StateField& column = table.columns[i];
					const char* writer = column.type == StateType::Text ? "set_texts"
					                   : column.type == StateType::Bool ? "set_bools"
					                                                    : "set_numbers";
					out << "\t\t_engine." << writer << "(\n"
					       "\t\t\t\"" << table.name << "\", \"" << column.name
					    << "\", [_engine.cell(r, \"" << column.name << "\", " << i
					    << ") for r in rows])\n";
				}

				for (const StateField& column : table.columns) {
					const char* writer = column.type == StateType::Text ? "set_texts"
					                   : column.type == StateType::Bool ? "set_bools"
					                                                    : "set_numbers";
					const char* reader = column.type == StateType::Text ? "get_cell_text"
					                   : column.type == StateType::Bool ? "get_cell_bool"
					                                                    : "get_cell_number";
					const char* type = pythonType(column.type);
					out << "\n\tdef set_" << column.name
					    << "(self, values, first: int = 0) -> None:\n";
					if (!column.doc.empty()) {
						out << "\t\t\"\"\"" << pythonDoc(column.doc)
						    << " -- a run of them, from row `first`.\"\"\"\n";
					}
					out << "\t\t_engine." << writer << "(\"" << table.name << "\", \""
					    << column.name << "\", values, first)\n"
					    << "\n\tdef " << column.name << "(self, row: int) -> " << type << ":\n";
					if (!column.doc.empty()) {
						out << "\t\t\"\"\"" << pythonDoc(column.doc) << ", in one row.\"\"\"\n";
					}
					out << "\t\treturn _engine." << reader << "(\"" << table.name << "\", \""
					    << column.name << "\", row)\n";
				}
			}

			out << "\n\nclass _Tables:\n"
			       "\t\"\"\"The row sets this application declares.\"\"\"\n"
			       "\t__slots__ = ()\n";
			for (const StateTable& table : schema.tables) {
				out << "\t" << table.name << " = " << pythonTableClass(table.name) << "()\n";
			}
			out << "\n\nTables = _Tables()\n";
		}

		if (!schema.routes.empty()) {
			out << "\n\n# The screens this application declares, and the layout each one shows.\n"
			       "#\n"
			       "# Hand this to rda.open_routes(); the order is the order they were declared,\n"
			       "# and the first is where the application opens unless `route` says otherwise.\n"
			       "# `params` names the signals that are a screen's arguments -- going back\n"
			       "# restores them along with the route, which is why they are named at all.\n"
			       "ROUTES = {\n";
			for (const StateRoute& route : schema.routes) {
				if (!route.doc.empty()) out << "\t# " << route.doc << "\n";
				out << "\t\"" << route.name << "\": {\"layout\": \"" << route.layout
				    << "\", \"params\": (";
				for (size_t i = 0; i < route.params.size(); ++i) {
					if (i) out << ", ";
					out << "\"" << route.params[i] << "\"";
				}
				// A one-element tuple needs the comma; none and two or more do not.
				out << (route.params.size() == 1 ? ",)},\n" : ")},\n");
			}
			out << "}\n";
		}

		const std::string text = out.str();
		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		std::ofstream file(outputPath, std::ios::trunc);
		if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
			error = "cannot write " + outputPath;
			return false;
		}
		return true;
	}

	bool emitStateNode(const StateSchema& schema, const std::string& outputPath,
	                   std::string& error) {
		std::ostringstream out;
		// The source is a path, and on Windows a path is full of backslashes. Inside a
		// block comment they are harmless, which is why this needs no escaping where the
		// Python docstring did -- but a `*/` in it would end the comment, and jsDoc is
		// what handles that.
		out << "/**\n"
		       " * Generated by `rda node` from " << jsDoc(schema.source) << ". Do not edit.\n"
		       " *\n"
		       " * The signals this application's interface is written against, and the work it\n"
		       " * can ask for. The same declaration produced the C++ and the TypeScript, so a\n"
		       " * name that exists here exists on every side.\n"
		       " *\n"
		       " * `State` is a real object with a real accessor per signal, not a Proxy: an\n"
		       " * editor completes the names and knows each one's type, and the object is not\n"
		       " * extensible -- so `State.notAThing = 1` throws where it is written rather than\n"
		       " * quietly becoming a property nobody reads.\n"
		       " */\n\n"
		       "import * as engine from 'rda/engine'\n\n"
		       "/**\n"
		       " * Creates every signal, command and table this declaration names.\n"
		       " *\n"
		       " * A layout is compiled against these names and cannot load before they exist, so\n"
		       " * this runs first -- after init(), and ahead of loadInterface().\n"
		       " */\n"
		       "export function define() {\n";
		for (const StateField& field : schema.fields) {
			out << "\tengine.define" << jsAccessor(field.type) << "('"
			    << field.name << "', " << jsLiteral(field) << ")\n";
		}
		for (const StateCommand& command : schema.commands) {
			out << "\tengine.defineCommand('" << command.name << "')\n";
		}
		for (const StateTable& table : schema.tables) {
			out << "\tengine.defineTable('" << table.name << "')\n";
			for (const StateField& column : table.columns) {
				const int kind = column.type == StateType::Bool ? 1
				               : column.type == StateType::Text ? 2 : 0;
				out << "\tengine.defineColumn('" << table.name << "', '"
				    << column.name << "', " << kind << ")\n";
			}
		}
		out << "}\n";

		// ---- the state ----
		out << "\n/** The state, as one object. Import it, read it, assign to it. */\n"
		       "export const State = Object.preventExtensions({\n";
		bool firstField = true;
		for (const StateField& field : schema.fields) {
			if (!firstField) out << "\n";
			firstField = false;
			if (!field.doc.empty()) {
				out << "\t/** " << jsDoc(field.doc) << " */\n";
			}
			out << "\tget " << field.name << "() { return engine.get"
			    << jsAccessor(field.type) << "('" << field.name << "') },\n"
			    << "\tset " << field.name << "(value) { engine.set"
			    << jsAccessor(field.type) << "('" << field.name << "', value) },\n";
		}
		out << "})\n";

		// ---- commands ----
		if (!schema.commands.empty()) {
			out << "\n/**\n"
			       " * The work the interface can ask for. Each name takes the function that\n"
			       " * answers it, and returns that function so it can be named or reused.\n"
			       " *\n"
			       " * The handler runs on Node's own loop rather than inside the frame the\n"
			       " * interface asked in -- see the `rda` package for why it has to.\n"
			       " */\n"
			       "export const Commands = Object.freeze({\n";
			for (const StateCommand& command : schema.commands) {
				if (!command.doc.empty()) {
					out << "\t/** " << jsDoc(command.doc) << " */\n";
				}
				out << "\t" << command.name << ": (fn) => engine.onCommand('"
				    << command.name << "', fn),\n";
			}
			out << "})\n";
		}

		// ---- tables ----
		if (!schema.tables.empty()) {
			out << "\n/** The rows a <list> shows. */\n"
			       "export const Tables = Object.freeze({\n";
			for (const StateTable& table : schema.tables) {
				out << "\t" << table.name << ": Object.freeze({\n";
				if (!table.doc.empty()) {
					out << "\t\t/** " << jsDoc(table.doc) << " */\n";
				}
				out << "\t\tNAME: '" << table.name << "',\n"
				       "\t\tCOLUMNS: [";
				for (size_t i = 0; i < table.columns.size(); ++i) {
					if (i) out << ", ";
					out << "'" << table.columns[i].name << "'";
				}
				out << "],\n";

				out << "\n\t\t/** How many rows it holds. Assigning grows or shrinks it. */\n"
				       "\t\tget rows() { return engine.tableRows('" << table.name << "') },\n"
				       "\t\tset rows(count) { engine.tableResize('" << table.name << "', count) },\n";

				out << "\n\t\t/**\n"
				       "\t\t * Replaces every row.\n"
				       "\t\t *\n"
				       "\t\t * A row may be an object with these keys, or an array in column order --\n"
				       "\t\t * whichever the application already has. Written a column at a time,\n"
				       "\t\t * because every call crosses to the engine's thread and waits.\n"
				       "\t\t */\n"
				       "\t\tfill(rows) {\n"
				       "\t\t\trows = Array.from(rows)\n"
				       "\t\t\tengine.tableResize('" << table.name << "', rows.length)\n"
				       "\t\t\tif (!rows.length) return\n";
				for (size_t i = 0; i < table.columns.size(); ++i) {
					const StateField& column = table.columns[i];
					const char* writer = column.type == StateType::Text ? "setTexts"
					                   : column.type == StateType::Bool ? "setBools"
					                                                    : "setNumbers";
					out << "\t\t\tengine." << writer << "('" << table.name << "', '"
					    << column.name << "',\n"
					       "\t\t\t\trows.map((r) => engine.cell(r, '" << column.name
					    << "', " << i << ")))\n";
				}
				out << "\t\t},\n";

				for (const StateField& column : table.columns) {
					const char* writer = column.type == StateType::Text ? "setTexts"
					                   : column.type == StateType::Bool ? "setBools"
					                                                    : "setNumbers";
					const char* reader = column.type == StateType::Text ? "getCellText"
					                   : column.type == StateType::Bool ? "getCellBool"
					                                                    : "getCellNumber";
					out << "\n";
					if (!column.doc.empty()) {
						out << "\t\t/** " << jsDoc(column.doc)
						    << " -- a run of them, from row `first`. */\n";
					}
					out << "\t\t" << setterName(column.name)
					    << "(values, first = 0) { engine." << writer << "('" << table.name
					    << "', '" << column.name << "', values, first) },\n";
					if (!column.doc.empty()) {
						out << "\t\t/** " << jsDoc(column.doc) << ", in one row. */\n";
					}
					out << "\t\t" << column.name << "(row) { return engine." << reader
					    << "('" << table.name << "', '" << column.name << "', row) },\n";
				}
				out << "\t}),\n";
			}
			out << "})\n";
		}

		// ---- routes ----
		if (!schema.routes.empty()) {
			out << "\n/**\n"
			       " * The screens this application declares, and the layout each one shows.\n"
			       " *\n"
			       " * Hand this to rda.openRoutes(); the order is the order they were declared,\n"
			       " * and the first is where the application opens unless `route` says otherwise.\n"
			       " * `params` names the signals that are a screen's arguments -- going back\n"
			       " * restores them along with the route, which is why they are named at all.\n"
			       " */\n"
			       "export const ROUTES = Object.freeze({\n";
			for (const StateRoute& route : schema.routes) {
				if (!route.doc.empty()) out << "\t/** " << jsDoc(route.doc) << " */\n";
				out << "\t" << route.name << ": { layout: '" << route.layout
				    << "', params: [";
				for (size_t i = 0; i < route.params.size(); ++i) {
					if (i) out << ", ";
					out << "'" << route.params[i] << "'";
				}
				out << "] },\n";
			}
			out << "})\n";
		}

		const std::string text = out.str();
		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		std::ofstream file(outputPath, std::ios::trunc);
		if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
			error = "cannot write " + outputPath;
			return false;
		}
		return true;
	}

	bool emitStateCSharp(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error) {
		std::ostringstream out;
		out << "// Generated by `rda csharp` from " << schema.source << ". Do not edit.\n"
		       "//\n"
		       "// The signals this application's interface is written against, and the work it\n"
		       "// can ask for. The same declaration produced the C++ and the TypeScript, so a\n"
		       "// name that exists here exists on every side.\n"
		       "//\n"
		       "// The names are the declaration's own, exactly as written -- `count`, not\n"
		       "// `Count`. That is unusual for C# and it is deliberate: `state.count` in a\n"
		       "// layout, `State::count()` in C++, `State.count` in Python and `State.count`\n"
		       "// here are one signal, and a generator that renamed things would make every\n"
		       "// reader translate between two spellings of it.\n"
		       "#nullable enable\n"
		       "using System;\n"
		       "using System.Collections.Generic;\n"
		       "using Rendeer;\n\n"
		       "namespace RdaState {\n\n"
		       "\t/// <summary>Creates every signal, command and table this declaration names.\n"
		       "\t/// <para>A layout is compiled against these names and cannot load before they\n"
		       "\t/// exist, so this runs first -- after Init(), and ahead of LoadInterface().\n"
		       "\t/// </para></summary>\n"
		       "\tpublic static class Schema {\n"
		       "\t\t/// <summary>Declares the lot. Call it once.</summary>\n"
		       "\t\tpublic static void Define() {\n";
		for (const StateField& field : schema.fields) {
			out << "\t\t\tEngine.Define" << csharpAccessor(field.type) << "(\""
			    << field.name << "\", " << csharpLiteral(field) << ");\n";
		}
		for (const StateCommand& command : schema.commands) {
			out << "\t\t\tEngine.DefineCommand(\"" << command.name << "\");\n";
		}
		for (const StateTable& table : schema.tables) {
			out << "\t\t\tEngine.DefineTable(\"" << table.name << "\");\n";
			for (const StateField& column : table.columns) {
				const int kind = column.type == StateType::Bool ? 1
				               : column.type == StateType::Text ? 2 : 0;
				out << "\t\t\tEngine.DefineColumn(\"" << table.name << "\", \""
				    << column.name << "\", " << kind << ");\n";
			}
		}
		out << "\t\t}\n\t}\n";

		// ---- the state ----
		out << "\n\t/// <summary>The state, as one class. Read it, assign to it.</summary>\n"
		       "\tpublic static class State {\n";
		for (const StateField& field : schema.fields) {
			out << "\n";
			if (!field.doc.empty()) {
				out << "\t\t/// <summary>" << csharpDoc(field.doc) << "</summary>\n";
			}
			out << "\t\tpublic static " << csharpType(field.type) << " "
			    << csharpName(field.name) << " {\n"
			    << "\t\t\tget => Engine.Get" << csharpAccessor(field.type) << "(\""
			    << field.name << "\");\n"
			    << "\t\t\tset => Engine.Set" << csharpAccessor(field.type) << "(\""
			    << field.name << "\", value);\n"
			    << "\t\t}\n";
		}
		out << "\t}\n";

		// ---- commands ----
		if (!schema.commands.empty()) {
			out << "\n\t/// <summary>The work the interface can ask for. Each name takes the\n"
			       "\t/// delegate that answers it.\n"
			       "\t/// <para>The engine calls these on its own loop thread, inside the frame\n"
			       "\t/// the interface asked in -- so a handler should be quick, and must not\n"
			       "\t/// block on anything the loop is responsible for.</para></summary>\n"
			       "\tpublic static class Commands {\n";
			for (const StateCommand& command : schema.commands) {
				out << "\n";
				if (!command.doc.empty()) {
					out << "\t\t/// <summary>" << csharpDoc(command.doc) << "</summary>\n";
				}
				out << "\t\tpublic static void " << csharpName(command.name)
				    << "(Action handler) => Engine.OnCommand(\"" << command.name
				    << "\", handler);\n";
			}
			out << "\t}\n";
		}

		// ---- tables ----
		if (!schema.tables.empty()) {
			out << "\n\t/// <summary>The rows a &lt;list&gt; shows.</summary>\n"
			       "\tpublic static class Tables {\n";
			for (const StateTable& table : schema.tables) {
				const std::string cls = csharpClass(table.name);
				out << "\n\t\t/// <summary>";
				if (!table.doc.empty()) out << csharpDoc(table.doc) << "\n\t\t/// <para>";
				out << "A signal holds one value; this holds ten thousand, and showing\n"
				       "\t\t/// them costs about as many widgets as fit on screen."
				    << (table.doc.empty() ? "" : "</para>") << "</summary>\n"
				    << "\t\tpublic static class " << cls << " {\n"
				    << "\t\t\t/// <summary>What the declaration calls it.</summary>\n"
				    << "\t\t\tpublic const string NAME = \"" << table.name << "\";\n";

				// The row struct. This is what C# buys over the other two bindings: a
				// misspelled column is a build error rather than a run-time surprise.
				out << "\n\t\t\t/// <summary>One row. A struct, so a misspelled column is a\n"
				       "\t\t\t/// compile error rather than something noticed at run time.\n"
				       "\t\t\t/// </summary>\n"
				       "\t\t\tpublic struct Row {\n";
				for (const StateField& column : table.columns) {
					if (!column.doc.empty()) {
						out << "\t\t\t\t/// <summary>" << csharpDoc(column.doc) << "</summary>\n";
					}
					out << "\t\t\t\tpublic " << csharpType(column.type) << " "
					    << csharpName(column.name) << ";\n";
				}
				out << "\t\t\t}\n";

				out << "\n\t\t\t/// <summary>How many rows it holds. Assigning grows or shrinks\n"
				       "\t\t\t/// it; new rows are zero, false and empty.</summary>\n"
				       "\t\t\tpublic static int Rows {\n"
				       "\t\t\t\tget => Engine.TableRows(NAME);\n"
				       "\t\t\t\tset => Engine.TableResize(NAME, value);\n"
				       "\t\t\t}\n";

				out << "\n\t\t\t/// <summary>Replaces every row.\n"
				       "\t\t\t/// <para>Written a column at a time, because every call crosses to\n"
				       "\t\t\t/// the engine's loop thread and waits: ten thousand rows is one\n"
				       "\t\t\t/// round trip per column, not one per cell.</para></summary>\n"
				       "\t\t\tpublic static void Fill(IEnumerable<Row> rows) {\n"
				       "\t\t\t\tList<Row> all = rows as List<Row> ?? new List<Row>(rows);\n"
				       "\t\t\t\tEngine.TableResize(NAME, all.Count);\n"
				       "\t\t\t\tif (all.Count == 0) return;\n";
				for (const StateField& column : table.columns) {
					const char* writer = column.type == StateType::Text ? "SetTexts"
					                   : column.type == StateType::Bool ? "SetBools"
					                                                    : "SetNumbers";
					const std::string field = csharpName(column.name);
					out << "\t\t\t\t{\n"
					    << "\t\t\t\t\t" << csharpType(column.type) << "[] block = new "
					    << csharpType(column.type) << "[all.Count];\n"
					    << "\t\t\t\t\tfor (int i = 0; i < all.Count; ++i) block[i] = all[i]."
					    << field << ";\n"
					    << "\t\t\t\t\tEngine." << writer << "(NAME, \"" << column.name
					    << "\", block);\n"
					    << "\t\t\t\t}\n";
				}
				out << "\t\t\t}\n";

				for (const StateField& column : table.columns) {
					const char* writer = column.type == StateType::Text ? "SetTexts"
					                   : column.type == StateType::Bool ? "SetBools"
					                                                    : "SetNumbers";
					const char* reader = column.type == StateType::Text ? "GetCellText"
					                   : column.type == StateType::Bool ? "GetCellBool"
					                                                    : "GetCellNumber";
					const char* type = csharpType(column.type);
					const std::string setter = setterName(column.name);
					out << "\n";
					if (!column.doc.empty()) {
						out << "\t\t\t/// <summary>" << csharpDoc(column.doc)
						    << " -- a run of them, from row <paramref name=\"first\"/>.</summary>\n";
					}
					out << "\t\t\tpublic static void " << setter << "(" << type
					    << "[] values, int first = 0) => Engine." << writer << "(NAME, \""
					    << column.name << "\", values, first);\n";
					if (!column.doc.empty()) {
						out << "\t\t\t/// <summary>" << csharpDoc(column.doc)
						    << ", in one row.</summary>\n";
					}
					out << "\t\t\tpublic static " << type << " " << csharpName(column.name)
					    << "(int row) => Engine." << reader << "(NAME, \"" << column.name
					    << "\", row);\n";
				}
				out << "\t\t}\n";
			}
			out << "\t}\n";
		}

		// ---- routes ----
		if (!schema.routes.empty()) {
			out << "\n\t/// <summary>The screens this application declares.\n"
			       "\t/// <para>Hand <c>All</c> to Rda.OpenRoutes(). The order is the order they\n"
			       "\t/// were declared, and the first is where the application opens unless\n"
			       "\t/// `route` already says otherwise.</para></summary>\n"
			       "\tpublic static class Routes {\n"
			       "\t\t/// <summary>Every declared screen, in declaration order.</summary>\n"
			       "\t\tpublic static readonly Route[] All = {\n";
			for (const StateRoute& route : schema.routes) {
				if (!route.doc.empty()) out << "\t\t\t// " << route.doc << "\n";
				out << "\t\t\tnew Route(\"" << route.name << "\", \"" << route.layout << "\"";
				if (!route.params.empty()) {
					out << ", new string[] { ";
					for (size_t i = 0; i < route.params.size(); ++i) {
						if (i) out << ", ";
						out << "\"" << route.params[i] << "\"";
					}
					out << " }";
				}
				out << "),\n";
			}
			out << "\t\t};\n\t}\n";
		}

		out << "}\n";

		const std::string text = out.str();
		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		std::ofstream file(outputPath, std::ios::trunc);
		if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
			error = "cannot write " + outputPath;
			return false;
		}
		return true;
	}

	bool emitStateHeader(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error) {
		std::ostringstream out;
		out << "// Generated by `rda state` from " << schema.source << ". Do not edit.\n"
		       "//\n"
		       "// Declares the signals this application's interface is written against, the\n"
		       "// work it can ask for, and typed access to both. The same declaration produced\n"
		       "// the TypeScript a layout is checked against, so a name that exists on one side\n"
		       "// exists on both.\n"
		       "#pragma once\n"
		       "#include <Core/Signals.h>\n"
		       "#include <Core/Commands.h>\n"
		       "#include <Core/Tables.h>\n"
		       "#include <vector>\n"
		       "#include <functional>\n"
		       "#include <string_view>\n\n"
		       "namespace RDA::State {\n\n"
		       "\t// Filled by define(). Held rather than looked up, so reading a signal is an\n"
		       "\t// array index instead of a hash of its name.\n"
		       "\tstruct Ids {\n";
		for (const StateField& field : schema.fields) {
			out << "\t\tuint32_t " << field.name << " = kNoSignal;\n";
		}
		out << "\t};\n\n"
		       "\tinline Ids& ids() {\n"
		       "\t\tstatic Ids table;\n"
		       "\t\treturn table;\n"
		       "\t}\n\n";

		if (!schema.commands.empty()) {
			out << "\t// The same, for the work the interface can ask for.\n"
			       "\tstruct CommandIds {\n";
			for (const StateCommand& command : schema.commands) {
				out << "\t\tuint32_t " << command.name << " = kNoCommand;\n";
			}
			out << "\t};\n\n"
			       "\tinline CommandIds& commandIds() {\n"
			       "\t\tstatic CommandIds table;\n"
			       "\t\treturn table;\n"
			       "\t}\n\n";
		}

		// ---- routes ----
		// The screens, and which layout each one shows. Router reads this; nothing else
		// has to know a route exists.
		if (!schema.routes.empty()) {
			out << "\t// The screens this application has. `route` holds which one is\n"
			       "\t// showing, and writing to it is how the interface navigates.\n"
			       "\tstruct RouteDesc {\n"
			       "\t\tstd::string_view name;\n"
			       "\t\tstd::string_view layout; // blueprint stem, without a directory\n"
			       "\t\t// Signals that are this screen's arguments. Going back restores them\n"
			       "\t\t// along with the route, which is the whole reason they are named.\n"
			       "\t\tstd::vector<std::string_view> params;\n"
			       "\t};\n\n"
			       "\tinline const std::vector<RouteDesc>& routes() {\n"
			       "\t\tstatic const std::vector<RouteDesc> table = {\n";
			for (const StateRoute& route : schema.routes) {
				if (!route.doc.empty()) out << "\t\t\t/** " << route.doc << " */\n";
				out << "\t\t\t{ \"" << route.name << "\", \"" << route.layout << "\", { ";
				for (size_t i = 0; i < route.params.size(); ++i) {
					if (i) out << ", ";
					out << "\"" << route.params[i] << "\"";
				}
				out << " } },\n";
			}
			out << "\t\t};\n"
			       "\t\treturn table;\n"
			       "\t}\n\n";
		}

		// ---- tables ----
		// A row struct plus a bulk setter, because filling a catalogue is one operation
		// and doing it field by field through indices is how a column order silently
		// drifts from the declaration it came from.
		if (!schema.tables.empty()) {
			out << "\t// The rows a list can show. Column indices are declaration order,\n"
			       "\t// which is what the setters below rely on.\n"
			       "\tstruct TableIds {\n";
			for (const StateTable& table : schema.tables) {
				out << "\t\tuint32_t " << table.name << " = kNoTable;\n";
			}
			out << "\t};\n\n"
			       "\tinline TableIds& tableIds() {\n"
			       "\t\tstatic TableIds table;\n"
			       "\t\treturn table;\n"
			       "\t}\n\n";

			for (const StateTable& table : schema.tables) {
				if (!table.doc.empty()) out << "\t/** " << table.doc << " */\n";
				out << "\tstruct " << rowStructName(table.name) << " {\n";
				for (const StateField& column : table.columns) {
					if (!column.doc.empty()) out << "\t\t/** " << column.doc << " */\n";
					// std::string, not the string_view a signal getter returns: a row is
					// something the caller fills, and a view of a temporary would dangle.
					out << "\t\t"
					    << (column.type == StateType::Text ? "std::string" : cppType(column.type))
					    << " " << column.name;
					switch (column.type) {
					case StateType::Number: out << " = 0.0"; break;
					case StateType::Bool:   out << " = false"; break;
					case StateType::Text:   break;
					}
					out << ";\n";
				}
				out << "\t};\n\n";
			}
		}

		out << "\t// Call once, before a layout is instantiated: a binding resolves the signals\n"
		       "\t// it reads and the commands it calls when it is created, and one that does\n"
		       "\t// not exist yet is guessed at or refused.\n"
		       "\tinline void define() {\n";
		for (const StateField& field : schema.fields) {
			out << "\t\tids()." << field.name << " = signals().define(\"" << field.name
			    << "\", " << cppLiteral(field) << ");\n";
		}
		for (const StateCommand& command : schema.commands) {
			out << "\t\tcommandIds()." << command.name << " = commands().define(\""
			    << command.name << "\");\n";
		}
		for (const StateTable& table : schema.tables) {
			out << "\t\ttableIds()." << table.name << " = tables().define(\""
			    << table.name << "\");\n"
			    << "\t\t{\n"
			    << "\t\t\tTable& t = tables().at(tableIds()." << table.name << ");\n";
			for (const StateField& column : table.columns) {
				const char* type = column.type == StateType::Text ? "Text"
				                 : column.type == StateType::Bool ? "Bool" : "Number";
				out << "\t\t\tt.defineColumn(\"" << column.name << "\", ColumnType::"
				    << type << ");\n";
			}
			out << "\t\t}\n";
		}
		out << "\t}\n";

		for (const StateField& field : schema.fields) {
			out << "\n";
			if (!field.doc.empty()) out << "\t/** " << field.doc << " */\n";
			out << "\tinline " << cppType(field.type) << " " << field.name << "() {\n"
			    << "\t\treturn signals()." << readerCall(field.type) << "(ids()." << field.name << ");\n"
			    << "\t}\n"
			    << "\tinline void " << setterName(field.name) << "(" << cppType(field.type)
			    << " value) {\n"
			    << "\t\tsignals().set(ids()." << field.name << ", value);\n"
			    << "\t}\n";
		}

		for (const StateCommand& command : schema.commands) {
			out << "\n";
			if (!command.doc.empty()) out << "\t/** " << command.doc << " */\n";
			out << "\t// Supplies the work. Until this is called the command exists and does\n"
			       "\t// nothing, and whatever calls it reports that rather than failing quietly.\n"
			       "\tinline void " << handlerName(command.name) << "(std::function<void()> work) {\n"
			    << "\t\tcommands().bind(commandIds()." << command.name << ", std::move(work));\n"
			    << "\t}\n";
		}

		// The accessors, after define() so the ids they use exist.
		for (const StateTable& table : schema.tables) {
			const std::string row = rowStructName(table.name);
			out << "\n"
			    << "\tinline Table& " << table.name << "() {\n"
			    << "\t\treturn tables().at(tableIds()." << table.name << ");\n"
			    << "\t}\n"
			    << "\n"
			    << "\t// Replaces every row. One call rather than a loop of setters, so the\n"
			    << "\t// column order stays where it was declared.\n"
			    << "\tinline void " << setterName(table.name)
			    << "(const std::vector<" << row << ">& rows) {\n"
			    << "\t\tTable& target = " << table.name << "();\n"
			    << "\t\ttarget.resize(rows.size());\n"
			    << "\t\tfor (size_t i = 0; i < rows.size(); ++i) {\n";
			for (size_t c = 0; c < table.columns.size(); ++c) {
				const StateField& column = table.columns[c];
				const char* call = column.type == StateType::Text ? "setText"
				                 : column.type == StateType::Bool ? "setBool"
				                                                  : "setNumber";
				out << "\t\t\ttarget." << call << "(i, " << c << ", rows[i]." << column.name << ");\n";
			}
			out << "\t\t}\n"
			    << "\t}\n";
		}

		out << "}\n";

		const std::string text = out.str();

		// A generated header usually lands in a build directory that does not exist
		// yet, so making it is part of writing the file rather than the caller's job.
		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		std::ofstream file(outputPath, std::ios::trunc);
		if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
			error = "cannot write " + outputPath;
			return false;
		}
		return true;
	}
}
