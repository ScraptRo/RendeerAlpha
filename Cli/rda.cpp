// rda — the layout toolchain.
//
//   rda build  <project> [--python|--node|--csharp|--cpp <dir>]
//                                      everything a project's res/ needs, by convention
//   rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint
//   rda types  <out.d.ts> [theme.ts] [state.ts]  emit the TypeScript definitions
//   rda theme  <theme.ts> [out.rdth]   compile a theme
//   rda state  <state.ts>  <out.h>     generate the C++ for a state declaration
//   rda python|node|csharp <state.ts> <out>   the same, for another language
//   rda dump   <file.rdab>             print what a compiled layout contains
//
// Compilers, every one of them. Everything else this program used to do — scaffolding a
// project, building it, running it — is a script's job and lives in scripts/. Those
// tasks are shell work wearing a C++ coat; these are not, because they need the widget
// schema and a JavaScript engine to evaluate a layout once.
//
// `build` is the one a person runs. A C++ project has CMake to call the others one file
// at a time; a Python, Node or C# project has no build system that would, and should
// not need one to compile an interface. So this walks a project's res/ and does what the
// CMake rules would have: themes, the types, the layouts, and the state in whichever
// language was asked for -- skipping whatever is already newer than its inputs.
#include <Core/BuildMode.h>
#include <Layout/LayoutCompiler.h>
#include <Layout/TypeEmitter.h>
#include <Layout/StateSchema.h>
#include <Layout/ThemeCompiler.h>
#include <Layout/Expression.h>

#include <Core/Location.h>
#include <Layout/ModuleTransform.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	void usage() {
		std::printf(
			"rda - the Rendeer layout toolchain\n\n"
			"  rda build  <project> [--python] [--node] [--csharp] [--cpp <dir>] [--force]\n"
			"                                     compile everything under <project>/res/\n"
			"  rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint\n"
			"  rda types  <out.d.ts> [theme] [state]  emit the TypeScript definitions\n"
			"  rda theme  <theme.ts> [out.rdth]   compile a theme\n"
			"  rda state  <state.ts>  <out.h>     generate the C++ for a state declaration\n"
			"  rda python <state.ts>  <out.py>    generate the Python for a state declaration\n"
			"  rda node   <state.ts>  <out.mjs>   generate the JavaScript for a state declaration\n"
			"  rda csharp <state.ts>  <out.cs>    generate the C# for a state declaration\n"
			"  rda dump   <file.rdab>             print what a compiled layout contains\n\n"
			"A layout is TypeScript with JSX in it. Compiling turns it into a flat file of\n"
			"nodes and strings that an application reads without a parser of any kind.\n\n"
			"`build` expects the layout every project has: res/state.ts, res/themes/*.ts and\n"
			"res/layouts/*.tsx. It writes each theme's .rdth and each layout's .rdab beside\n"
			"its source, res/layouts/rda.d.ts from the first theme and the state, and the\n"
			"state module -- state.py, state.mjs or state.cs -- into the project directory.\n");
	}

	int layout(const std::string& source, const std::string& output) {
		const std::string target =
			output.empty() ? RDA::Layout::blueprintPathFor(source) : output;

		const RDA::Layout::CompileResult result = RDA::Layout::compileToFile(source, target);
		if (!result.ok) {
			std::printf("rda: %s\n%s\n", source.c_str(), result.error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu nodes, %zu bindings, %zu bytes  (transform %.1f ms, evaluate %.1f ms)\n",
		            source.c_str(), target.c_str(), result.nodeCount, result.bindingCount,
		            result.bytes.size(), result.transformMs, result.evaluateMs);
		return 0;
	}

	// Generates the C++ side of a state declaration: the signal definitions, and typed
	// access to them. The same file feeds `rda types`, which is the point -- one
	// declaration, and both sides of the boundary agree by construction.
	int theme(const std::string& source, const std::string& output) {
		const std::string target =
			output.empty() ? RDA::Layout::themePathFor(source) : output;

		const RDA::Layout::ThemeCompileResult result =
			RDA::Layout::compileThemeToFile(source, target);
		if (!result.ok) {
			std::printf("rda: %s\n%s\n", source.c_str(), result.error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu variants, %zu bytes  (transform %.1f ms, evaluate %.1f ms)\n",
		            source.c_str(), target.c_str(), result.variantCount,
		            result.bytes.size(), result.transformMs, result.evaluateMs);
		return 0;
	}

	int state(const std::string& source, const std::string& output) {
		const RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		std::string error;
		if (!RDA::Layout::emitStateHeader(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals\n", source.c_str(), output.c_str(), schema.fields.size());
		return 0;
	}

	// The same declaration as Python. A backend written in Python reaches the engine
	// through its C ABI, and this is what gives it names to reach with.
	int python(const std::string& source, const std::string& output) {
		const RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		std::string error;
		if (!RDA::Layout::emitStatePython(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals, %zu commands\n", source.c_str(), output.c_str(),
		            schema.fields.size(), schema.commands.size());
		return 0;
	}

	// The same declaration as an ES module. Node reaches the engine through the same C
	// ABI Python does; what differs is which side calls the other, and the binding is
	// where that difference lives.
	int node(const std::string& source, const std::string& output) {
		const RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		std::string error;
		if (!RDA::Layout::emitStateNode(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals, %zu commands\n", source.c_str(), output.c_str(),
		            schema.fields.size(), schema.commands.size());
		return 0;
	}

	// The same declaration as C#. The one generator whose output the consuming compiler
	// checks as thoroughly as the engine does.
	int csharp(const std::string& source, const std::string& output) {
		const RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		std::string error;
		if (!RDA::Layout::emitStateCSharp(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals, %zu commands\n", source.c_str(), output.c_str(),
		            schema.fields.size(), schema.commands.size());
		return 0;
	}

	int dump(const std::string& path) {
		RDA::Layout::Blueprint blueprint;
		std::string error;
		if (!RDA::Layout::loadBlueprintFile(path, blueprint, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}

		std::printf("%s: %zu nodes, %zu bindings, %zu bytes\n\n",
		            path.c_str(), blueprint.nodeCount(), blueprint.bindingCount(),
		            blueprint.byteSize());

		for (size_t i = 0; i < blueprint.nodeCount(); ++i) {
			const RDA::Layout::BlueprintNode& node = blueprint.node(i);
			std::printf("  [%zu] <%.*s> %.*s\n", i,
			            static_cast<int>(blueprint.string(node.type).size()), blueprint.string(node.type).data(),
			            static_cast<int>(blueprint.string(node.id).size()), blueprint.string(node.id).data());
			for (uint32_t p = 0; p < node.propCount; ++p) {
				const RDA::Layout::BlueprintProp& prop = blueprint.prop(node.firstProp + p);
				const std::string_view key = blueprint.string(prop.key);
				if (prop.kind == static_cast<uint32_t>(RDA::Layout::PropKind::String)) {
					const std::string_view text = blueprint.string(prop.text);
					std::printf("        %.*s = \"%.*s\"\n",
					            static_cast<int>(key.size()), key.data(),
					            static_cast<int>(text.size()), text.data());
				} else {
					std::printf("        %.*s = %g\n",
					            static_cast<int>(key.size()), key.data(), prop.number);
				}
			}
		}

		if (blueprint.bindingCount() > 0) std::printf("\n");
		for (size_t i = 0; i < blueprint.bindingCount(); ++i) {
			const RDA::Layout::BlueprintBinding& binding = blueprint.binding(i);
			const std::string_view key = blueprint.string(binding.key);
			const std::string_view owner = blueprint.string(blueprint.node(binding.node).id);
			std::printf("  %s %.*s on %.*s\n",
			            binding.kind == static_cast<uint32_t>(RDA::Layout::BindingKind::Event)
			                ? "event" : "bind ",
			            static_cast<int>(key.size()), key.data(),
			            static_cast<int>(owner.size()), owner.data());
			const RDA::Layout::Program program = blueprint.programFor(binding);
			const std::string listing = RDA::Layout::disassemble(program);
			size_t start = 0;
			while (start < listing.size()) {
				const size_t end = listing.find('\n', start);
				std::printf("        %s\n", listing.substr(start, end - start).c_str());
				if (end == std::string::npos) break;
				start = end + 1;
			}
		}
		return 0;
	}

	// ---- build: a whole project by convention ------------------------------------------

	namespace fs = std::filesystem;

	// Modification time as a plain number, 0 for a file that is not there. Only compared,
	// never shown, so the epoch and the unit do not matter as long as they match.
	int64_t stampOf(const fs::path& path) {
		std::error_code ec;
		const auto written = fs::last_write_time(path, ec);
		if (ec) return 0;
		return written.time_since_epoch().count();
	}

	// Whether `output` has to be made: missing, or older than any of its inputs, or older
	// than this tool -- a blueprint written by an older compiler is stale even when its
	// source has not moved, and that is the case nobody remembers.
	bool stale(const fs::path& output, const std::vector<fs::path>& inputs, int64_t toolStamp) {
		const int64_t have = stampOf(output);
		if (have == 0) return true;
		if (have < toolStamp) return true;
		for (const fs::path& input : inputs) {
			if (stampOf(input) > have) return true;
		}
		return false;
	}

	std::vector<fs::path> filesIn(const fs::path& directory, const std::string& extension) {
		std::vector<fs::path> found;
		std::error_code ec;
		if (!fs::is_directory(directory, ec)) return found;
		for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == extension) {
				found.push_back(entry.path());
			}
		}
		// Alphabetical, so "the first theme" means the same thing on every filesystem.
		std::sort(found.begin(), found.end());
		return found;
	}

	std::string shown(const fs::path& path) { return path.lexically_normal().generic_string(); }

	// Which of these files are imported by another one of them.
	//
	// A folder of layouts holds screens and the components they are built from, and a
	// folder of themes holds the theme and whatever palette it reads. Only the ones
	// nothing imports are compiled: a component has props and no meaning on its own, and
	// a palette styles nothing, so compiling either produces an error about a file that
	// was never wrong. Nobody should have to name their shared file specially to be left
	// alone by a build.
	std::set<fs::path> importedByAnother(const std::vector<fs::path>& candidates) {
		std::set<fs::path> imported;
		for (const fs::path& source : candidates) {
			const std::vector<std::string> files = RDA::Layout::importedFiles(source.string());
			// [0] is the file itself; everything after it is something it pulled in.
			for (size_t i = 1; i < files.size(); ++i) {
				std::error_code ec;
				fs::path resolved = fs::weakly_canonical(files[i], ec);
				imported.insert(ec ? fs::path(files[i]) : resolved);
			}
		}
		return imported;
	}

	bool isImported(const std::set<fs::path>& imported, const fs::path& candidate) {
		std::error_code ec;
		const fs::path resolved = fs::weakly_canonical(candidate, ec);
		return imported.count(ec ? candidate : resolved) != 0;
	}

	int build(const std::vector<std::string>& args) {
		fs::path project = ".";
		bool wantPython = false, wantNode = false, wantCSharp = false, force = false;
		std::string cppDir;
		for (size_t i = 1; i < args.size(); ++i) {
			const std::string& arg = args[i];
			if      (arg == "--python") wantPython = true;
			else if (arg == "--node")   wantNode = true;
			else if (arg == "--csharp") wantCSharp = true;
			else if (arg == "--force")  force = true;
			else if (arg == "--cpp" && i + 1 < args.size()) cppDir = args[++i];
			else if (!arg.empty() && arg[0] == '-') {
				std::printf("rda: unknown option %s\n", arg.c_str());
				return 1;
			}
			else project = arg;
		}

		const fs::path res = project / "res";
		std::error_code ec;
		if (!fs::is_directory(res, ec)) {
			std::printf("rda: no res/ directory in %s -- a project keeps its declaration, "
			            "theme and layouts there\n", shown(project).c_str());
			return 1;
		}

		const fs::path state = res / "state.ts";
		const bool haveState = fs::is_regular_file(state, ec);
		const std::vector<fs::path> themes  = filesIn(res / "themes", ".ts");
		const std::vector<fs::path> layouts = filesIn(res / "layouts", ".tsx");
		const int64_t tool = force ? INT64_MAX : stampOf(RDA::executablePath());

		int failed = 0;
		size_t made = 0, kept = 0;

		// A file that another one imports is a part rather than a whole: a component, or
		// a palette. It is compiled as part of whatever imports it and never on its own.
		const std::set<fs::path> importedThemes = importedByAnother(themes);
		const std::set<fs::path> importedLayouts = importedByAnother(layouts);
		size_t parts = 0;

		// Themes first: the types read one, and a layout is checked against those.
		for (const fs::path& source : themes) {
			if (isImported(importedThemes, source)) { ++parts; continue; }
			const fs::path out = RDA::Layout::themePathFor(source.string());
			if (!stale(out, { source }, tool)) { ++kept; continue; }
			const RDA::Layout::ThemeCompileResult result =
				RDA::Layout::compileThemeToFile(source.string(), out.string());
			if (!result.ok) {
				std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
				++failed;
				continue;
			}
			std::printf("rda: %s -> %s  (%zu variants)\n", shown(source).c_str(),
			            shown(out).c_str(), result.variantCount);
			++made;
		}

		// The definitions a layout is checked against, from the first theme and the state.
		// Regenerated whenever either moves, and whenever the tool does: a .d.ts that has
		// drifted from the widget schema is worse than none.
		if (!layouts.empty() || !themes.empty()) {
			const fs::path out = res / "layouts" / "rda.d.ts";
			fs::path firstTheme;
			for (const fs::path& theme : themes) {
				if (!isImported(importedThemes, theme)) { firstTheme = theme; break; }
			}
			std::vector<fs::path> inputs;
			if (!firstTheme.empty()) inputs.push_back(firstTheme);
			if (haveState) inputs.push_back(state);
			if (stale(out, inputs, tool)) {
				fs::create_directories(out.parent_path(), ec);
				const RDA::Layout::TypesResult result = RDA::Layout::emitTypeDefinitions(
					out.string(), firstTheme.empty() ? std::string() : firstTheme.string(),
					haveState ? state.string() : std::string());
				if (!result.ok) {
					std::printf("rda: %s\n", result.error.c_str());
					++failed;
				} else {
					std::printf("rda: %s  (%zu elements, %zu variants, %zu signals)\n",
					            shown(out).c_str(), result.widgetCount, result.variantCount,
					            result.stateCount);
					++made;
				}
			} else {
				++kept;
			}
		}

		// The layouts. A layout may import other files, so every one of those is an
		// input -- the same list hot reload watches.
		for (const fs::path& source : layouts) {
			if (isImported(importedLayouts, source)) { ++parts; continue; }
			const fs::path out = RDA::Layout::blueprintPathFor(source.string());
			std::vector<fs::path> inputs;
			for (const std::string& imported : RDA::Layout::importedFiles(source.string())) {
				inputs.emplace_back(imported);
			}
			if (inputs.empty()) inputs.push_back(source);
			if (!stale(out, inputs, tool)) { ++kept; continue; }
			const RDA::Layout::CompileResult result =
				RDA::Layout::compileToFile(source.string(), out.string());
			if (!result.ok) {
				std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
				++failed;
				continue;
			}
			std::printf("rda: %s -> %s  (%zu nodes, %zu bindings)\n", shown(source).c_str(),
			            shown(out).c_str(), result.nodeCount, result.bindingCount);
			++made;
		}

		// The state, in whichever languages were asked for. Into the project directory,
		// beside the program that imports it, because that is where an import resolves
		// from with nothing configured.
		struct Emit {
			bool wanted;
			fs::path out;
			bool (*emit)(const RDA::Layout::StateSchema&, const std::string&, std::string&);
		};
		const std::vector<Emit> emits = {
			{ wantPython,      project / "state.py",  RDA::Layout::emitStatePython },
			{ wantNode,        project / "state.mjs", RDA::Layout::emitStateNode },
			{ wantCSharp,      project / "state.cs",  RDA::Layout::emitStateCSharp },
			{ !cppDir.empty(), fs::path(cppDir) / "RdaState.h", RDA::Layout::emitStateHeader },
		};
		bool anyWanted = false;
		for (const Emit& e : emits) anyWanted = anyWanted || e.wanted;
		if (anyWanted && !haveState) {
			std::printf("rda: no %s -- a state module was asked for and there is no "
			            "declaration to generate it from\n", shown(state).c_str());
			++failed;
		} else if (anyWanted) {
			RDA::Layout::StateSchema schema;
			bool loaded = false;
			for (const Emit& e : emits) {
				if (!e.wanted) continue;
				if (!stale(e.out, { state }, tool)) { ++kept; continue; }
				if (!loaded) {
					schema = RDA::Layout::loadStateSchema(state.string());
					loaded = true;
					if (!schema.ok) {
						std::printf("rda: %s\n", schema.error.c_str());
						++failed;
						break;
					}
				}
				if (e.out.has_parent_path()) fs::create_directories(e.out.parent_path(), ec);
				std::string error;
				if (!e.emit(schema, e.out.string(), error)) {
					std::printf("rda: %s\n", error.c_str());
					++failed;
					continue;
				}
				std::printf("rda: %s -> %s  (%zu signals, %zu commands)\n", shown(state).c_str(),
				            shown(e.out).c_str(), schema.fields.size(), schema.commands.size());
				++made;
			}
		}

		std::string partsNote;
		if (parts) {
			partsNote = ", " + std::to_string(parts) +
			            (parts == 1 ? " imported by another and left to it"
			                        : " imported by others and left to them");
		}
		if (failed) {
			std::printf("rda: %d failed, %zu written, %zu up to date%s\n", failed, made, kept,
			            partsNote.c_str());
			return 1;
		}
		std::printf("rda: %zu written, %zu up to date%s\n", made, kept, partsNote.c_str());
		return 0;
	}

	int types(const std::string& output, const std::string& theme, const std::string& stateFile) {
		const std::string target = output.empty() ? std::string("rda.d.ts") : output;

		const RDA::Layout::TypesResult result =
			RDA::Layout::emitTypeDefinitions(target, theme, stateFile);
		if (!result.ok) {
			std::printf("rda: %s\n", result.error.c_str());
			return 1;
		}
		std::printf("rda: wrote %s\n"
		            "     %zu elements, %zu theme variants, %zu signals, %zu bytes\n",
		            target.c_str(), result.widgetCount, result.variantCount,
		            result.stateCount, result.byteSize);
		if (theme.empty()) {
			std::printf("     (no theme given, so variants are plain strings)\n");
		}
		if (stateFile.empty()) {
			std::printf("     (no state given, so state is loosely typed)\n");
		}
		return 0;
	}
}

int main(int argc, char** argv) {
	std::vector<std::string> args(argv + 1, argv + argc);
	if (args.empty()) { usage(); return 1; }

	const std::string command = args[0];
	const std::string first   = args.size() > 1 ? args[1] : std::string();
	const std::string second  = args.size() > 2 ? args[2] : std::string();

	if (command == "build")  return build(args);
	if (command == "layout" && !first.empty()) return layout(first, second);
	const std::string third = args.size() > 3 ? args[3] : std::string();
	if (command == "types"  && !first.empty()) return types(first, second, third);
	if (command == "state"  && !first.empty() && !second.empty()) return state(first, second);
	if (command == "python" && !first.empty() && !second.empty()) return python(first, second);
	if (command == "node" && !first.empty() && !second.empty()) return node(first, second);
	if (command == "csharp" && !first.empty() && !second.empty()) return csharp(first, second);
	if (command == "theme"  && !first.empty()) return theme(first, second);
	if (command == "dump"   && !first.empty()) return dump(first);

	usage();
	return 1;
}
