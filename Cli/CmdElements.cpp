// `rda elements`: what the widget schema says, readable without opening a generated file.
//
// People ended up searching res/layouts/rda.d.ts for the values hAlign takes, because it was
// the only place those were written down where they could find them. The schema is one table
// in the engine and the .d.ts, the loader and the build's name check all read it, so this
// reads it too and prints the part somebody is looking for.
#include "Commands.h"

#include <Layout/WidgetSchema.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace Cli {

	namespace {

		using RDA::Layout::PropDesc;
		using RDA::Layout::PropType;
		using RDA::Layout::WidgetDesc;

		bool isEvent(const PropDesc& prop) { return prop.type == PropType::Event; }

		// Whether a binding can drive it: everything but an event, which is a handler, and a
		// constant, which is read once when the widget is built.
		bool bindable(const PropDesc& prop) { return !isEvent(prop) && !prop.constant; }

		std::string typeName(const PropDesc& prop) {
			const std::string values = prop.values ? prop.values : "";
			switch (prop.type) {
				case PropType::String:  return "string";
				case PropType::Number:  return "number";
				case PropType::Bool:    return "boolean";
				case PropType::Size:    return "size";
				case PropType::Align:   return "align: " + values + ", each with an optional +n or -n";
				case PropType::Variant: return "variant of the theme's '" + values + "'";
				case PropType::Enum:    return "one of " + values;
				case PropType::Chord:   return "chord";
				case PropType::Event:   return values.empty() ? "event" : "event with a " + values;
			}
			return "?";
		}

		// What a Size may be, once, rather than in every row it appears in.
		const char* kSizeNote =
			"size: a number, \"content\", \"fill\" or \"fill:2\" (a share of what is left), "
			"any of them with an offset such as \"fill-20\"";

		// Words broken to a width, each line behind `indent`.
		void wrapped(const std::string& text, const std::string& indent, size_t width = 78) {
			std::string line = indent;
			size_t at = 0;
			bool first = true;
			while (at < text.size()) {
				size_t end = text.find(' ', at);
				if (end == std::string::npos) end = text.size();
				const std::string word = text.substr(at, end - at);
				at = end + 1;
				if (word.empty()) continue;
				if (!first && line.size() + 1 + word.size() > width) {
					std::printf("%s\n", line.c_str());
					line = indent;
					first = true;
				}
				line += (first ? "" : " ") + word;
				first = false;
			}
			if (!first) std::printf("%s\n", line.c_str());
		}

		void printProperty(const PropDesc& prop) {
			const std::string how = isEvent(prop) ? "" : bindable(prop) ? "  (can be bound)" : "  (constant: not bindable)";
			std::printf("  %s  [%s]%s\n", prop.name, typeName(prop).c_str(), how.c_str());
			wrapped(prop.doc, "      ");
		}

		void printSection(const char* title, const PropDesc* props, size_t count, bool events) {
			bool any = false;
			for (size_t i = 0; i < count; ++i) {
				if (isEvent(props[i]) != events) continue;
				if (!any) std::printf("\n%s\n", title);
				any = true;
				printProperty(props[i]);
			}
		}

		// ---- JSON ---------------------------------------------------------------------------

		std::string quoted(const std::string& text) {
			std::string out = "\"";
			for (const char c : text) {
				switch (c) {
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n"; break;
					case '\r': out += "\\r"; break;
					case '\t': out += "\\t"; break;
					default:
						if (static_cast<unsigned char>(c) < 0x20) {
							char escape[8];
							std::snprintf(escape, sizeof(escape), "\\u%04x", c);
							out += escape;
						} else {
							out += c;
						}
				}
			}
			return out + "\"";
		}

		const char* jsonType(PropType type) {
			switch (type) {
				case PropType::String:  return "string";
				case PropType::Number:  return "number";
				case PropType::Bool:    return "boolean";
				case PropType::Size:    return "size";
				case PropType::Align:   return "align";
				case PropType::Variant: return "variant";
				case PropType::Enum:    return "enum";
				case PropType::Chord:   return "chord";
				case PropType::Event:   return "event";
			}
			return "unknown";
		}

		// `values` means a different thing for each type, and a consumer wants the meaning
		// rather than the column: the words an enum takes, the theme element a variant reads,
		// the type an event hands its handler.
		void printJsonProperty(const PropDesc& prop, bool common, bool last) {
			std::printf("    { \"name\": %s, \"type\": \"%s\"", quoted(prop.name).c_str(), jsonType(prop.type));
			if (prop.values) {
				const char* key = prop.type == PropType::Variant ? "themeElement"
				                : prop.type == PropType::Event   ? "argument" : "values";
				std::printf(", \"%s\": %s", key, quoted(prop.values).c_str());
			}
			std::printf(", \"bindable\": %s, \"common\": %s, \"doc\": %s }%s\n",
			            bindable(prop) ? "true" : "false", common ? "true" : "false",
			            quoted(prop.doc).c_str(), last ? "" : ",");
		}

		void printJsonList(const char* key, const WidgetDesc& widget, bool events) {
			size_t commonCount = 0;
			const PropDesc* common = RDA::Layout::commonProps(commonCount);
			std::vector<std::pair<const PropDesc*, bool>> rows;
			for (size_t i = 0; i < widget.propCount; ++i) {
				if (isEvent(widget.props[i]) == events) rows.emplace_back(&widget.props[i], false);
			}
			for (size_t i = 0; i < commonCount; ++i) {
				if (isEvent(common[i]) == events) rows.emplace_back(&common[i], true);
			}
			std::printf("  \"%s\": [\n", key);
			for (size_t i = 0; i < rows.size(); ++i) {
				printJsonProperty(*rows[i].first, rows[i].second, i + 1 == rows.size());
			}
			std::printf("  ]");
		}

		int listAll(bool json) {
			size_t count = 0;
			const WidgetDesc* widgets = RDA::Layout::widgetSchema(count);
			if (json) {
				std::printf("[\n");
				for (size_t i = 0; i < count; ++i) {
					std::printf("  { \"name\": %s, \"doc\": %s }%s\n", quoted(widgets[i].name).c_str(),
					            quoted(widgets[i].doc).c_str(), i + 1 == count ? "" : ",");
				}
				std::printf("]\n");
				return 0;
			}
			size_t widest = 0;
			for (size_t i = 0; i < count; ++i) widest = (std::max)(widest, std::string(widgets[i].name).size());
			std::printf("%zu elements. `rda elements <name>` lists the properties and events of one.\n\n", count);
			for (size_t i = 0; i < count; ++i) {
				std::printf("  %-*s  %s\n", static_cast<int>(widest), widgets[i].name, widgets[i].doc);
			}
			return 0;
		}

		int describe(const WidgetDesc& widget, bool json) {
			size_t commonCount = 0;
			const PropDesc* common = RDA::Layout::commonProps(commonCount);
			if (json) {
				std::printf("{\n  \"name\": %s,\n  \"doc\": %s,\n", quoted(widget.name).c_str(),
				            quoted(widget.doc).c_str());
				printJsonList("properties", widget, false);
				std::printf(",\n");
				printJsonList("events", widget, true);
				std::printf("\n}\n");
				return 0;
			}

			std::printf("<%s>\n", widget.name);
			wrapped(widget.doc, "  ");
			printSection("Properties", widget.props, widget.propCount, false);
			printSection("Events", widget.props, widget.propCount, true);
			printSection("Common properties, on every element", common, commonCount, false);
			printSection("Common events, on every element", common, commonCount, true);

			bool sized = false;
			for (size_t i = 0; i < widget.propCount; ++i) sized = sized || widget.props[i].type == PropType::Size;
			for (size_t i = 0; i < commonCount; ++i) sized = sized || common[i].type == PropType::Size;
			if (sized) std::printf("\n%s\n", kSizeNote);
			return 0;
		}
	}

	int cmdElements(const Args& args) {
		bool json = false;
		std::string name;
		for (size_t i = 1; i < args.size(); ++i) {
			if (args[i] == "--json") json = true;
			else if (!args[i].empty() && args[i][0] == '-') {
				std::printf("rda: unknown option %s\n", args[i].c_str());
				return 1;
			}
			else name = args[i];
		}
		if (name.empty()) return listAll(json);

		// `<checkbox>` as it is written in a layout is as good as `checkbox`.
		if (name.size() > 2 && name.front() == '<' && name.back() == '>') name = name.substr(1, name.size() - 2);
		if (const WidgetDesc* widget = RDA::Layout::findWidget(name)) return describe(*widget, json);

		size_t count = 0;
		const WidgetDesc* widgets = RDA::Layout::widgetSchema(count);
		std::string all;
		for (size_t i = 0; i < count; ++i) all += (i ? ", " : "") + std::string(widgets[i].name);
		std::printf("rda: there is no element called `%s`. The elements are: %s\n", name.c_str(), all.c_str());
		return 1;
	}
}
