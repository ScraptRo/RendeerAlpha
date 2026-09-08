#include <Layout/LayoutCompiler.h>
#include <Layout/ExpressionParser.h>
#include "ModuleTransform.h"
#include <Logger/Logger.h>
#include <vendor/quickjs/quickjs.h>

#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		double millisSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		// ---- step 2: evaluate --------------------------------------------------------

		std::string exceptionText(JSContext* ctx) {
			JSValue error = JS_GetException(ctx);
			const char* message = JS_ToCString(ctx, error);
			std::string text = message ? message : "<unprintable exception>";
			if (message) JS_FreeCString(ctx, message);

			JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
			if (!JS_IsUndefined(stack)) {
				const char* trace = JS_ToCString(ctx, stack);
				if (trace) { text += "\n"; text += trace; JS_FreeCString(ctx, trace); }
			}
			JS_FreeValue(ctx, stack);
			JS_FreeValue(ctx, error);
			return text;
		}

		// Children arrive as trailing arguments and may themselves be arrays, since a
		// helper that returns several nodes is an ordinary thing to write. Nulls and
		// booleans are dropped so `cond && <x/>` behaves the way it reads.
		void flattenInto(JSContext* ctx, JSValue array, JSValueConst value, uint32_t& count) {
			if (JS_IsNull(value) || JS_IsUndefined(value) || JS_IsBool(value)) return;

			if (JS_IsArray(value)) {
				JSValue lengthValue = JS_GetPropertyStr(ctx, value, "length");
				uint32_t length = 0;
				JS_ToUint32(ctx, &length, lengthValue);
				JS_FreeValue(ctx, lengthValue);
				for (uint32_t i = 0; i < length; ++i) {
					JSValue item = JS_GetPropertyUint32(ctx, value, i);
					flattenInto(ctx, array, item, count);
					JS_FreeValue(ctx, item);
				}
				return;
			}
			JS_SetPropertyUint32(ctx, array, count++, JS_DupValue(ctx, value));
		}

		// The whole of the layout runtime: h(type, props, ...children) makes one plain
		// object. Deliberately dumb — it records what the builder asked for and decides
		// nothing, so every decision stays in C++ where it can be checked.
		JSValue js_h(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			JSValue node = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, node, "type",
			                  argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_NULL);
			JS_SetPropertyStr(ctx, node, "props",
			                  argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL);

			JSValue children = JS_NewArray(ctx);
			uint32_t count = 0;
			for (int i = 2; i < argc; ++i) flattenInto(ctx, children, argv[i], count);
			JS_SetPropertyStr(ctx, node, "children", children);
			return node;
		}

		// ---- step 3: flatten ---------------------------------------------------------

		// One signal a layout declared for itself. `name` is what it will be called once
		// scoping has been applied, so nothing downstream has to know it was ever local.
		struct DeclaredSignal {
			std::string name;
			std::string authored;   // what the layout wrote, before scoping
			PropKind    kind = PropKind::Number;
			double      number = 0.0;
			std::string text;
			bool        global = false;
		};

		// Everything a layout declared, keyed by the name it wrote. Used to rewrite the
		// reads inside its bindings, which name the signal the way the author did.
		using DeclaredMap = std::unordered_map<std::string, std::string>;

		struct Pending {
			JSValue  value;   // the h() object
			uint32_t parent;  // blueprint node index, or kNoParent
			// The parameter a row template was written with, inherited by everything
			// inside it. Empty outside one, which is what makes the same name elsewhere
			// the ordinary "not in scope" error.
			std::string rowParam;
		};

		// The single parameter of `(item) => ...` or `item => ...`, from the function's
		// own source. Nothing else is accepted: a template is handed one row and there is
		// nothing to fill a second parameter with.
		std::string rowParameterName(const std::string& source) {
			size_t at = 0;
			auto skipSpace = [&] { while (at < source.size() && std::isspace(static_cast<unsigned char>(source[at]))) ++at; };
			skipSpace();
			std::string name;
			if (at < source.size() && source[at] == '(') {
				++at;
				skipSpace();
				while (at < source.size() && (std::isalnum(static_cast<unsigned char>(source[at])) || source[at] == '_' || source[at] == '$')) {
					name += source[at++];
				}
				skipSpace();
				if (at >= source.size() || source[at] != ')') return {};
			} else {
				while (at < source.size() && (std::isalnum(static_cast<unsigned char>(source[at])) || source[at] == '_' || source[at] == '$')) {
					name += source[at++];
				}
			}
			return name;
		}

		// React's convention, and unambiguous: onClick is an event, onward is a property.
		bool namesAnEvent(const std::string& key) {
			return key.size() > 2 && key.compare(0, 2, "on") == 0 &&
			       std::isupper(static_cast<unsigned char>(key[2]));
		}

		// A function prop is a binding. Its source is recovered from the function itself,
		// parsed, and compiled; anything the grammar refuses fails the whole layout rather
		// than being dropped, because a binding that silently does not exist is the one
		// failure this design cannot afford.
		bool readBinding(JSContext* ctx, JSValueConst value, BlueprintBuilder& builder,
		                 uint32_t node, const std::string& key, const std::string& nodeId,
		                 std::string& error, const std::string& rowParam,
		                 const DeclaredMap& declared) {
			const char* raw = JS_ToCString(ctx, value);
			const std::string source = raw ? raw : "";
			if (raw) JS_FreeCString(ctx, raw);

			if (source.empty()) {
				error = "cannot read the source of '" + key + "' on '" + nodeId + "'";
				return false;
			}

			const bool isEvent = namesAnEvent(key);
			const ParseResult parsed = parseBinding(source, isEvent, rowParam);
			if (!parsed.ok) {
				error = "in '" + key + "' on '" + nodeId + "': " + parsed.error +
				        "\n  " + pointAt(source, parsed.position);
				return false;
			}

			// A name the layout declared for itself is stored scoped, so two layouts
			// may each have an `open` without being the same signal. Commands and columns
			// carry their own mark and are left alone.
			Program program = parsed.program;
			for (std::string& pooled : program.signals) {
				if (!pooled.empty() && (pooled.front() == '@' || pooled.front() == '#')) continue;
				const auto found = declared.find(pooled);
				if (found != declared.end()) pooled = found->second;
			}

			builder.addBinding(node, builder.intern(key),
			                   isEvent ? BindingKind::Event : BindingKind::Value,
			                   program);
			return true;
		}

		bool readProps(JSContext* ctx, JSValueConst props, BlueprintBuilder& builder,
		               uint32_t node, const std::string& nodeId, std::string& error,
		               const std::string& type, const std::string& rowParam,
		               const DeclaredMap& declared) {
			if (!JS_IsObject(props)) return true;

			JSPropertyEnum* names = nullptr;
			uint32_t count = 0;
			if (JS_GetOwnPropertyNames(ctx, &names, &count, props,
			                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
				return true;
			}

			bool ok = true;

			for (uint32_t i = 0; i < count; ++i) {
				const char* rawKey = JS_AtomToCString(ctx, names[i].atom);
				if (!rawKey) continue;
				const std::string key = rawKey;
				JS_FreeCString(ctx, rawKey);

				// `id` is lifted onto the node itself, and `children` never belongs in
				// props with this factory.
				if (key == "id" || key == "children") continue;
				// A list's row template is structure, not a property: it was called once
				// above and its result queued as this node's child.
				if (key == "row" && type == "list") continue;

				JSValue value = JS_GetProperty(ctx, props, names[i].atom);
				if (JS_IsString(value)) {
					const char* text = JS_ToCString(ctx, value);
					builder.addProp(node, builder.intern(key), PropKind::String, 0.0f,
					                builder.intern(text ? text : ""));
					if (text) JS_FreeCString(ctx, text);
				} else if (JS_IsNumber(value)) {
					double number = 0.0;
					JS_ToFloat64(ctx, &number, value);
					builder.addProp(node, builder.intern(key), PropKind::Number,
					                static_cast<float>(number), 0);
				} else if (JS_IsBool(value)) {
					builder.addProp(node, builder.intern(key), PropKind::Bool,
					                JS_ToBool(ctx, value) ? 1.0f : 0.0f, 0);
				} else if (JS_IsFunction(ctx, value)) {
					ok = readBinding(ctx, value, builder, node, key, nodeId, error, rowParam,
					                 declared);
				} else {
					error = "'" + key + "' on '" + nodeId + "' is not a string, number, "
					        "boolean or binding";
					ok = false;
				}
				JS_FreeValue(ctx, value);
				if (!ok) break;
			}
			JS_FreePropertyEnum(ctx, names, count);
			return ok;
		}

		// What a function calls itself, for an error message. Anonymous ones are rare
		// enough that "a component" reads better there than an empty pair of angles.
		std::string componentName(JSContext* ctx, JSValueConst fn) {
			JSValue nameValue = JS_GetPropertyStr(ctx, fn, "name");
			const char* text = JS_ToCString(ctx, nameValue);
			std::string name = text ? text : "";
			if (text) JS_FreeCString(ctx, text);
			JS_FreeValue(ctx, nameValue);
			return name.empty() ? "a component" : "<" + name + ">";
		}

		// `<Card title="x"/>` is a function, not an element name, so it is called here --
		// while the layout is being compiled -- and what it returns takes its place.
		//
		// That is the whole feature. A component costs nothing at run time and leaves no
		// trace in the blueprint, because by the time there is a blueprint it has already
		// happened. It is the same build-time-only bargain as the rest of this file: the
		// author writes what reads like a component tree, and what ships is nodes.
		//
		// Two rules worth stating, because they are where this differs from React:
		//
		//   The id at the call site wins over the one the component gave its own root.
		//   `<Card id="left"/>` and `<Card id="right"/>` have to be two distinguishable
		//   widgets -- hover and focus are keyed on that path -- and without this they
		//   would be one name and a numbered duplicate, which reorders when the file does.
		//
		//   A component returns exactly one element. There are no fragments: a node that
		//   contributed nothing would have to splice its children into the parent, and
		//   the blueprint's children are contiguous by construction.
		bool expandComponents(JSContext* ctx, JSValue& node, std::string& error) {
			// A component may return another component. Looping rather than recursing
			// keeps that flat, and the cap turns one that returns itself into a failed
			// build rather than a hung one.
			for (int depth = 0; depth < 64; ++depth) {
				JSValue typeValue = JS_GetPropertyStr(ctx, node, "type");
				if (!JS_IsFunction(ctx, typeValue)) {
					JS_FreeValue(ctx, typeValue);
					return true;
				}
				const std::string name = componentName(ctx, typeValue);

				// Called with the props from the call site, plus its children under
				// `children` -- so `<Card>{...}</Card>` reaches it the way it would
				// anywhere else JSX is written.
				JSValue props = JS_GetPropertyStr(ctx, node, "props");
				JSValue argument = JS_IsObject(props) ? JS_DupValue(ctx, props) : JS_NewObject(ctx);
				JS_FreeValue(ctx, props);
				JS_SetPropertyStr(ctx, argument, "children",
				                  JS_GetPropertyStr(ctx, node, "children"));

				JSValue callSiteId = JS_GetPropertyStr(ctx, argument, "id");
				JSValueConst arguments[1] = { argument };
				JSValue produced = JS_Call(ctx, typeValue, JS_UNDEFINED, 1, arguments);
				JS_FreeValue(ctx, argument);
				JS_FreeValue(ctx, typeValue);

				if (JS_IsException(produced)) {
					error = name + " threw while the layout was being compiled: "
					      + exceptionText(ctx);
					JS_FreeValue(ctx, produced);
					JS_FreeValue(ctx, callSiteId);
					return false;
				}
				if (!JS_IsObject(produced) || JS_IsArray(produced)) {
					error = name + " has to return one element. A component that returns "
					        "nothing, a list, or a fragment has no single node to become; "
					        "wrap what it returns in a <container> or a <stack>.";
					JS_FreeValue(ctx, produced);
					JS_FreeValue(ctx, callSiteId);
					return false;
				}

				if (JS_IsString(callSiteId)) {
					JSValue producedProps = JS_GetPropertyStr(ctx, produced, "props");
					if (!JS_IsObject(producedProps)) {
						JS_FreeValue(ctx, producedProps);
						producedProps = JS_NewObject(ctx);
						JS_SetPropertyStr(ctx, produced, "props",
						                  JS_DupValue(ctx, producedProps));
					}
					JS_SetPropertyStr(ctx, producedProps, "id",
					                  JS_DupValue(ctx, callSiteId));
					JS_FreeValue(ctx, producedProps);
				}
				JS_FreeValue(ctx, callSiteId);

				JS_FreeValue(ctx, node);
				node = produced;
			}
			error = "components are nested more than 64 deep here; one that returns "
			        "itself would look like this";
			return false;
		}

		bool flatten(JSContext* ctx, JSValue root, BlueprintBuilder& builder, std::string& error,
		             const DeclaredMap& declared,
		             const std::vector<DeclaredSignal>& declaredSignals) {
			// Breadth-first, so a node's children land contiguously and every parent is
			// written before them. Both are properties the loader relies on.
			std::deque<Pending> queue;
			queue.push_back({ JS_DupValue(ctx, root), kNoParent, std::string() });

			std::vector<uint32_t> firstChild, childCount;
			// The full id of each node, so a child can be named relative to its parent.
			std::vector<std::string> paths;
			// Local names already taken under a given parent, for the sibling collision
			// rule below. Keyed by parent index; kNoParent uses its own slot.
			std::unordered_map<uint32_t, std::unordered_map<std::string, int>> taken;
			bool ok = true;

			while (!queue.empty()) {
				Pending pending = queue.front();
				queue.pop_front();

				// A component is expanded before anything is read from the node, so
				// everything below this sees an ordinary element and nothing downstream
				// has to know a component was ever written.
				if (!expandComponents(ctx, pending.value, error)) {
					JS_FreeValue(ctx, pending.value);
					ok = false;
					break;
				}

				JSValue typeValue = JS_GetPropertyStr(ctx, pending.value, "type");
				const char* typeText = JS_IsString(typeValue) ? JS_ToCString(ctx, typeValue) : nullptr;
				if (!typeText) {
					error = "a node has no element name. An element is either one of the "
					        "built-in ones, like <panel>, or a function that returns one; "
					        "this is neither -- a fragment <>...</>, perhaps, which has no "
					        "node of its own to become.";
					JS_FreeValue(ctx, typeValue);
					JS_FreeValue(ctx, pending.value);
					ok = false;
					break;
				}
				const std::string type = typeText;
				JS_FreeCString(ctx, typeText);
				JS_FreeValue(ctx, typeValue);

				JSValue props = JS_GetPropertyStr(ctx, pending.value, "props");

				// ---- identity ------------------------------------------------------
				//
				// A widget's id is what the GUI keys hover and press on, so two widgets
				// sharing one is two widgets that light up together. An id written in a
				// source file cannot be unique on its own: a helper that returns a row
				// is meant to be called repeatedly, and every row it makes would carry
				// the same names.
				//
				// So the id stored here is the path to the node — parent id, then the
				// local name. That is unique by construction, and it is stable across a
				// recompile in the way a reload needs: renaming a sibling or reordering
				// two rows does not rename anything else.
				std::string local;
				if (JS_IsObject(props)) {
					JSValue idValue = JS_GetPropertyStr(ctx, props, "id");
					if (JS_IsString(idValue)) {
						const char* text = JS_ToCString(ctx, idValue);
						if (text) { local = text; JS_FreeCString(ctx, text); }
					}
					JS_FreeValue(ctx, idValue);
				}
				if (local.empty()) local = type;

				// Two siblings may still ask for the same local name — two unnamed
				// labels in one stack, say. The first keeps it and the rest are
				// numbered, so the result stays unique without renaming the one the
				// author actually named.
				if (const int seen = taken[pending.parent][local]++; seen > 0) {
					local += "#" + std::to_string(seen + 1);
				}

				const std::string id = (pending.parent == kNoParent)
					? local
					: paths[pending.parent] + "/" + local;

				const uint32_t index =
					builder.addNode(builder.intern(type), builder.intern(id), pending.parent);
				firstChild.push_back(0);
				childCount.push_back(0);
				paths.push_back(id);

				if (pending.parent != kNoParent) {
					if (childCount[pending.parent] == 0) firstChild[pending.parent] = index;
					++childCount[pending.parent];
				}

				// A list's row template: called once, here, and what it returns becomes
				// the list's child. It is not evaluated per row -- nothing is, at run
				// time. What varies per row are the column reads inside its bindings.
				std::string templateParam;
				JSValue rowTemplate = JS_UNDEFINED;
				if (type == "list" && JS_IsObject(props)) {
					JSValue fn = JS_GetPropertyStr(ctx, props, "row");
					if (JS_IsFunction(ctx, fn)) {
						const char* raw = JS_ToCString(ctx, fn);
						templateParam = rowParameterName(raw ? raw : "");
						if (raw) JS_FreeCString(ctx, raw);
						if (templateParam.empty()) {
							error = "the row template on '" + id + "' needs one parameter, "
							        "naming the row it is given: row={(item) => ...}";
							ok = false;
						} else {
							// Handed an empty object rather than a row: a template is
							// structure, and anything that reads a column has to do it
							// inside a thunk, where it is compiled rather than run.
							JSValue argument = JS_NewObject(ctx);
							rowTemplate = JS_Call(ctx, fn, JS_UNDEFINED, 1, &argument);
							JS_FreeValue(ctx, argument);
							if (JS_IsException(rowTemplate)) {
								error = "the row template on '" + id + "' threw while it "
								        "was being read";
								JS_FreeValue(ctx, rowTemplate);
								rowTemplate = JS_UNDEFINED;
								ok = false;
							}
						}
					}
					JS_FreeValue(ctx, fn);
					if (!ok) {
						JS_FreeValue(ctx, props);
						JS_FreeValue(ctx, pending.value);
						break;
					}
				}

				if (index == 0) {
					// While the root is still the node being filled: props are contiguous
					// per node, and addProp discards anything written once another node
					// has started. Carried as properties rather than in a section of
					// their own, which would mean a new field in every blueprint header.
					for (const DeclaredSignal& one : declaredSignals) {
						builder.addProp(0, builder.intern("!signal " + one.name), one.kind,
						                static_cast<float>(one.number),
						                one.kind == PropKind::String
							                ? builder.intern(one.text) : 0);
					}
				}

				if (!readProps(ctx, props, builder, index, id, error, type, pending.rowParam,
				               declared)) {
					JS_FreeValue(ctx, props);
					JS_FreeValue(ctx, pending.value);
					ok = false;
					break;
				}
				JS_FreeValue(ctx, props);

				if (!JS_IsUndefined(rowTemplate)) {
					queue.push_back({ rowTemplate, index, templateParam });
				}

				JSValue children = JS_GetPropertyStr(ctx, pending.value, "children");
				if (JS_IsArray(children)) {
					JSValue lengthValue = JS_GetPropertyStr(ctx, children, "length");
					uint32_t length = 0;
					JS_ToUint32(ctx, &length, lengthValue);
					JS_FreeValue(ctx, lengthValue);
					for (uint32_t i = 0; i < length; ++i) {
						queue.push_back({ JS_GetPropertyUint32(ctx, children, i), index, pending.rowParam });
					}
				}
				JS_FreeValue(ctx, children);
				JS_FreeValue(ctx, pending.value);
			}

			for (Pending& leftover : queue) JS_FreeValue(ctx, leftover.value);
			if (!ok) return false;

			for (uint32_t i = 0; i < firstChild.size(); ++i) {
				builder.setChildRange(i, firstChild[i], childCount[i]);
			}
			return true;
		}

		// Reads __rdaSignals, left behind by the prelude's signal(). The scope is the
		// file's own name, which is stable across a rebuild and readable in a log -- an
		// index would renumber the moment a declaration moved.
		std::vector<DeclaredSignal> collectSignals(JSContext* ctx, JSValueConst global,
		                                           const std::string& sourcePath,
		                                           std::string& error) {
			std::vector<DeclaredSignal> out;
			const std::string scope = std::filesystem::path(sourcePath).stem().string();

			JSValue list = JS_GetPropertyStr(ctx, global, "__rdaSignals");
			uint32_t length = 0;
			JSValue lengthValue = JS_GetPropertyStr(ctx, list, "length");
			JS_ToUint32(ctx, &length, lengthValue);
			JS_FreeValue(ctx, lengthValue);

			for (uint32_t i = 0; i < length; ++i) {
				JSValue entry = JS_GetPropertyUint32(ctx, list, i);
				JSValue nameValue = JS_GetPropertyStr(ctx, entry, "name");
				JSValue value = JS_GetPropertyStr(ctx, entry, "value");
				JSValue globalFlag = JS_GetPropertyStr(ctx, entry, "global");

				DeclaredSignal declared;
				const char* rawName = JS_ToCString(ctx, nameValue);
				declared.authored = rawName ? rawName : "";
				if (rawName) JS_FreeCString(ctx, rawName);
				declared.global = JS_ToBool(ctx, globalFlag) != 0;
				declared.name = declared.global ? declared.authored
				                                : scope + "/" + declared.authored;

				if (JS_IsBool(value)) {
					declared.kind = PropKind::Bool;
					declared.number = JS_ToBool(ctx, value) ? 1.0 : 0.0;
				} else if (JS_IsNumber(value)) {
					declared.kind = PropKind::Number;
					JS_ToFloat64(ctx, &declared.number, value);
				} else if (JS_IsString(value)) {
					declared.kind = PropKind::String;
					const char* text = JS_ToCString(ctx, value);
					declared.text = text ? text : "";
					if (text) JS_FreeCString(ctx, text);
				} else {
					error = "signal('" + declared.authored + "') needs a number, a boolean "
					        "or a string for its initial value";
				}

				JS_FreeValue(ctx, globalFlag);
				JS_FreeValue(ctx, value);
				JS_FreeValue(ctx, nameValue);
				JS_FreeValue(ctx, entry);
				if (!error.empty()) break;
				out.push_back(std::move(declared));
			}
			JS_FreeValue(ctx, list);
			return out;
		}

		bool evaluate(const std::string& js, const std::string& name, const std::string& propsJson,
		              BlueprintBuilder& builder, std::string& error) {
			JSRuntime* runtime = JS_NewRuntime();
			if (!runtime) { error = "cannot create a JS runtime"; return false; }
			JSContext* ctx = JS_NewContext(runtime);
			if (!ctx) { JS_FreeRuntime(runtime); error = "cannot create a JS context"; return false; }

			bool ok = false;
			JSValue global = JS_GetGlobalObject(ctx);
			JS_SetPropertyStr(ctx, global, "h", JS_NewCFunction(ctx, js_h, "h", 3));

			// The helpers first, so the module can call them as it is evaluated.
			const std::string source =
				std::string(colourPrelude()) + layoutPrelude() + js;
			JSValue result = JS_Eval(ctx, source.c_str(), source.size(), name.c_str(),
			                         JS_EVAL_TYPE_GLOBAL);
			if (JS_IsException(result)) {
				error = exceptionText(ctx);
			} else {
				JSValue exports = JS_GetPropertyStr(ctx, global, "__module");
				JSValue builderFn = JS_GetPropertyStr(ctx, exports, "default");

				// Collected before the tree is walked, because the bindings in it name
				// these signals the way the author wrote them and have to be rewritten.
				const std::vector<DeclaredSignal> declaredSignals =
					collectSignals(ctx, global, name, error);
				DeclaredMap declared;
				for (const DeclaredSignal& one : declaredSignals) {
					if (!one.global) declared.emplace(one.authored, one.name);
				}

				if (!JS_IsFunction(ctx, builderFn)) {
					error = "the layout has no default export, or it is not a function. "
					        "A layout file exports the function that builds it: "
					        "`export default function Name(props) { return <panel/> }`";
				} else {
					JSValue props = JS_ParseJSON(ctx, propsJson.c_str(), propsJson.size(), "<props>");
					if (JS_IsException(props)) {
						JS_FreeValue(ctx, props);
						props = JS_NewObject(ctx);
					}
					JSValue tree = JS_Call(ctx, builderFn, JS_UNDEFINED, 1, &props);
					if (JS_IsException(tree)) {
						error = exceptionText(ctx);
					} else if (!JS_IsObject(tree)) {
						error = "the builder returned nothing to lay out";
					} else {
						ok = error.empty() &&
						     flatten(ctx, tree, builder, error, declared, declaredSignals);
					}
					JS_FreeValue(ctx, tree);
					JS_FreeValue(ctx, props);
				}
				JS_FreeValue(ctx, builderFn);
				JS_FreeValue(ctx, exports);
			}

			JS_FreeValue(ctx, result);
			JS_FreeValue(ctx, global);
			JS_FreeContext(ctx);
			JS_FreeRuntime(runtime);
			return ok;
		}
	}

	std::string blueprintPathFor(const std::string& sourcePath) {
		std::filesystem::path path(sourcePath);
		path.replace_extension(".rdab");
		return path.string();
	}

	CompileResult compileFile(const std::string& sourcePath, const CompileOptions& options) {
		CompileResult result;

		const Clock::time_point transformStart = Clock::now();
		std::string js;
		if (!transformModule(sourcePath, options.esbuildPath, /*withJsx*/ true, js, result.error))
			return result;
		result.transformMs = millisSince(transformStart);

		const Clock::time_point evaluateStart = Clock::now();
		BlueprintBuilder builder;
		if (!evaluate(js, sourcePath, options.propsJson, builder, result.error)) return result;
		result.evaluateMs = millisSince(evaluateStart);

		result.bytes     = builder.finish();
		result.nodeCount = builder.nodeCount();
		result.bindingCount = builder.bindingCount();
		result.ok        = true;
		return result;
	}

	CompileResult compileToFile(const std::string& sourcePath, const std::string& outputPath,
	                            const CompileOptions& options) {
		CompileResult result = compileFile(sourcePath, options);
		if (!result.ok) return result;

		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(reinterpret_cast<const char*>(result.bytes.data()),
		                       static_cast<std::streamsize>(result.bytes.size()))) {
			result.ok = false;
			result.error = "cannot write " + outputPath;
		}
		return result;
	}
}
