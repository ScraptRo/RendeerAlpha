#include <Layout/ThemeCompiler.h>
#include <Layout/Blueprint.h>
#include <Layout/ThemeSchema.h>
#include "ModuleTransform.h"

#include <vendor/quickjs/quickjs.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		double millisSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		std::string exceptionText(JSContext* ctx) {
			JSValue error = JS_GetException(ctx);
			const char* message = JS_ToCString(ctx, error);
			std::string text = message ? message : "<unprintable exception>";
			if (message) JS_FreeCString(ctx, message);
			JS_FreeValue(ctx, error);
			return text;
		}

		// A field the schema does not know is a build failure, not a shrug.
		//
		// Before this, a theme could say `borderWith: 1` and get no border and no message:
		// the reader looks for the fields it knows and never sees the rest. That is the
		// worst kind of mistake to leave to a person -- a missing border reads as a design
		// decision, and a wrong colour reads as the colour somebody chose.
		//
		// The suggestion is worth the trouble because the mistake is nearly always a
		// letter, and a field name is not something you can work out from first principles.
		bool checkField(const ThemeFieldDesc* fields, size_t fieldCount, bool allowCommon,
		                const std::string& field, const std::string& what, std::string& error) {
			for (size_t i = 0; i < fieldCount; ++i) {
				if (field == fields[i].name) return true;
			}
			if (allowCommon) {
				size_t commonCount = 0;
				const ThemeFieldDesc* common = commonThemeFields(commonCount);
				for (size_t i = 0; i < commonCount; ++i) {
					if (field == common[i].name) return true;
				}
			}
			error = what + " has no field called `" + field + "`";
			// nearestThemeField takes an element, and the list here may be a syntax palette
			// instead -- so it is handed whichever list this call is checking against. A
			// palette gets no suggestion from the common fields, because `base` is not a
			// misspelling of a token kind.
			const ThemeElementDesc checking{ "", fields, fieldCount, "" };
			const std::string_view nearest = nearestThemeField(checking, field);
			if (!nearest.empty()) error += ". Did you mean `" + std::string(nearest) + "`?";
			return false;
		}

		// One object of scalars becomes one node's props. A nested object becomes a child
		// node named for the field it came from -- which is how a text field carries a
		// whole syntax palette without the format needing to know what a palette is.
		//
		// `fields` is what the keys are checked against. A nested object is checked against
		// its own list rather than the element's, which is the whole of what "nested" means
		// here: `syntax` is not a field with sub-fields, it is a different set of names.
		bool readFields(JSContext* ctx, JSValueConst object, BlueprintBuilder& builder,
		                uint32_t node, const std::string& nodeId,
		                std::vector<uint32_t>& childFirst, std::vector<uint32_t>& childCount,
		                const ThemeFieldDesc* fields, size_t fieldCount, bool allowCommon,
		                const std::string& what, std::string& error) {
			JSPropertyEnum* names = nullptr;
			uint32_t count = 0;
			if (JS_GetOwnPropertyNames(ctx, &names, &count, object,
			                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
				return true;
			}
			bool ok = true;

			// Two passes: scalars first so a node's props stay contiguous, then the nested
			// objects, which have to become children after this node exists.
			for (uint32_t pass = 0; pass < 2; ++pass) {
				for (uint32_t i = 0; i < count; ++i) {
					const char* rawKey = JS_AtomToCString(ctx, names[i].atom);
					if (!rawKey) continue;
					const std::string key = rawKey;
					JS_FreeCString(ctx, rawKey);

					JSValue value = JS_GetProperty(ctx, object, names[i].atom);
					const bool nested = JS_IsObject(value) && !JS_IsFunction(ctx, value);

					// Once, on the first pass, so a name is not reported twice.
					if (pass == 0 && !checkField(fields, fieldCount, allowCommon, key,
					                             what, error)) {
						JS_FreeValue(ctx, value);
						ok = false;
						break;
					}

					if (pass == 0 && !nested) {
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
						}
					} else if (pass == 1 && nested) {
						const std::string childId = nodeId + "/" + key;
						const uint32_t child =
							builder.addNode(builder.intern(key), builder.intern(childId), node);
						childFirst.push_back(0);
						childCount.push_back(0);
						if (childCount[node] == 0) childFirst[node] = child;
						++childCount[node];

						// The only nested thing a theme has is a syntax palette, so that is
						// what its keys are checked against. A different field written as an
						// object is caught by the check above, before this runs.
						size_t kindCount = 0;
						const ThemeFieldDesc* kinds = syntaxFields(kindCount);
						ok = readFields(ctx, value, builder, child, childId, childFirst,
						                childCount, kinds, kindCount, false,
						                what + " `" + key + "`", error);
					}
					JS_FreeValue(ctx, value);
					if (!ok) break;
				}
				if (!ok) break;
			}
			JS_FreePropertyEnum(ctx, names, count);
			return ok;
		}

		bool evaluate(const std::string& js, const std::string& name, BlueprintBuilder& builder,
		              std::vector<std::pair<std::string, std::string>>& variants,
		              std::string& error) {
			JSRuntime* runtime = JS_NewRuntime();
			if (!runtime) { error = "cannot create a JS runtime"; return false; }
			JSContext* ctx = JS_NewContext(runtime);
			if (!ctx) { JS_FreeRuntime(runtime); error = "cannot create a JS context"; return false; }

			bool ok = false;
			JSValue global = JS_GetGlobalObject(ctx);
			// The colour helpers first, so the module can call them as it is evaluated.
			const std::string source = std::string(colourPrelude()) + js;
			JSValue result = JS_Eval(ctx, source.c_str(), source.size(), name.c_str(),
			                         JS_EVAL_TYPE_GLOBAL);

			if (JS_IsException(result)) {
				error = exceptionText(ctx);
			} else {
				JSValue exports = JS_GetPropertyStr(ctx, global, "__module");
				JSPropertyEnum* elements = nullptr;
				uint32_t elementCount = 0;

				if (!JS_IsObject(exports) ||
				    JS_GetOwnPropertyNames(ctx, &elements, &elementCount, exports,
				                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
					error = "the theme exports nothing. A theme is a set of exported objects, "
					        "one per widget type: `export const button = { primary: { ... } }`";
				} else {
					// A blueprint's children are contiguous and its parents come first, so
					// the ranges are recorded as nodes are added and written at the end.
					std::vector<uint32_t> childFirst, childCount;
					ok = true;

					for (uint32_t e = 0; e < elementCount && ok; ++e) {
						const char* rawElement = JS_AtomToCString(ctx, elements[e].atom);
						if (!rawElement) continue;
						const std::string element = rawElement;
						JS_FreeCString(ctx, rawElement);

						// esbuild marks its namespace object; not a widget type.
						if (element == "__esModule") continue;

						// An export nothing is styled by is a mistake worth stopping for:
						// a theme that says `buttons` gets no buttons and no message.
						const ThemeElementDesc* described = findThemeElement(element);
						if (!described) {
							error = "nothing is styled by `" + element + "`";
							const std::string_view nearest = nearestThemeElement(element);
							if (!nearest.empty()) {
								error += ". Did you mean `" + std::string(nearest) + "`?";
							}
							ok = false;
							break;
						}

						JSValue byVariant = JS_GetProperty(ctx, exports, elements[e].atom);
						JSPropertyEnum* names = nullptr;
						uint32_t nameCount = 0;
						if (!JS_IsObject(byVariant) ||
						    JS_GetOwnPropertyNames(ctx, &names, &nameCount, byVariant,
						                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
							error = "'" + element + "' should be an object of variants, "
							        "each naming one style";
							ok = false;
							JS_FreeValue(ctx, byVariant);
							break;
						}

						for (uint32_t v = 0; v < nameCount; ++v) {
							const char* rawName = JS_AtomToCString(ctx, names[v].atom);
							if (!rawName) continue;
							const std::string variant = rawName;
							JS_FreeCString(ctx, rawName);

							JSValue style = JS_GetProperty(ctx, byVariant, names[v].atom);
							if (JS_IsObject(style)) {
								const uint32_t node = builder.addNode(
									builder.intern(element), builder.intern(variant), kNoParent);
								childFirst.push_back(0);
								childCount.push_back(0);
								ok = readFields(ctx, style, builder, node, variant,
								                childFirst, childCount,
								                described->fields, described->fieldCount, true,
								                "`" + element + "." + variant + "`", error);
								if (!ok) {
									JS_FreeValue(ctx, style);
									break;
								}
								variants.emplace_back(element, variant);
							}
							JS_FreeValue(ctx, style);
						}
						JS_FreePropertyEnum(ctx, names, nameCount);
						JS_FreeValue(ctx, byVariant);
						if (!ok) break;
					}

					for (uint32_t i = 0; i < childFirst.size(); ++i) {
						builder.setChildRange(i, childFirst[i], childCount[i]);
					}
					JS_FreePropertyEnum(ctx, elements, elementCount);
				}
				JS_FreeValue(ctx, exports);
			}

			JS_FreeValue(ctx, result);
			JS_FreeValue(ctx, global);
			JS_FreeContext(ctx);
			JS_FreeRuntime(runtime);
			return ok;
		}
	}

	std::string themePathFor(const std::string& sourcePath) {
		std::filesystem::path path(sourcePath);
		path.replace_extension(".rdth");
		return path.string();
	}

	ThemeCompileResult compileTheme(const std::string& sourcePath, const std::string& esbuildPath) {
		ThemeCompileResult result;

		const Clock::time_point transformStart = Clock::now();
		std::string js;
		// No JSX: a theme is data, and an angle bracket in one is a mistake rather than
		// markup.
		if (!transformModule(sourcePath, esbuildPath, /*withJsx*/ false, js, result.error)) {
			return result;
		}
		result.transformMs = millisSince(transformStart);

		const Clock::time_point evaluateStart = Clock::now();
		BlueprintBuilder builder;
		if (!evaluate(js, sourcePath, builder, result.variants, result.error)) return result;
		result.evaluateMs = millisSince(evaluateStart);

		result.bytes = builder.finish();
		result.variantCount = result.variants.size();
		result.ok = true;
		return result;
	}

	ThemeCompileResult compileThemeToFile(const std::string& sourcePath,
	                                      const std::string& outputPath,
	                                      const std::string& esbuildPath) {
		ThemeCompileResult result = compileTheme(sourcePath, esbuildPath);
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
