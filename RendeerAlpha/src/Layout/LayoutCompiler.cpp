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

		struct Pending {
			JSValue  value;   // the h() object
			uint32_t parent;  // blueprint node index, or kNoParent
		};

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
		                 std::string& error) {
			const char* raw = JS_ToCString(ctx, value);
			const std::string source = raw ? raw : "";
			if (raw) JS_FreeCString(ctx, raw);

			if (source.empty()) {
				error = "cannot read the source of '" + key + "' on '" + nodeId + "'";
				return false;
			}

			const ParseResult parsed = parseBinding(source);
			if (!parsed.ok) {
				error = "in '" + key + "' on '" + nodeId + "': " + parsed.error +
				        "\n  " + pointAt(source, parsed.position);
				return false;
			}

			builder.addBinding(node, builder.intern(key),
			                   namesAnEvent(key) ? BindingKind::Event : BindingKind::Value,
			                   parsed.program);
			return true;
		}

		bool readProps(JSContext* ctx, JSValueConst props, BlueprintBuilder& builder,
		               uint32_t node, const std::string& nodeId, std::string& error) {
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
					ok = readBinding(ctx, value, builder, node, key, nodeId, error);
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

		bool flatten(JSContext* ctx, JSValue root, BlueprintBuilder& builder, std::string& error) {
			// Breadth-first, so a node's children land contiguously and every parent is
			// written before them. Both are properties the loader relies on.
			std::deque<Pending> queue;
			queue.push_back({ JS_DupValue(ctx, root), kNoParent });

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

				JSValue typeValue = JS_GetPropertyStr(ctx, pending.value, "type");
				const char* typeText = JS_IsString(typeValue) ? JS_ToCString(ctx, typeValue) : nullptr;
				if (!typeText) {
					error = "a node has no element name; only intrinsic elements like "
					        "<panel> are supported so far, not component references";
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

				if (!readProps(ctx, props, builder, index, id, error)) {
					JS_FreeValue(ctx, props);
					JS_FreeValue(ctx, pending.value);
					ok = false;
					break;
				}
				JS_FreeValue(ctx, props);

				JSValue children = JS_GetPropertyStr(ctx, pending.value, "children");
				if (JS_IsArray(children)) {
					JSValue lengthValue = JS_GetPropertyStr(ctx, children, "length");
					uint32_t length = 0;
					JS_ToUint32(ctx, &length, lengthValue);
					JS_FreeValue(ctx, lengthValue);
					for (uint32_t i = 0; i < length; ++i) {
						queue.push_back({ JS_GetPropertyUint32(ctx, children, i), index });
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

		bool evaluate(const std::string& js, const std::string& name, const std::string& propsJson,
		              BlueprintBuilder& builder, std::string& error) {
			JSRuntime* runtime = JS_NewRuntime();
			if (!runtime) { error = "cannot create a JS runtime"; return false; }
			JSContext* ctx = JS_NewContext(runtime);
			if (!ctx) { JS_FreeRuntime(runtime); error = "cannot create a JS context"; return false; }

			bool ok = false;
			JSValue global = JS_GetGlobalObject(ctx);
			JS_SetPropertyStr(ctx, global, "h", JS_NewCFunction(ctx, js_h, "h", 3));

			JSValue result = JS_Eval(ctx, js.c_str(), js.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
			if (JS_IsException(result)) {
				error = exceptionText(ctx);
			} else {
				JSValue exports = JS_GetPropertyStr(ctx, global, "__module");
				JSValue builderFn = JS_GetPropertyStr(ctx, exports, "default");

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
						ok = flatten(ctx, tree, builder, error);
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
