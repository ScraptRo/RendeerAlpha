// `rda add screen <name>`: a layout and the route that reaches it.
//
// A second screen is two edits that have to agree -- a file in res/layouts/ and an entry in
// res/state.ts -- and the second is the one people get wrong, because the first time it is
// not an entry but a whole declaration that changes what the backend does. So this makes
// both, and says what the backend has to change when it is the first.
//
// state.ts is the author's file, and it is edited as text: one entry inserted where it
// belongs, in the layout the file already has, and nothing else touched. Not rewritten from
// a parsed form, which would have been the end of every comment in it. When the file is not
// in a shape that can be edited that way, it is left alone and the lines to paste are
// printed -- a refusal that costs the person a paste, against a guess that costs them a
// file. And whatever is written is read back the way the build will read it, and put back
// as it was if the build would not accept it.
#include "Commands.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

namespace Cli {

	namespace {

		bool readFile(const fs::path& path, std::string& into) {
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			std::ostringstream text;
			text << in.rdbuf();
			into = text.str();
			return true;
		}

		bool writeFile(const fs::path& path, const std::string& text) {
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			return out && out.write(text.data(), static_cast<std::streamsize>(text.size()));
		}

		// A screen is a file name and a string in a signal, so what it is called is held to what
		// both can carry.
		bool validName(const std::string& name) {
			if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0]))) return false;
			return std::all_of(name.begin(), name.end(), [](char c) {
				return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
			});
		}

		bool isIdentifier(const std::string& name) {
			return std::all_of(name.begin(), name.end(), [](char c) {
				return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
			});
		}

		// A key as an object literal writes it: bare when it can be, quoted when not (my-screen).
		std::string keyFor(const std::string& name) {
			return isIdentifier(name) ? name : "\"" + name + "\"";
		}

		// my-screen -> MyScreen, for the function a layout exports.
		std::string componentName(const std::string& name) {
			std::string out;
			bool upper = true;
			for (const char c : name) {
				if (c == '-' || c == '_') { upper = true; continue; }
				out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
				upper = false;
			}
			return out;
		}

		// The file with every comment and every string's contents blanked to spaces, same
		// length. What is left is structure, so braces can be counted and a name looked for
		// without a `}` inside a string or a `routes` inside a comment being taken for code.
		std::string structureOf(const std::string& text) {
			std::string out = text;
			size_t i = 0;
			const size_t n = text.size();
			const auto blank = [&](size_t from, size_t to) {
				for (size_t k = from; k < to && k < n; ++k) if (out[k] != '\n' && out[k] != '\r') out[k] = ' ';
			};
			while (i < n) {
				const char c = text[i];
				if (c == '/' && i + 1 < n && text[i + 1] == '/') {
					size_t end = text.find('\n', i);
					if (end == std::string::npos) end = n;
					blank(i, end);
					i = end;
				} else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
					size_t end = text.find("*/", i + 2);
					end = end == std::string::npos ? n : end + 2;
					blank(i, end);
					i = end;
				} else if (c == '"' || c == '\'' || c == '`') {
					size_t k = i + 1;
					while (k < n && text[k] != c) k += (text[k] == '\\') ? 2 : 1;
					// The quotes stay, so `routes: "x"` still has a value; the inside goes.
					blank(i + 1, k);
					i = k + 1;
				} else {
					++i;
				}
			}
			return out;
		}

		std::string newlineOf(const std::string& text) {
			return text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
		}

		// Start of the line `at` is on, and the end of it (before its line break).
		size_t lineStart(const std::string& text, size_t at) {
			const size_t found = text.rfind('\n', at == 0 ? 0 : at - 1);
			return (at == 0 || found == std::string::npos) ? 0 : found + 1;
		}
		size_t lineEnd(const std::string& text, size_t at) {
			size_t end = text.find('\n', at);
			if (end == std::string::npos) return text.size();
			if (end > 0 && text[end - 1] == '\r') --end;
			return end;
		}

		struct Edited {
			bool        ok = false;
			std::string text;
			std::string why;      // when not ok: what about the file made it unsafe
		};

		Edited cannot(const std::string& why) {
			Edited edited;
			edited.why = why;
			return edited;
		}

		// A route added to `export const routes = { ... }` where it stands.
		Edited addToExisting(const std::string& text, const std::string& structure,
		                     const std::string& name, size_t openBrace) {
			// The matching brace, counted in the structure so that a string cannot throw it off.
			size_t close = openBrace;
			for (int depth = 0; close < structure.size(); ++close) {
				if (structure[close] == '{') ++depth;
				else if (structure[close] == '}' && --depth == 0) break;
			}
			if (close >= structure.size()) return cannot("its `routes` object is never closed");

			// The closing brace has to be alone on its line, or it is a one-line object and a
			// new line in the middle of it would be a reformatting the author did not ask for.
			for (size_t k = lineStart(structure, close); k < close; ++k) {
				if (structure[k] != ' ' && structure[k] != '\t') {
					return cannot("its `routes` object is written on one line");
				}
			}

			// What is in it: the first and last character that is code.
			size_t first = std::string::npos, last = std::string::npos;
			for (size_t k = openBrace + 1; k < close; ++k) {
				if (std::isspace(static_cast<unsigned char>(structure[k]))) continue;
				if (first == std::string::npos) first = k;
				last = k;
			}
			const std::string nl = newlineOf(text);
			std::string indent = "  ";
			std::string gap = " ";
			size_t insertAt = lineEnd(text, openBrace);
			bool needComma = false;

			if (first != std::string::npos) {
				if (lineStart(structure, first) == lineStart(structure, openBrace)) {
					return cannot("the first of its routes is on the same line as the opening brace");
				}
				const size_t firstLine = lineStart(text, first);
				indent = text.substr(firstLine, first - firstLine);
				if (indent.find_first_not_of(" \t") != std::string::npos) {
					return cannot("its first route is not the first thing on its line");
				}
				// Aligned the way the author aligned them, if they did: `home:      {`.
				static const std::regex entry(R"(^[ \t]*([A-Za-z_$"'][\w$"'-]*)[ \t]*:([ \t]+)\S)");
				const std::string firstText = text.substr(firstLine, lineEnd(text, first) - firstLine);
				std::smatch parts;
				if (std::regex_search(firstText, parts, entry) && parts[2].length() > 1) {
					const size_t valueColumn = parts[0].length() - 1;
					const std::string key = keyFor(name);
					const size_t used = indent.size() + key.size() + 1;
					gap = std::string(valueColumn > used ? valueColumn - used : 1, ' ');
				}
				insertAt = lineEnd(text, last);
				needComma = structure[last] != ',';
			}

			std::string key = keyFor(name);
			const std::string line = nl + indent + key + ":" + gap + "{ layout: \"" + name + "\" },";
			std::string edited = text;
			// The later position first, so the earlier one is still where it was counted.
			edited.insert(insertAt, line);
			if (needComma) edited.insert(last + 1, ",");
			Edited done;
			done.ok = true;
			done.text = edited;
			return done;
		}

		// No `routes` yet: one is declared at the end of the file, with an entry for each layout
		// already there, so that the screen the app has now is still the one it opens on.
		Edited addNewDeclaration(const std::string& text, const std::string& structure,
		                         const std::vector<std::string>& existing, const std::string& name) {
			if (std::regex_search(structure, std::regex(R"(\broutes\b)"))) {
				return cannot("it mentions `routes` in a form other than `export const routes = { ... }`");
			}
			std::vector<std::string> names = existing;
			names.push_back(name);

			size_t widest = 0;
			for (const std::string& one : names) widest = (std::max)(widest, keyFor(one).size());

			const std::string nl = newlineOf(text);
			std::string block;
			if (!text.empty() && text.back() != '\n') block += nl;
			block += nl;
			block += "// The screens, one showing at a time; state.route names it and the first opens." + nl;
			block += "// See docs/frontend/screens.md." + nl;
			block += "export const routes = {" + nl;
			for (const std::string& one : names) {
				const std::string key = keyFor(one);
				block += "  " + key + ":" + std::string(widest - key.size() + 1, ' ') +
				         "{ layout: \"" + one + "\" }," + nl;
			}
			block += "}" + nl;

			Edited done;
			done.ok = true;
			done.text = text + block;
			return done;
		}

		// ---- the layout ---------------------------------------------------------------------

		std::string layoutFor(const std::string& name, const std::string& nl) {
			const std::string title = componentName(name);
			std::string out;
			const auto line = [&](const std::string& one) { out += one + nl; };
			line("// The " + name + " screen. Reach it from another with `state.route = \"" + name + "\"`;");
			line("// `commands.back()` goes back to the one before.");
			line("export default function " + title + "() {");
			line("  return (");
			line("    <stack id=\"root\" arrange=\"vertical\" spacing={10} padding={16}>");
			line("      <label id=\"title\" variant=\"heading\" height=\"content\" text=\"" + title + "\" />");
			line("      <button id=\"back\" text=\"Back\" width={110} height=\"content\"");
			line("              onClick={() => commands.back()} />");
			line("    </stack>");
			line("  )");
			line("}");
			return out;
		}

		// ---- what the backend has to do ----------------------------------------------------

		void sayWhatTheBackendChanges(const Languages& found, const std::string& first,
		                              const std::string& name) {
			const bool all = !found.any();
			std::printf("\nrda: this project had no routes, so its backend opens one layout and has to open "
			            "all of them now. With routes the engine owns the screens, and `state.route` names "
			            "the one showing:\n");
			if (all || !found.python.empty()) {
				std::printf("  Python  from state import ROUTES\n"
				            "          rda.open_routes(ROUTES, \"res/layouts\", \"res/layouts\")   "
				            "instead of rda.load_interface(...)\n");
			}
			if (all || !found.node.empty()) {
				std::printf("  Node    import { ROUTES } from './state.mjs'\n"
				            "          rda.openRoutes(ROUTES, 'res/layouts', 'res/layouts')   "
				            "instead of rda.loadInterface(...)\n");
			}
			if (all || !found.csharp.empty()) {
				std::printf("  C#      Rda.OpenRoutes(Routes.All, \"res/layouts\", \"res/layouts\")   "
				            "instead of Rda.LoadInterface(...)\n");
			}
			if (all || !found.cpp.empty()) {
				std::printf("  C++     a RDA::Layout::Router opened with RDA::State::routes(), and "
				            "gRouter.update() once a frame   instead of LayoutHost::open (docs/backend/cpp.md)\n");
			}
			std::printf("It opens on `%s`, the first route. Anything that reached the one layout by name "
			            "now goes through `state.route = \"%s\"`; `commands.back()` and `commands.forward()` "
			            "come with the routes. See docs/frontend/screens.md.\n", first.c_str(), name.c_str());
		}

		struct AddOptions {
			std::string name;
			fs::path    project = ".";
			bool        build = true;
		};

		bool parseAddOptions(const Args& args, AddOptions& options) {
			size_t positional = 0;
			for (size_t i = 2; i < args.size(); ++i) {
				if (args[i] == "--no-build") options.build = false;
				else if (!args[i].empty() && args[i][0] == '-') {
					std::printf("rda: unknown option %s\n", args[i].c_str());
					return false;
				}
				else if (positional++ == 0) options.name = args[i];
				else options.project = args[i];
			}
			return true;
		}

		int addScreen(const Args& args) {
			AddOptions options;
			if (!parseAddOptions(args, options)) return 1;
			if (!validName(options.name)) {
				std::printf("rda: a screen's name is a file name and a route, so letters, digits, - and _, "
				            "starting with a letter (got `%s`)\n", options.name.c_str());
				return 1;
			}

			const fs::path res = options.project / "res";
			const fs::path layouts = res / "layouts";
			const fs::path layoutFile = layouts / (options.name + ".tsx");
			const fs::path stateFile = res / "state.ts";
			std::error_code ec;
			if (!fs::is_directory(res, ec)) {
				std::printf("rda: no res/ directory in %s -- a project keeps its declaration, theme and "
				            "layouts there\n", shown(options.project).c_str());
				return 1;
			}
			if (!fs::is_regular_file(stateFile, ec)) {
				std::printf("rda: no %s to declare the route in\n", shown(stateFile).c_str());
				return 1;
			}
			if (fs::exists(layoutFile, ec)) {
				std::printf("rda: %s already exists; nothing was changed\n", shown(layoutFile).c_str());
				return 1;
			}

			// What the declaration says now, read the way the build reads it.
			const RDA::Layout::StateSchema before = RDA::Layout::loadStateSchema(stateFile.string());
			if (!before.ok) {
				std::printf("rda: %s\n", before.error.c_str());
				return 1;
			}
			for (const RDA::Layout::StateRoute& route : before.routes) {
				if (route.name == options.name) {
					std::printf("rda: %s already has a route called `%s` (it shows `%s`); nothing was changed\n",
					            shown(stateFile).c_str(), route.name.c_str(), route.layout.c_str());
					return 1;
				}
			}

			std::string original;
			if (!readFile(stateFile, original)) {
				std::printf("rda: cannot read %s\n", shown(stateFile).c_str());
				return 1;
			}
			const std::string structure = structureOf(original);

			// The route first, as text, because whether it can be done decides whether anything
			// is: a screen with no way to reach it is the thing this command exists to avoid.
			const bool hadRoutes = !before.routes.empty();
			static const std::regex declaration(R"(export\s+const\s+routes\s*=\s*\{)");
			std::smatch declared;
			Edited edited;
			if (std::regex_search(structure, declared, declaration)) {
				edited = addToExisting(original, structure, options.name,
				                       static_cast<size_t>(declared.position(0) + declared.length(0) - 1));
			} else {
				// Every screen already there, so the one the app has now is not lost: the layouts
				// nothing else imports (the others are components), `main` first because it is
				// the one every project starts with and opens on.
				const std::vector<fs::path> found = filesIn(layouts, ".tsx");
				const std::set<fs::path> parts = importedByAnother(found);
				std::vector<std::string> existing;
				for (const fs::path& one : found) {
					if (!isImported(parts, one)) existing.push_back(one.stem().string());
				}
				const auto main = std::find(existing.begin(), existing.end(), "main");
				if (main != existing.end()) std::rotate(existing.begin(), main, main + 1);
				edited = addNewDeclaration(original, structure, existing, options.name);
			}

			const std::string nl = newlineOf(original);
			const std::string layoutText = layoutFor(options.name, nl);

			// The lines to paste, for a file this cannot edit.
			const auto printPaste = [&] {
				std::printf("rda: %s is not in a shape I can edit safely (%s), so I did not touch it. "
				            "Add this route to it yourself:\n\n", shown(stateFile).c_str(), edited.why.c_str());
				if (hadRoutes) {
					std::printf("  %s: { layout: \"%s\" },\n\n", keyFor(options.name).c_str(), options.name.c_str());
				} else {
					std::printf("  export const routes = {\n");
					for (const fs::path& one : filesIn(layouts, ".tsx")) {
						std::printf("    %s: { layout: \"%s\" },\n", keyFor(one.stem().string()).c_str(),
						            one.stem().string().c_str());
					}
					std::printf("    %s: { layout: \"%s\" },\n  }\n\n", keyFor(options.name).c_str(),
					            options.name.c_str());
				}
			};

			fs::create_directories(layouts, ec);
			if (!writeFile(layoutFile, layoutText)) {
				std::printf("rda: cannot write %s\n", shown(layoutFile).c_str());
				return 1;
			}
			std::printf("rda: %s  (a new screen)\n", shown(layoutFile).c_str());

			if (!edited.ok) {
				printPaste();
				return 1;
			}
			if (!writeFile(stateFile, edited.text)) {
				std::printf("rda: cannot write %s\n", shown(stateFile).c_str());
				return 1;
			}

			// Read back the way the build will, and put right if it would not take it.
			const RDA::Layout::StateSchema after = RDA::Layout::loadStateSchema(stateFile.string());
			bool routed = after.ok;
			if (routed) {
				routed = std::any_of(after.routes.begin(), after.routes.end(),
				                     [&](const RDA::Layout::StateRoute& one) { return one.name == options.name; });
			}
			if (!routed) {
				writeFile(stateFile, original);
				edited.why = after.ok ? "the edit did not produce the route" : after.error;
				printPaste();
				return 1;
			}
			std::printf("rda: %s  (route `%s`%s)\n", shown(stateFile).c_str(), options.name.c_str(),
			            hadRoutes ? "" : ", and a `routes` declaration holding the screens already here");

			if (!hadRoutes) {
				sayWhatTheBackendChanges(detectLanguages(options.project), after.routes.front().name, options.name);
			}

			if (!options.build) return 0;
			std::printf("\n");
			return cmdBuild({ "build", options.project.string() });
		}
	}

	int cmdAdd(const Args& args) {
		if (argument(args, 1) == "screen") return addScreen(args);
		std::printf("rda: `add` knows `screen`: rda add screen <name> [<project>]\n");
		return 1;
	}
}
