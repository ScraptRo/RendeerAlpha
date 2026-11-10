// `rda widgets`: everything about the widgets, without opening a browser or a generated file.
//
// People ended up searching res/layouts/rda.d.ts for the values hAlign takes, because it was
// the only place those were written down where they could find them. The schema is one table
// in the engine and the .d.ts, the loader and the build's name check all read it, so this
// reads it too. What it does not say is how a widget behaves, and that is written once as well:
// the documentation's pages are embedded here, and a widget's own section is what is printed.
#include "Command.h"
#include "Starters.h"

#include <DocChoices.h>
#include <DocContainers.h>
#include <DocDocking.h>
#include <DocList.h>
#include <DocStream.h>
#include <DocText.h>
#include <DocViewport.h>
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

		// ---- what the documentation says ------------------------------------------------------

		struct Page {
			const unsigned char* text;
			size_t               size;
			const char*          path;
		};
		const Page kPages[] = {
			{ kDocContainers, kDocContainersSize, "docs/frontend/elements/containers.md" },
			{ kDocText,       kDocTextSize,       "docs/frontend/elements/text-and-input.md" },
			{ kDocChoices,    kDocChoicesSize,    "docs/frontend/elements/choices-and-pages.md" },
			{ kDocList,       kDocListSize,       "docs/frontend/elements/list.md" },
			{ kDocDocking,    kDocDockingSize,    "docs/frontend/elements/docking.md" },
			{ kDocStream,     kDocStreamSize,     "docs/frontend/elements/stream.md" },
			{ kDocViewport,   kDocViewportSize,   "docs/frontend/elements/viewport.md" },
		};

		std::vector<std::string> linesOf(const Page& page) {
			std::vector<std::string> out;
			std::string line;
			for (size_t i = 0; i < page.size; ++i) {
				const char c = static_cast<char>(page.text[i]);
				if (c == '\r') continue;
				if (c == '\n') { out.push_back(line); line.clear(); continue; }
				line.push_back(c);
			}
			if (!line.empty()) out.push_back(line);
			return out;
		}

		// The lines under the heading that names this widget, up to the next heading of that
		// level, and the page they are on. Empty for a widget with no section of its own.
		std::vector<std::string> sectionFor(const std::string& name, const char*& where) {
			const std::string mark = "`<" + name + ">`";
			for (const Page& page : kPages) {
				std::vector<std::string> section;
				bool inside = false;
				for (const std::string& line : linesOf(page)) {
					if (line.rfind("## ", 0) == 0) {
						if (inside) break;
						inside = line.find(mark) != std::string::npos;
						continue;
					}
					if (inside) section.push_back(line);
				}
				if (inside) {
					where = page.path;
					return section;
				}
			}
			return {};
		}

		// Markdown as words: the marks that mean something to a browser and only get in the way
		// of reading in a terminal.
		std::string plain(const std::string& line) {
			std::string out;
			for (size_t i = 0; i < line.size(); ++i) {
				const char c = line[i];
				if (c == '`') continue;
				if (c == '*' && i + 1 < line.size() && line[i + 1] == '*') { ++i; continue; }
				if (c == '[') {
					const size_t close = line.find("](", i);
					const size_t end = close == std::string::npos ? close : line.find(')', close);
					if (end != std::string::npos) {
						out += line.substr(i + 1, close - i - 1);
						i = end;
						continue;
					}
				}
				out.push_back(c);
			}
			return out;
		}

		std::vector<std::string> cellsOf(const std::string& row) {
			std::vector<std::string> cells;
			size_t at = 1;
			while (at < row.size()) {
				size_t bar = row.find('|', at);
				if (bar == std::string::npos) bar = row.size();
				std::string cell = row.substr(at, bar - at);
				while (!cell.empty() && cell.front() == ' ') cell.erase(cell.begin());
				while (!cell.empty() && cell.back() == ' ') cell.pop_back();
				cells.push_back(plain(cell));
				at = bar + 1;
			}
			return cells;
		}

		void printTable(const std::vector<std::vector<std::string>>& rows) {
			size_t widest = 0;
			for (const auto& row : rows) {
				if (row.size() >= 2) widest = (std::max)(widest, row[0].size());
			}
			for (const auto& row : rows) {
				if (row.size() < 2) continue;
				const std::string indent = "    " + std::string(widest + 2, ' ');
				std::printf("    %-*s  ", static_cast<int>(widest), row[0].c_str());
				// The first line sits beside the name, so it is cut from what follows.
				std::string text = row[1];
				const size_t room = indent.size() < 76 ? 78 - indent.size() : 24;
				size_t cut = text.size() <= room ? text.size() : text.rfind(' ', room);
				if (cut == std::string::npos) cut = text.size();
				std::printf("%s\n", text.substr(0, cut).c_str());
				if (cut < text.size()) wrapped(text.substr(cut + 1), indent);
			}
			std::printf("\n");
		}

		void printSection(const std::vector<std::string>& lines) {
			std::string paragraph;
			std::vector<std::vector<std::string>> table;
			bool code = false;
			const auto finish = [&] {
				if (!paragraph.empty()) {
					wrapped(paragraph, "  ");
					std::printf("\n");
					paragraph.clear();
				}
				if (!table.empty()) {
					printTable(table);
					table.clear();
				}
			};
			for (const std::string& line : lines) {
				if (line.rfind("```", 0) == 0) {
					finish();
					if (code) std::printf("\n");
					code = !code;
				} else if (code) {
					std::printf("      %s\n", line.c_str());
				} else if (line.empty()) {
					finish();
				} else if (line.rfind("### ", 0) == 0) {
					finish();
					std::printf("  %s\n\n", plain(line.substr(4)).c_str());
				} else if (line.front() == '|') {
					if (!paragraph.empty()) finish();
					const std::vector<std::string> cells = cellsOf(line);
					// `| | |` and `| --- | --- |` are how a table without a heading is written.
					const bool empty = cells.size() >= 2 && cells[0].empty();
					const bool rule = !cells.empty() && cells[0].rfind("---", 0) == 0;
					if (!empty && !rule) table.push_back(cells);
				} else {
					paragraph += (paragraph.empty() ? "" : " ") + plain(line);
				}
			}
			finish();
		}

		void printProperty(const PropDesc& prop) {
			const std::string how = isEvent(prop) ? "" : bindable(prop) ? "  (can be bound)" : "  (constant: not bindable)";
			std::printf("  %s  [%s]%s\n", prop.name, typeName(prop).c_str(), how.c_str());
			wrapped(prop.doc, "      ");
		}

		void printPropertySection(const char* title, const PropDesc* props, size_t count, bool events) {
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
			std::printf("%zu widgets. `rda widgets <name>` says all there is about one.\n\n", count);
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

			const char* page = nullptr;
			const std::vector<std::string> section = sectionFor(widget.name, page);
			if (!section.empty()) {
				std::printf("\n");
				printSection(section);
			}

			if (const Starter* starter = starterFor(widget.name)) {
				std::printf("%sExample\n", section.empty() ? "\n" : "");
				const std::string source = starterSource(*starter, widget.name);
				size_t at = 0;
				while (at < source.size()) {
					size_t end = source.find('\n', at);
					if (end == std::string::npos) end = source.size();
					std::printf("  %s\n", source.substr(at, end - at).c_str());
					at = end + 1;
				}
			}

			printPropertySection("Properties", widget.props, widget.propCount, false);
			printPropertySection("Events", widget.props, widget.propCount, true);

			// By name only: they are the same on every widget, and listing them in full here
			// would bury what is particular to this one.
			std::string names;
			for (size_t i = 0; i < commonCount; ++i) {
				if (!isEvent(common[i])) names += (names.empty() ? "" : ", ") + std::string(common[i].name);
			}
			std::printf("\nEvery widget also takes\n");
			wrapped(names + ". `rda widgets common` says what each one does.", "  ");
			if (page) std::printf("\nMore in %s\n", page);
			return 0;
		}

		int describeCommon() {
			size_t commonCount = 0;
			const PropDesc* common = RDA::Layout::commonProps(commonCount);
			std::printf("What every widget takes.\n");
			printPropertySection("Properties", common, commonCount, false);
			printPropertySection("Events", common, commonCount, true);
			std::printf("\n%s\n", kSizeNote);
			return 0;
		}
	}

	static int cmdWidgets(const Args& args) {
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
		if (name == "common" && !json) return describeCommon();

		// `<checkbox>` as it is written in a layout is as good as `checkbox`.
		if (name.size() > 2 && name.front() == '<' && name.back() == '>') name = name.substr(1, name.size() - 2);
		if (const WidgetDesc* widget = RDA::Layout::findWidget(name)) return describe(*widget, json);

		size_t count = 0;
		const WidgetDesc* widgets = RDA::Layout::widgetSchema(count);
		std::string all;
		for (size_t i = 0; i < count; ++i) all += (i ? ", " : "") + std::string(widgets[i].name);
		std::printf("rda: there is no widget called `%s`. The widgets are: %s\n", name.c_str(), all.c_str());
		return 1;
	}

	namespace {
		const Command kWidgets{ __FILE__, "widgets", Section::Learn, 0,
			"  rda widgets [<name>|common] [--json]\n"
			"                                     every widget a layout can use, or all about one\n",
			cmdWidgets };
		// What this was called first, and what the docs of a layout still call them.
		const Command kElements{ __FILE__, "elements", Section::Learn, 0, nullptr, cmdWidgets };
	}
}
