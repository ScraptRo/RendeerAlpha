#include <Layout/LayoutCompiler.h>
#include <Layout/WidgetSchema.h>
#include <Layout/ExpressionParser.h>
#include <Layout/ModuleTransform.h>
#include "JsContext.h"
#include "ScriptCompiler.h"
#include "ScriptPreludes.h"
#include "SourceMap.h"
#include <Logger/Logger.h>
#include <vendor/quickjs/quickjs.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		// The options of the compile in progress, reached from the walk without threading
		// them through every function of it. One compile at a time per thread.
		thread_local const CompileOptions* gOptions = nullptr;
		const CompileOptions& options() {
			static const CompileOptions defaults;
			return gOptions ? *gOptions : defaults;
		}
		struct OptionsScope {
			const CompileOptions* previous;
			explicit OptionsScope(const CompileOptions& now) : previous(gOptions) { gOptions = &now; }
			~OptionsScope() { gOptions = previous; }
		};

		double millisSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		// ---- step 2: evaluate --------------------------------------------------------

		// Where the lines of the bundle being evaluated came from, as far as an exception it
		// throws is concerned: esbuild's map of it, and the name QuickJS was given for it,
		// which is what its frames say.
		struct Origins {
			const std::string& sourceMap;
			const std::string& name;
		};

		// An exception the bundle threw, with each of its frames moved from the line esbuild
		// printed to the line somebody wrote. Those were apart by whatever the transform took
		// out -- types, mostly -- by every import pasted in above, and by the line wrapBundle
		// puts in front of it all, so a throw on a component's third line was reported on
		// its fifth. The map is read here and only here, on the way out with an error, so a
		// layout that throws nothing never decodes it.
		std::string thrownBy(JSContext* ctx, const Origins& origins) {
			return thrownText(ctx, SourceMap(origins.sourceMap, origins.name, kWrapLinesBefore));
		}

		// The helpers a file is built with, evaluated as a script of their own before it: in
		// front of its source, a frame inside one would be reported as a line of the file --
		// and every line of the file after them would be off by however long they are.
		bool evaluateHelpers(JSContext* ctx, const std::string& helpers, std::string& error) {
			return evalScript(ctx, helpers.c_str(), helpers.size(), "<rda helpers>", error);
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

		// An element written as an object rather than in JSX: { type: "label", text: "Hi" },
		// its properties beside `type`. h() keeps them under `props`, so having none is what
		// tells the two apart, and this turns the one into the other before anything reads
		// it, so that nothing after here knows which was written. A new object rather than
		// a change to that one: a constant shared by two parents is read twice.
		void normaliseElement(JSContext* ctx, JSValue& node) {
			if (!JS_IsObject(node) || JS_IsArray(node) || JS_IsFunction(ctx, node)) return;
			JSValue existing = JS_GetPropertyStr(ctx, node, "props");
			const bool fromJsx = !JS_IsUndefined(existing);
			JS_FreeValue(ctx, existing);
			if (fromJsx) return;

			JSValue element = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, element, "type", JS_GetPropertyStr(ctx, node, "type"));

			JSValue props = JS_NewObject(ctx);
			JSPropertyEnum* names = nullptr;
			uint32_t count = 0;
			if (JS_GetOwnPropertyNames(ctx, &names, &count, node,
			                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
				for (uint32_t i = 0; i < count; ++i) {
					const char* key = JS_AtomToCString(ctx, names[i].atom);
					const bool structure = !key || std::string_view(key) == "type" ||
					                       std::string_view(key) == "children";
					if (key) JS_FreeCString(ctx, key);
					if (structure) continue;
					JS_SetProperty(ctx, props, names[i].atom, JS_GetProperty(ctx, node, names[i].atom));
				}
				JS_FreePropertyEnum(ctx, names, count);
			}
			JS_SetPropertyStr(ctx, element, "props", props);

			// One element or a list of them, with a false or a null dropped, as in JSX.
			JSValue children = JS_NewArray(ctx);
			uint32_t childCount = 0;
			JSValue given = JS_GetPropertyStr(ctx, node, "children");
			flattenInto(ctx, children, given, childCount);
			JS_FreeValue(ctx, given);
			JS_SetPropertyStr(ctx, element, "children", children);

			JS_FreeValue(ctx, node);
			node = element;
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

		// ---- names the schema does not know -------------------------------------------------
		//
		// `<buton widht={1}>` used to compile and be ignored when the layout loaded, with a line
		// in a log that nobody was reading yet. The schema already says what every element takes,
		// so the compiler can say so too, at the moment the author is looking: with the closest
		// name it does know, because a typo is nearly always one.
		//
		// A warning, not an error, for the reason the loader's is: a layout compiled against a
		// newer engine should still build against this one. `strict` is for the build that wants
		// the other answer.

		// Where the compile in progress collects them: the walk is several functions deep and
		// they are not all in a position to hand a list back.
		thread_local std::vector<std::string>* gNaming = nullptr;
		struct NamingScope {
			std::vector<std::string>* previous;
			explicit NamingScope(std::vector<std::string>* now) : previous(gNaming) { gNaming = now; }
			~NamingScope() { gNaming = previous; }
		};

		char lowered(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

		// Edit distance with a swap counting as one edit, because widht for width is the commonest
		// mistake there is. Case is ignored: onclick is onClick with a different hand on the shift
		// key, and should come back as a suggestion rather than as a stranger.
		size_t nameDistance(std::string_view a, std::string_view b) {
			const size_t n = a.size(), m = b.size();
			std::vector<std::vector<size_t>> d(n + 1, std::vector<size_t>(m + 1, 0));
			for (size_t i = 0; i <= n; ++i) d[i][0] = i;
			for (size_t j = 0; j <= m; ++j) d[0][j] = j;
			for (size_t i = 1; i <= n; ++i) {
				for (size_t j = 1; j <= m; ++j) {
					const size_t cost = lowered(a[i - 1]) == lowered(b[j - 1]) ? 0 : 1;
					d[i][j] = (std::min)({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
					if (i > 1 && j > 1 && lowered(a[i - 1]) == lowered(b[j - 2]) &&
					    lowered(a[i - 2]) == lowered(b[j - 1])) {
						d[i][j] = (std::min)(d[i][j], d[i - 2][j - 2] + 1);
					}
				}
			}
			return d[n][m];
		}

		// How far off a name may be and still be offered: a short name has little room to be
		// wrong in, and offering `x` for `h` is noise.
		size_t nameTolerance(size_t length) { return length <= 3 ? 1 : (length + 1) / 3; }

		// Names that people write because another toolkit taught them, and that are too far from
		// the right one for a distance to find. `element` is empty for any element.
		struct KnownGuess {
			const char* element;
			const char* name;
			const char* instead;   // what it is called here; null when nothing is
			const char* note;
		};
		constexpr KnownGuess kPropertyGuesses[] = {
			{ "checkbox", "text",       "label",   "a <checkbox> calls the text beside its box its label" },
			{ "checkbox", "checked",    "value",   "a <checkbox> is ticked when its value is true" },
			{ "checkbox", "onClick",    "onChange", "a <checkbox> reports being toggled, with the new state" },
			{ "button",   "label",      "text",    "a <button> calls the text drawn on it its text" },
			{ "button",   "onPress",    "onClick", nullptr },
			{ "button",   "onChange",   "onClick", nullptr },
			{ "label",    "label",      "text",    nullptr },
			{ "label",    "content",    "text",    nullptr },
			{ "label",    "value",      "text",    nullptr },
			{ "textfield", "value",     "text",    "a <textfield> holds its contents in text" },
			{ "textfield", "onInput",   "onChange", nullptr },
			{ "slider",   "onInput",    "onChange", nullptr },
			{ "image",    "source",     "src",     nullptr },
			{ "image",    "href",       "src",     nullptr },
			{ "image",    "url",        "src",     nullptr },
			{ "stack",    "direction",  "arrange", "arrange is horizontal or vertical" },
			{ "stack",    "orientation", "arrange", "arrange is horizontal or vertical" },
			{ "stack",    "gap",        "spacing", "the gap between a stack's children is its spacing" },
			{ "scroll",   "gap",        "spacing", nullptr },
			{ "select",   "options",    nullptr,   "choices are <option> children, or a table named by `of`" },
			{ "",         "hidden",     nullptr,   "a widget is hidden with visible={false}" },
			{ "",         "disabled",   nullptr,   "nothing takes a disabled flag; visible={false} takes a widget away" },
			{ "",         "size",       nullptr,   "set width and height" },
			{ "",         "margin",     nullptr,   "a widget has no margin: space comes from its container's spacing and padding" },
			{ "",         "className",  nullptr,   "there are no classes: look comes from a theme, chosen with variant" },
			{ "",         "class",      nullptr,   "there are no classes: look comes from a theme, chosen with variant" },
			{ "",         "style",      nullptr,   "there is no inline style: look comes from a theme, chosen with variant" },
			{ "",         "color",      nullptr,   "colours come from the theme: choose them with variant" },
			{ "",         "background", nullptr,   "colours come from the theme: choose them with variant" },
			{ "",         "backgroundColor", nullptr, "colours come from the theme: choose them with variant" },
			{ "",         "align",      "hAlign",  "alignment is named for its axis: hAlign or vAlign" },
			{ "",         "valign",     "vAlign",  nullptr },
			{ "",         "alignSelf",  "hAlignSelf", "or vAlignSelf, whichever axis you meant" },
			{ "",         "justify",    "spread",  "spread shares out room left over; hAlign and vAlign place" },
			{ "",         "key",        nullptr,   "a layout has no keys: id names a node" },
		};
		constexpr KnownGuess kElementGuesses[] = {
			{ "", "scrollview", "scroll",    "one element scrolls, down by default and sideways with horizontal" },
			{ "", "div",        "container", "or <panel> for one that draws a background" },
			{ "", "span",       "label",     nullptr },
			{ "", "text",       "label",     nullptr },
			{ "", "p",          "label",     nullptr },
			{ "", "input",      "textfield", nullptr },
			{ "", "textbox",    "textfield", nullptr },
			{ "", "img",        "image",     nullptr },
			{ "", "picture",    "image",     nullptr },
			{ "", "toggle",     "checkbox",  nullptr },
			{ "", "switch",     "checkbox",  nullptr },
			{ "", "dropdown",   "select",    nullptr },
			{ "", "combobox",   "select",    nullptr },
			{ "", "listview",   "list",      nullptr },
			{ "", "treeview",   "tree",      nullptr },
			{ "", "table",      "grid",      nullptr },
			{ "", "hstack",     "stack",     "with arrange=\"horizontal\"" },
			{ "", "vstack",     "stack",     "with arrange=\"vertical\"" },
			{ "", "row",        "stack",     "with arrange=\"horizontal\"" },
			{ "", "popover",    "popup",     nullptr },
			{ "", "tooltip",    "popup",     "with blocking={false}" },
		};

		const KnownGuess* knownGuess(const KnownGuess* table, size_t count, std::string_view element,
		                             std::string_view name) {
			for (size_t i = 0; i < count; ++i) {
				if (name != table[i].name) continue;
				if (table[i].element[0] == '\0' || element == table[i].element) return &table[i];
			}
			return nullptr;
		}

		// What to say about a property its element does not take.
		std::string unknownProperty(const std::string& type, const std::string& id,
		                            const std::string& key, const WidgetDesc& widget) {
			std::string message = "<" + type + "> '" + id + "' has no property '" + key + "'";

			// The table first: it knows what was meant, where a distance only knows what looks alike.
			if (const KnownGuess* guess = knownGuess(kPropertyGuesses, std::size(kPropertyGuesses), type, key)) {
				// A suggestion this element would itself refuse is not one.
				const bool usable = guess->instead && findProp(widget, guess->instead);
				if (usable) message += "; did you mean '" + std::string(guess->instead) + "'?";
				if (guess->note) message += std::string(usable ? " (" : "; ") + guess->note + (usable ? ")" : "");
				if (usable || guess->note) return message;
			}

			// Its own properties before the common ones, so that on a tie the one that belongs to
			// this element is the one named.
			std::string best;
			size_t bestDistance = SIZE_MAX;
			const auto consider = [&](const PropDesc& prop) {
				const size_t distance = nameDistance(key, prop.name);
				if (distance < bestDistance) { bestDistance = distance; best = prop.name; }
			};
			for (size_t i = 0; i < widget.propCount; ++i) consider(widget.props[i]);
			size_t commonCount = 0;
			const PropDesc* common = commonProps(commonCount);
			for (size_t i = 0; i < commonCount; ++i) consider(common[i]);
			if (!best.empty() && bestDistance <= nameTolerance(key.size())) {
				return message + "; did you mean '" + best + "'?";
			}

			// Nothing close: say what it does take, the properties first and the events after.
			std::string valid;
			size_t listed = 0;
			for (const bool events : { false, true }) {
				for (size_t i = 0; i < widget.propCount && listed < 6; ++i) {
					if ((widget.props[i].type == PropType::Event) != events) continue;
					valid += (listed++ ? ", " : "") + std::string(widget.props[i].name);
				}
			}
			if (listed == 0) valid = "visible, width, height";
			return message + "; it takes " + valid + ", and the common ones. `rda elements " + type +
			       "` lists them all";
		}

		// What to say about an element nothing builds.
		std::string unknownElement(const std::string& type, const std::string& id) {
			std::string message = "<" + type + "> '" + id + "' is not an element";
			if (const KnownGuess* guess = knownGuess(kElementGuesses, std::size(kElementGuesses), "", type)) {
				message += "; did you mean <" + std::string(guess->instead) + ">?";
				if (guess->note) message += std::string(" (") + guess->note + ")";
				return message;
			}
			size_t count = 0;
			const WidgetDesc* widgets = widgetSchema(count);
			std::string best;
			size_t bestDistance = SIZE_MAX;
			for (size_t i = 0; i < count; ++i) {
				const size_t distance = nameDistance(type, widgets[i].name);
				if (distance < bestDistance) { bestDistance = distance; best = widgets[i].name; }
			}
			if (!best.empty() && bestDistance <= nameTolerance(type.size())) {
				return message + "; did you mean <" + best + ">?";
			}
			return message + "; `rda elements` lists the ones there are";
		}

		// A function prop is a binding. Its source is recovered from the function itself,
		// parsed, and compiled; anything the grammar refuses fails the whole layout rather
		// than being dropped, because a binding that silently does not exist is the one
		// failure this design cannot afford.
		bool readBinding(JSContext* ctx, JSValueConst value, BlueprintBuilder& builder,
		                 uint32_t node, const std::string& key, const std::string& nodeId,
		                 std::string& error, const std::string& rowParam,
		                 const DeclaredMap& declared, ScriptPlanner& planner) {
			const char* raw = JS_ToCString(ctx, value);
			const std::string source = raw ? raw : "";
			if (raw) JS_FreeCString(ctx, raw);

			if (source.empty()) {
				error = "cannot read the source of '" + key + "' on '" + nodeId + "'";
				return false;
			}

			const bool isEvent = namesAnEvent(key);
			// The grammar first: what it accepts runs in the VM with its dependencies known
			// now, and a name followed by `(` may be one of the layout's own functions.
			const CallableName callable = [&planner](const std::string& name) {
				return planner.isFunction(name);
			};
			const ParseResult parsed = parseBinding(source, isEvent, rowParam, callable);
			Program program;
			if (parsed.ok) {
				if (!parsed.calls.empty()) {
					if (!options().allowRuntimeCode) {
						error = "in '" + key + "' on '" + nodeId + "': this calls " + parsed.calls.front() +
						        "(), which runs at run time, and this build was asked for no "
						        "run-time code (--no-runtime-code)";
						return false;
					}
					std::string why;
					if (!planner.noteCalls(source, rowParam, parsed.calls, why)) {
						error = "in '" + key + "' on '" + nodeId + "': " + why + "\n  " + source;
						return false;
					}
				}
				program = parsed.program;
			} else {
				// What the grammar does not cover is JavaScript instead, unless it is wrong
				// rather than merely bigger -- a name that is nothing, a write in a value
				// binding -- which is still refused, here, with the reason.
				std::string slot, why;
				if (!planner.addBinding(source, rowParam, isEvent, parsed.error, slot, why)) {
					error = "in '" + key + "' on '" + nodeId + "': " + why +
					        "\n  " + (options().allowRuntimeCode ? source : pointAt(source, parsed.position));
					return false;
				}
				program.code = { { Op::RunScript, 0 }, { Op::Halt, 0 } };
				program.signals = { slot };
			}

			// A name the layout declared for itself is stored scoped, so two layouts
			// may each have an `open` without being the same signal. Commands, columns and
			// the layout's functions carry their own mark and are left alone.
			for (std::string& pooled : program.signals) {
				if (!pooled.empty() && (pooled.front() == '@' || pooled.front() == '#' ||
				                        pooled.front() == '$' || pooled.front() == '%')) continue;
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
		               const DeclaredMap& declared, ScriptPlanner& planner) {
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
				if (key == "row" && (type == "list" || type == "tree")) continue;
				// Written by this compiler, not by the author; never a property of anything.
				const bool compilerOwned = key.rfind("!signal ", 0) == 0;

				// Against the schema, once the structural keys are out of the way. A
				// component's own props are not here: it was called, and what it returned is
				// the element being read.
				if (!compilerOwned && gNaming) {
					const WidgetDesc* widget = findWidget(type);
					if (widget && !findProp(*widget, key)) {
						gNaming->push_back(unknownProperty(type, nodeId, key, *widget));
					}
				}

				// A widget has no opening of its own: it is simply there or not. Refused here
				// because the loader has nothing to attach it to, and would drop what the
				// author wrote with a line in the log. A <popup>'s onClose is its own event.
				if (isLayoutEvent(key) && node != 0) {
					const WidgetDesc* widget = findWidget(type);
					const PropDesc* prop = widget ? findProp(*widget, key) : nullptr;
					const bool own = prop && prop >= widget->props && prop < widget->props + widget->propCount;
					if (!own) {
						error = "'" + key + "' on '" + nodeId + "' belongs on the layout's root element: "
						        "a widget has no opening of its own, because it is simply there or not. "
						        "To react to a value changing, use `watch` in state.ts";
						ok = false;
						break;
					}
				}

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
					const WidgetDesc* widget = findWidget(type);
					const PropDesc* prop = widget ? findProp(*widget, key) : nullptr;
					if (prop && prop->constant) {
						error = "'" + key + "' on '" + nodeId + "' takes a constant, not a binding: <" +
						        type + "> reads it once, when it is built";
						ok = false;
					} else {
						ok = readBinding(ctx, value, builder, node, key, nodeId, error, rowParam,
						                 declared, planner);
					}
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
		bool expandComponents(JSContext* ctx, JSValue& node, const Origins& origins, std::string& error) {
			// A component may return another component. Looping rather than recursing
			// keeps that flat, and the cap turns one that returns itself into a failed
			// build rather than a hung one.
			for (int depth = 0; depth < 64; ++depth) {
				// Every node comes through here, and so does what a component returned.
				normaliseElement(ctx, node);
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
					      + thrownBy(ctx, origins);
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
		             const std::vector<DeclaredSignal>& declaredSignals,
		             ScriptPlanner& planner, const Origins& origins) {
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
				if (!expandComponents(ctx, pending.value, origins, error)) {
					JS_FreeValue(ctx, pending.value);
					ok = false;
					break;
				}

				JSValue typeValue = JS_GetPropertyStr(ctx, pending.value, "type");
				const char* typeText = JS_IsString(typeValue) ? JS_ToCString(ctx, typeValue) : nullptr;
				if (!typeText) {
					if (JS_IsString(pending.value)) {
						const char* raw = JS_ToCString(ctx, pending.value);
						std::string text = raw ? raw : "";
						if (raw) JS_FreeCString(ctx, raw);
						const size_t first = text.find_first_not_of(" \t\r\n");
						const std::string parent = pending.parent == kNoParent
							? std::string("the layout") : "'" + paths[pending.parent] + "'";
						error = first == std::string::npos
							? "there are spaces beside a tag in " + parent + ", which JSX keeps as text, and "
							  "a layout has no text between tags. A comment on the same line as a tag "
							  "leaves them behind: put it on a line of its own."
							: "\"" + text.substr(first, 40) + "\" is text between the tags of " + parent +
							  ", and a layout has no text there: text is a property, text=\"...\". A // "
							  "comment there is text too: write {/* ... */} on a line of its own.";
					} else {
						error = "a node has no element name. An element is one of the built-in ones, "
						        "like <panel> or { type: \"panel\" }, or a function that returns one; "
						        "this is neither -- a fragment <>...</>, perhaps, which has no "
						        "node of its own to become.";
					}
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
				if ((type == "list" || type == "tree") && JS_IsObject(props)) {
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
								// With what it threw, which used to be left in the context
								// unread -- so the error said that it threw and not why.
								error = "the row template on '" + id + "' threw while it "
								        "was being read: " + thrownBy(ctx, origins);
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

				if (gNaming && !findWidget(type)) gNaming->push_back(unknownElement(type, id));

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
				               declared, planner)) {
					JS_FreeValue(ctx, props);
					JS_FreeValue(ctx, pending.value);
					// Built above and not queued yet, so nothing else will free it -- and a
					// runtime torn down with it still alive asserts instead of reporting
					// the error.
					JS_FreeValue(ctx, rowTemplate);
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

		bool evaluate(const std::string& bundle, const std::string& sourceMap, const std::string& name,
		              BlueprintBuilder& builder, CompileResult& compiled, std::string& error) {
			const std::string& propsJson = options().propsJson;
			JsContext engine(JsContext::Options{ kCompileJsStack });
			if (!engine) { error = engine.failure(); return false; }
			JSContext* ctx = engine.get();

			bool ok = false;
			const NamingScope naming(&compiled.nameWarnings);
			JSValue global = JS_GetGlobalObject(ctx);
			JS_SetPropertyStr(ctx, global, "h", JS_NewCFunction(ctx, js_h, "h", 3));

			// The helpers first, so the module can call them as it is evaluated.
			std::string wrapped;
			if (!wrapBundle(bundle, wrapped, error)) {
				JS_FreeValue(ctx, global);
				return false;
			}
			const std::string helpers = std::string(colourPrelude()) + formatPrelude() +
				buildTimerPrelude() + layoutPrelude();
			const Origins origins{ sourceMap, name };
			JSValue result = evaluateHelpers(ctx, helpers, error)
				? JS_Eval(ctx, wrapped.c_str(), wrapped.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL)
				: JS_EXCEPTION;
			if (!error.empty()) {
				// The helpers did not evaluate, which is the engine's mistake; said as it was.
			} else if (JS_IsException(result)) {
				error = thrownBy(ctx, origins);
			} else {
				ScriptPlanner planner(ctx, bundle, sourceMap, name, options().allowRuntimeCode);
				JSValue exports = JS_GetPropertyStr(ctx, global, "__rdaModule");
				JSValue builderFn = JS_GetPropertyStr(ctx, exports, "default");

				// Collected before the tree is walked, because the bindings in it name
				// these signals the way the author wrote them and have to be rewritten.
				const std::vector<DeclaredSignal> declaredSignals =
					collectSignals(ctx, global, name, error);
				DeclaredMap declared;
				for (const DeclaredSignal& one : declaredSignals) {
					if (!one.global) declared.emplace(one.authored, one.name);
				}

				if (!JS_IsFunction(ctx, builderFn) && (!JS_IsObject(builderFn) || JS_IsArray(builderFn))) {
					error = "the layout has no default export, or it is neither a function nor an "
					        "element. A layout file exports the function that builds it, "
					        "`export default function Name(props) { return <panel/> }`, or the "
					        "element itself, `export default { type: \"panel\" }`";
				} else {
					JSValue props = JS_ParseJSON(ctx, propsJson.c_str(), propsJson.size(), "<props>");
					if (JS_IsException(props)) {
						JS_FreeValue(ctx, props);
						props = JS_NewObject(ctx);
					}
					JSValue tree = JS_IsFunction(ctx, builderFn)
						? JS_Call(ctx, builderFn, JS_UNDEFINED, 1, &props)
						: JS_DupValue(ctx, builderFn);
					if (JS_IsException(tree)) {
						error = thrownBy(ctx, origins);
					} else if (!JS_IsObject(tree)) {
						error = "the builder returned nothing to lay out";
					} else {
						ok = error.empty() &&
						     flatten(ctx, tree, builder, error, declared, declaredSignals, planner, origins);
						// After the tree, because the tree is what said which functions are
						// reached: the run-time module, written and compiled.
						if (ok && planner.needed()) {
							BlueprintScript script;
							ok = planner.emit(declared, script, error);
							if (ok) {
								compiled.scriptBytes = script.code.size();
								builder.setScript(std::move(script));
							}
						}
						compiled.warnings = planner.warnings();
						compiled.scriptBindings = planner.bindingCount();
						compiled.scriptCalls = planner.callCount();
					}
					JS_FreeValue(ctx, tree);
					JS_FreeValue(ctx, props);
				}
				JS_FreeValue(ctx, builderFn);
				JS_FreeValue(ctx, exports);
			}

			JS_FreeValue(ctx, result);
			JS_FreeValue(ctx, global);
			return ok;
		}
	}

	std::string blueprintPathFor(const std::string& sourcePath) {
		std::filesystem::path path(sourcePath);
		path.replace_extension(".rdab");
		return path.string();
	}

	static CompileResult compileOnThisThread(const std::string& sourcePath, const CompileOptions& options);

	CompileResult compileFile(const std::string& sourcePath, const CompileOptions& options) {
		CompileResult result;
		const bool ran = onLargeStack([&] { result = compileOnThisThread(sourcePath, options); });
		if (!ran) result = compileOnThisThread(sourcePath, options);
		return result;
	}

	static CompileResult compileOnThisThread(const std::string& sourcePath, const CompileOptions& options) {
		CompileResult result;
		const OptionsScope scope(options);

		const Clock::time_point transformStart = Clock::now();
		std::string js, sourceMap;
		if (!bundleLayout(sourcePath, options.esbuildPath, js, sourceMap, result.error))
			return result;
		result.transformMs = millisSince(transformStart);

		const Clock::time_point evaluateStart = Clock::now();
		BlueprintBuilder builder;
		if (!evaluate(js, sourceMap, sourcePath, builder, result, result.error)) return result;
		result.evaluateMs = millisSince(evaluateStart);
		if (options.strict && !result.nameWarnings.empty()) {
			// Every one, not the first: a build that fails on a typo and then on the next
			// one is a build run five times to learn what one run knew.
			for (const std::string& one : result.nameWarnings) {
				result.error += (result.error.empty() ? "" : "\n") + one;
			}
			result.error += "\n(--strict: without it these are warnings, and the layout builds)";
			return result;
		}
		for (const std::string& warning : result.warnings) RDA_LOG_WARNING(sourcePath << ": " << warning);

		result.bytes     = builder.finish();
		result.nodeCount = builder.nodeCount();
		result.bindingCount = builder.bindingCount();
		result.ok        = true;
		return result;
	}

	static StateCodeResult compileStateOnThisThread(const std::string& statePath,
	                                                const CompileOptions& options) {
		StateCodeResult result;
		const OptionsScope scope(options);
		std::string& error = result.error;

		std::string js, sourceMap;
		if (!bundleLayout(statePath, options.esbuildPath, js, sourceMap, error)) return result;
		std::unordered_map<std::string, std::string> exports;
		std::string wrapped;
		if (!wrapBundle(js, wrapped, exports, error)) return result;

		JsContext engine(JsContext::Options{ kCompileJsStack });
		if (!engine) { error = engine.failure(); return result; }
		JSContext* ctx = engine.get();

		bool ok = evaluateHelpers(ctx, std::string(colourPrelude()) + formatPrelude() + buildTimerPrelude() +
		                          statePrelude(), error);
		if (ok) {
			JSValue evaluated = JS_Eval(ctx, wrapped.c_str(), wrapped.size(), statePath.c_str(), JS_EVAL_TYPE_GLOBAL);
			ok = !JS_IsException(evaluated);
			if (!ok) error = thrownBy(ctx, Origins{ sourceMap, statePath });
			JS_FreeValue(ctx, evaluated);
		}

		// The functions a declaration names: `compute` and `watch` on a field of state,
		// `run` on a command. Their source is what toString() gives, which is the bundle's own text --
		// so it is found there again for its lines, as a binding's is.
		struct Found { std::string name, source; };
		std::vector<Found> computed, watches, actions;
		auto collect = [&](const char* exportName, const char* key, std::vector<Found>& into) {
			JSValue global = JS_GetGlobalObject(ctx);
			JSValue module = JS_GetPropertyStr(ctx, global, "__rdaModule");
			JSValue table = JS_GetPropertyStr(ctx, module, "exports");
			JSValue object = JS_GetPropertyStr(ctx, table, exportName);
			JSPropertyEnum* names = nullptr;
			uint32_t count = 0;
			if (JS_IsObject(object) &&
			    JS_GetOwnPropertyNames(ctx, &names, &count, object, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
				for (uint32_t i = 0; i < count; ++i) {
					JSValue entry = JS_GetProperty(ctx, object, names[i].atom);
					JSValue fn = JS_IsObject(entry) ? JS_GetPropertyStr(ctx, entry, key) : JS_UNDEFINED;
					if (JS_IsFunction(ctx, fn)) {
						const char* name = JS_AtomToCString(ctx, names[i].atom);
						const char* text = JS_ToCString(ctx, fn);
						if (name && text) into.push_back(Found{ name, text });
						if (text) JS_FreeCString(ctx, text);
						if (name) JS_FreeCString(ctx, name);
					}
					JS_FreeValue(ctx, fn);
					JS_FreeValue(ctx, entry);
				}
				JS_FreePropertyEnum(ctx, names, count);
			}
			JS_FreeValue(ctx, object);
			JS_FreeValue(ctx, table);
			JS_FreeValue(ctx, module);
			JS_FreeValue(ctx, global);
		};
		if (ok) {
			collect("state", "compute", computed);
			collect("state", "watch", watches);
			collect("commands", "run", actions);
		}

		if (ok && (!computed.empty() || !watches.empty() || !actions.empty())) {
			if (!options.allowRuntimeCode) {
				error = !computed.empty() ? "state." + computed.front().name + " is computed by state.ts"
				      : !watches.empty()  ? "state." + watches.front().name + " is watched by state.ts"
				      :                     "commands." + actions.front().name + " is answered by state.ts";
				error += ", which is code that runs -- and this build was asked for no run-time code "
				         "(--no-runtime-code)";
				ok = false;
			}
			ScriptPlanner planner(ctx, js, sourceMap, statePath, options.allowRuntimeCode);
			// The declarations are top-level names of the bundle; code that names one means
			// the live one. Under whatever name esbuild gave it.
			std::unordered_map<std::string, std::string> aliases;
			for (const char* live : { "state", "tables", "commands" }) {
				const auto found = exports.find(live);
				if (found != exports.end()) aliases[found->second] = live;
			}
			if (ok) ok = planner.setStateAliases(aliases, error);
			for (size_t i = 0; ok && i < computed.size(); ++i) ok = planner.addComputed(computed[i].name, computed[i].source, error);
			for (size_t i = 0; ok && i < watches.size(); ++i) ok = planner.addWatch(watches[i].name, watches[i].source, error);
			for (size_t i = 0; ok && i < actions.size(); ++i) ok = planner.addAction(actions[i].name, actions[i].source, error);
			BlueprintScript script;
			if (ok) ok = planner.emit({}, script, error);
			if (ok) {
				result.bytes = writeScriptSection(script);
				result.computedCount = computed.size();
				result.actionCount = actions.size();
			}
			result.warnings = planner.warnings();
		}

		result.ok = ok;
		return result;
	}

	StateCodeResult compileStateCode(const std::string& statePath, const CompileOptions& options) {
		StateCodeResult result;
		const bool ran = onLargeStack([&] { result = compileStateOnThisThread(statePath, options); });
		if (!ran) result = compileStateOnThisThread(statePath, options);
		for (const std::string& warning : result.warnings) RDA_LOG_WARNING(statePath << ": " << warning);
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
