// `rda init`: the files a project starts from.
//
// The tutorials have a newcomer write five or six files by hand before anything can be
// seen, and every one of them is the same five or six files. This writes them: an
// interface that `rda prototype .` can open, and with a language flag the backend that
// answers it. What gets written is the counter from the first-interface tutorial, small
// enough to read in a minute and complete enough to exercise the whole path -- a
// declaration, a theme, a layout with a binding and a handler, and a command the backend
// answers.
//
// The files are real files under Cli/templates/, embedded the way the JavaScript preludes
// are, so that a Program.cs reads and edits as a Program.cs. What is filled in is four
// names between @ signs and nothing else: no loops, no conditions. A project that needs
// more than that is one the author is writing by hand already.
//
// Nothing here needs a GPU or a window, and it must stay that way: `rda` links neither, and
// the project it makes is built by the same code `rda build` runs.
#include "Commands.h"

#include <Core/Location.h>
#include <Layout/ModuleTransform.h>

#include <InitCppCMake.h>
#include <InitCppMain.h>
#include <InitCSharp.h>
#include <InitCSharpProject.h>
#include <InitGitignore.h>
#include <InitLayout.h>
#include <InitNode.h>
#include <InitNodePackage.h>
#include <InitPython.h>
#include <InitState.h>
#include <InitTheme.h>
#include <InitTsconfig.h>

#include <cctype>
#include <cstdio>
#include <fstream>

namespace Cli {

	namespace {

		enum class Language { None, Python, Node, CSharp, Cpp };

		struct InitOptions {
			fs::path dir = ".";
			// The directory as the user wrote it, for the next steps: "." says nothing worth
			// printing, a name is something to `cd` into.
			bool     dirGiven = false;
			Language language = Language::None;
			bool     force = false;
			bool     build = true;
		};

		// False, with the reason printed, for an option nobody knows or a second language.
		bool parseInitOptions(const Args& args, InitOptions& options) {
			bool languageGiven = false;
			for (size_t i = 1; i < args.size(); ++i) {
				const std::string& arg = args[i];
				Language chosen = Language::None;
				if      (arg == "--python") chosen = Language::Python;
				else if (arg == "--node")   chosen = Language::Node;
				else if (arg == "--csharp") chosen = Language::CSharp;
				else if (arg == "--cpp")    chosen = Language::Cpp;
				else if (arg == "--force")    { options.force = true; continue; }
				else if (arg == "--no-build") { options.build = false; continue; }
				else if (!arg.empty() && arg[0] == '-') {
					std::printf("rda: unknown option %s\n", arg.c_str());
					return false;
				}
				else {
					if (options.dirGiven) {
						std::printf("rda: init takes one directory, and was given %s and %s\n",
						            shown(options.dir).c_str(), arg.c_str());
						return false;
					}
					options.dir = arg;
					options.dirGiven = true;
					continue;
				}
				// Only the language flags get here. A project has one backend: a second flag
				// is a mistake, not a request for both, and the two would want different
				// files with the same names.
				if (languageGiven && chosen != options.language) {
					std::printf("rda: a project has one backend -- choose one of --python, --node, "
					            "--csharp, --cpp\n");
					return false;
				}
				languageGiven = true;
				options.language = chosen;
			}
			return true;
		}

		// ---- the templates ---------------------------------------------------------------

		// A file embedded by rda_embed_js, with the CRs taken out: a checkout with
		// autocrlf holds these as CRLF, and the project they make should not depend on whose
		// machine ran the command.
		std::string textOf(const unsigned char* bytes, size_t size) {
			std::string text;
			text.reserve(size);
			for (size_t i = 0; i < size; ++i) {
				if (bytes[i] != '\r') text.push_back(static_cast<char>(bytes[i]));
			}
			return text;
		}

		void replaceAll(std::string& text, const std::string& token, const std::string& value) {
			for (size_t at = text.find(token); at != std::string::npos;
			     at = text.find(token, at + value.size())) {
				text.replace(at, token.size(), value);
			}
		}

		// What a template is told about the project it is becoming.
		struct Names {
			// The folder's name, as the window's title. Without a quote or a backslash, which
			// would end the string literal it is written into in three of the four languages.
			std::string display;
			// Lower case, letters, digits and hyphens: valid as an npm package, a CMake
			// project and target, and the name of a .csproj, which are all different rules.
			std::string slug;
			// Where the engine checkout is, as the project should say it.
			std::string engine;
			// The engine's shared library, which a C# project copies beside its executable.
			std::string library;
		};

		std::string filled(const unsigned char* bytes, size_t size, const Names& names) {
			std::string text = textOf(bytes, size);
			replaceAll(text, "@NAME@", names.display);
			replaceAll(text, "@SLUG@", names.slug);
			replaceAll(text, "@ENGINE@", names.engine);
			replaceAll(text, "@LIB@", names.library);
			return text;
		}

		std::string displayNameOf(const std::string& folder) {
			std::string name;
			for (char c : folder) {
				if (c != '"' && c != '\\') name.push_back(c);
			}
			return name.empty() ? "app" : name;
		}

		std::string slugOf(const std::string& folder) {
			std::string slug;
			for (char c : folder) {
				const unsigned char u = static_cast<unsigned char>(c);
				slug.push_back(std::isalnum(u) && u < 128 ? static_cast<char>(std::tolower(u)) : '-');
			}
			// npm refuses a name that starts with a hyphen, and a run of them reads as noise.
			size_t first = slug.find_first_not_of('-');
			if (first == std::string::npos) return "app";
			slug.erase(0, first);
			while (!slug.empty() && slug.back() == '-') slug.pop_back();
			return slug.empty() ? "app" : slug;
		}

		// ---- where the engine is ---------------------------------------------------------

		// The checkout this program belongs to: a directory above it that holds the Node
		// binding, which only a checkout does. The program sits in the checkout's bin/ once
		// staged, and in build/<preset>/tools/<config>/ before that, so a handful of
		// levels up finds it either way.
		fs::path checkoutAbove(fs::path from) {
			std::error_code ec;
			for (int depth = 0; depth < 6 && !from.empty(); ++depth) {
				if (fs::is_regular_file(from / "bindings" / "node" / "package.json", ec)
				    && fs::is_regular_file(from / "CMakeLists.txt", ec)) {
					return from;
				}
				if (!from.has_parent_path() || from.parent_path() == from) break;
				from = from.parent_path();
			}
			return {};
		}

		// How the project should name the checkout: relative to itself when it can, so the
		// folder pair can move together, and absolute when it cannot -- another drive has no
		// relative path. Empty when this program is not in a checkout at all.
		std::string enginePathFor(const fs::path& project) {
			const fs::path root = checkoutAbove(RDA::executableDirectory());
			if (root.empty()) return {};
			std::error_code ec;
			const fs::path relative = fs::relative(root, project, ec);
			if (ec || relative.empty()) return root.generic_string();
			return relative.generic_string();
		}

		// ---- the plan --------------------------------------------------------------------

		struct Planned {
			fs::path    where;      // relative to the project
			std::string content;
		};

		std::vector<Planned> planFiles(const InitOptions& options, const Names& names) {
			std::vector<Planned> plan = {
				{ "res/state.ts",             filled(kInitState, kInitStateSize, names) },
				{ "res/themes/app.ts",        filled(kInitTheme, kInitThemeSize, names) },
				{ "res/layouts/main.tsx",     filled(kInitLayout, kInitLayoutSize, names) },
				{ "res/layouts/tsconfig.json", filled(kInitTsconfig, kInitTsconfigSize, names) },
				{ ".gitignore",               filled(kInitGitignore, kInitGitignoreSize, names) },
			};
			switch (options.language) {
			case Language::None:
				break;
			case Language::Python:
				plan.push_back({ "app.py", filled(kInitPython, kInitPythonSize, names) });
				break;
			case Language::Node:
				plan.push_back({ "app.mjs", filled(kInitNode, kInitNodeSize, names) });
				plan.push_back({ "package.json", filled(kInitNodePackage, kInitNodePackageSize, names) });
				break;
			case Language::CSharp:
				plan.push_back({ "Program.cs", filled(kInitCSharp, kInitCSharpSize, names) });
				plan.push_back({ names.slug + ".csproj",
				                 filled(kInitCSharpProject, kInitCSharpProjectSize, names) });
				break;
			case Language::Cpp:
				plan.push_back({ "main.cpp", filled(kInitCppMain, kInitCppMainSize, names) });
				plan.push_back({ "CMakeLists.txt", filled(kInitCppCMake, kInitCppCMakeSize, names) });
				break;
			}
			return plan;
		}

		// Binary, so what is written is the bytes in the template and not what the platform
		// would have made of its newlines.
		bool writeFile(const fs::path& path, const std::string& content) {
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out.write(content.data(), static_cast<std::streamsize>(content.size()));
			return static_cast<bool>(out);
		}

		const char* languageName(Language language) {
			switch (language) {
			case Language::Python: return "Python";
			case Language::Node:   return "Node";
			case Language::CSharp: return "C#";
			case Language::Cpp:    return "C++";
			default:               return "interface only";
			}
		}

		// The flag `rda build` takes for the state module this language imports, so that
		// the first build already writes it and the editor can see it.
		const char* buildFlagFor(Language language) {
			switch (language) {
			case Language::Python: return "--python";
			case Language::Node:   return "--node";
			case Language::CSharp: return "--csharp";
			// A C++ project gets its RdaState.h from CMake, into the build tree.
			default:               return nullptr;
			}
		}

		void printNextSteps(const InitOptions& options, const Names& names) {
			std::printf("\nNext:\n");
			if (options.dirGiven && shown(options.dir) != ".") {
				std::printf("  cd %s\n", shown(options.dir).c_str());
			}
			std::printf("  rda prototype .                 the interface, with no backend behind it\n");
			switch (options.language) {
			case Language::None:
				break;
			case Language::Python:
				std::printf("  python app.py\n");
				break;
			case Language::Node:
				std::printf("  npm install                     once: links the rda package from the checkout\n");
				std::printf("  node app.mjs\n");
				break;
			case Language::CSharp:
				std::printf("  dotnet run                      builds res/ first, then the program\n");
				break;
			case Language::Cpp:
				std::printf("  cmake -B build\n");
				std::printf("  cmake --build build --config Debug\n");
				std::printf("  then run %s from build/bin/Debug/\n", names.slug.c_str());
				break;
			}
		}

		int initProject(const InitOptions& options) {
			std::error_code ec;
			// Resolved before the folder exists, so that "." and "../x" give the folder's real
			// name and not a dot.
			const fs::path project = fs::weakly_canonical(options.dir, ec);
			if (ec) {
				std::printf("rda: cannot make sense of %s: %s\n", shown(options.dir).c_str(),
				            ec.message().c_str());
				return 1;
			}

			Names names;
			const std::string folder = project.filename().string();
			names.display = displayNameOf(folder);
			names.slug = slugOf(folder);
#if defined(_WIN32)
			names.library = "rendeer_c.dll";
#else
			names.library = "librendeer_c.so";
#endif
			names.engine = enginePathFor(project);
			const bool engineFound = !names.engine.empty();
			if (!engineFound) names.engine = "../RendeerAlpha";

			const std::vector<Planned> plan = planFiles(options, names);

			// Everything is looked at before anything is written: a refusal that had already
			// written half of the files would be worse than the overwrite it was avoiding.
			if (!options.force) {
				std::vector<std::string> existing;
				for (const Planned& one : plan) {
					if (fs::exists(project / one.where, ec)) existing.push_back(shown(one.where));
				}
				if (!existing.empty()) {
					std::printf("rda: nothing was changed -- %s already holds %s init would write:\n",
					            shown(options.dir).c_str(),
					            existing.size() == 1 ? "a file" : "files");
					for (const std::string& one : existing) std::printf("  %s\n", one.c_str());
					std::printf("Use --force to overwrite %s.\n", existing.size() == 1 ? "it" : "them");
					return 1;
				}
			}

			for (const Planned& one : plan) {
				const fs::path path = project / one.where;
				fs::create_directories(path.parent_path(), ec);
				if (ec || !writeFile(path, one.content)) {
					std::printf("rda: cannot write %s%s%s\n", shown(path).c_str(),
					            ec ? ": " : "", ec ? ec.message().c_str() : "");
					return 1;
				}
			}

			std::printf("rda: %s project %s, %zu files\n", languageName(options.language),
			            shown(options.dir).c_str(), plan.size());
			for (const Planned& one : plan) std::printf("  %s\n", shown(one.where).c_str());
			if (options.language != Language::None && !engineFound) {
				std::printf("rda: this rda is not inside a RendeerAlpha checkout, so the project "
				            "points at ../RendeerAlpha. Edit that path if the checkout is elsewhere.\n");
			}

			if (options.build) {
				// The build is what writes res/layouts/rda.d.ts, which the editor needs before
				// it can check a line of the layout. The project is already made by now, so no
				// esbuild is a note rather than a failure: install it, and `rda build .` does
				// the rest.
				if (RDA::Layout::findEsbuild().empty()) {
					std::printf("\nrda: esbuild was not found, so nothing was compiled. The project is "
					            "made; run `rda build .` once esbuild is installed (see "
					            "docs/setup/troubleshooting.md).\n");
				} else {
					std::printf("\n");
					// The directory as it was typed, so what the build prints is short enough to read.
					Args buildLine = { "build", options.dir.string() };
					if (const char* flag = buildFlagFor(options.language)) buildLine.push_back(flag);
					if (cmdBuild(buildLine) != 0) return 1;
				}
			}

			printNextSteps(options, names);
			return 0;
		}
	}

	int cmdInit(const Args& args) {
		InitOptions options;
		if (!parseInitOptions(args, options)) return 1;
		return initProject(options);
	}
}
