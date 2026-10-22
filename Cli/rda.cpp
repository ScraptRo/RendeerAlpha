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
#include <Layout/MeshCompiler.h>
#include <Layout/ThemeSchema.h>
#include <Layout/ThemeCompiler.h>
#include <Layout/Expression.h>

#include <Core/Location.h>
#include <Layout/ModuleTransform.h>

// `prototype` opens a window, so the tool links the runtime as well as the compilers.
// It already did -- rda has always linked `rendeer` whole -- and nothing else here
// touches any of it.
#include <RendeerAlpha.h>
#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Shortcut.h>
#include <Core/Signals.h>
#include <Layout/Prototype.h>
#include <Core/Png.h>
#include <Layout/Router.h>
#include <Layout/LayoutHost.h>
#include <Layout/ScriptHost.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

	// --no-runtime-code, anywhere on the line: a layout that would need run-time code is a
	// build error instead, naming the binding. For an application that wants "a shipped
	// binary evaluates no JavaScript" to stay true.
	bool gNoRuntimeCode = false;

	RDA::Layout::CompileOptions compileOptions() {
		RDA::Layout::CompileOptions options;
		options.allowRuntimeCode = !gNoRuntimeCode;
		return options;
	}

	// state.ts's code, compiled into the schema a generated module is written from -- the
	// module carries it. A declaration with no code needs nothing; one whose code does not
	// compile is said, and false.
	bool withStateCode(RDA::Layout::StateSchema& schema, const std::string& source) {
		if (!schema.hasCode() || !schema.code.empty()) return true;
		const RDA::Layout::StateCodeResult code = RDA::Layout::compileStateCode(source, compileOptions());
		if (!code.ok) {
			std::printf("rda: %s\n%s\n", source.c_str(), code.error.c_str());
			return false;
		}
		schema.code = code.bytes;
		return true;
	}

	// ", 2 computed, 1 action, 3412 bytes of code", or nothing for a declaration with none.
	std::string stateSummary(const RDA::Layout::StateSchema& schema) {
		if (schema.code.empty()) return {};
		size_t computed = 0, actions = 0;
		for (const RDA::Layout::StateField& one : schema.fields) computed += one.computed ? 1 : 0;
		for (const RDA::Layout::StateCommand& one : schema.commands) actions += one.action ? 1 : 0;
		char text[160];
		std::snprintf(text, sizeof(text), ", %zu computed, %zu action%s, %zu bytes of code",
		              computed, actions, actions == 1 ? "" : "s", schema.code.size());
		return text;
	}

	// ", 3 run-time bindings, 2 functions, 4 KB of bytecode", or nothing for a layout with none.
	std::string runtimeSummary(const RDA::Layout::CompileResult& result) {
		if (result.scriptBytes == 0) return {};
		char text[160];
		std::snprintf(text, sizeof(text), ", run-time code: %zu binding%s and %zu function%s in %zu bytes",
		              result.scriptBindings, result.scriptBindings == 1 ? "" : "s",
		              result.scriptCalls, result.scriptCalls == 1 ? "" : "s", result.scriptBytes);
		return text;
	}

	void usage() {
		std::printf(
			"rda - the Rendeer layout toolchain\n\n"
			"  rda build  <project> [--python] [--node] [--csharp] [--cpp <dir>] [--force]\n"
			"             [--no-runtime-code]\n"
			"                                     compile everything under <project>/res/\n"
			"  rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint\n"
			"  rda types  <out.d.ts> [theme] [state]  emit the TypeScript definitions\n"
			"  rda theme  <theme.ts> [out.rdth]   compile a theme\n"
			"  rda mesh   <mesh.ts>  [out.rdam]   compile a widget mesh\n"
			"  --mesh <mesh.ts>                   on `theme` and `types`, repeatable: a mesh\n"
			"                                     whose parameters the schema includes\n"
			"  rda state  <state.ts>  <out.h>     generate the C++ for a state declaration\n"
			"  rda python <state.ts>  <out.py>    generate the Python for a state declaration\n"
			"  rda node   <state.ts>  <out.mjs>   generate the JavaScript for a state declaration\n"
			"  rda csharp <state.ts>  <out.cs>    generate the C# for a state declaration\n"
			"  rda dump   <file.rdab>             print what a compiled layout contains\n"
			"  rda prototype <project> [--layout <name>] [--set n=v] [--rows t=n]\n"
			"                          [--data <file.json>] [--no-build]\n"
			"                                     open the interface with placeholder state\n"
			"  rda shot   <project> [--out <file.png>] [--size WxH] [--scale <n>]\n"
			"                       [--frames <n>] [--every <ms>] [--settle <n>]\n"
			"                       [--hover X,Y] [--press X,Y] [--scroll <n>]\n"
			"                       [--key <chord>]... [--type <text>]...\n"
			"                       [--drag X1,Y1,X2,Y2 [--hold]] [--files <a;b>]\n"
			"                       [--click [Ctrl+][Shift+]X,Y]...\n"
			"                       [--a11y <tree.txt>] [--compose <text>]\n"
			"                                                          + the above\n"
			"                                     the same with no window: writes a .png\n\n"
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

		const RDA::Layout::CompileResult result =
			RDA::Layout::compileToFile(source, target, compileOptions());
		if (!result.ok) {
			std::printf("rda: %s\n%s\n", source.c_str(), result.error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu nodes, %zu bindings, %zu bytes%s  (transform %.1f ms, evaluate %.1f ms)\n",
		            source.c_str(), target.c_str(), result.nodeCount, result.bindingCount,
		            result.bytes.size(), runtimeSummary(result).c_str(),
		            result.transformMs, result.evaluateMs);
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

	// A mesh: the geometry a widget is drawn with, built from a TypeScript graph.
	//
	// Compiled before the themes, because a mesh declares what the theme may carry --
	// see `build` below for the ordering that follows from that.
	int mesh(const std::string& source, const std::string& output) {
		const std::string target =
			output.empty() ? RDA::Layout::meshPathFor(source) : output;

		const RDA::Layout::MeshCompileResult result =
			RDA::Layout::compileMeshToFile(source, target);
		if (!result.ok) {
			std::printf("rda: %s\n%s\n", source.c_str(), result.error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu states, %zu vertices, %zu triangles, %zu parameters, %zu bytes"
		            "  (transform %.1f ms, evaluate %.1f ms)\n",
		            source.c_str(), target.c_str(), result.stateCount, result.vertexCount,
		            result.triangleCount, result.params.size(), result.bytes.size(),
		            result.transformMs, result.evaluateMs);
		return 0;
	}

	// What a set of meshes declares, merged into the theme schema before anything is
	// checked against it.
	//
	// `build` does this by walking res/mesh, because it has every part of the project in
	// one process. A project built by CMake compiles each part in its own, so the meshes
	// a theme may draw with have to be named on the command line -- which is what
	// `rda_add_meshes` then does for you.
	//
	// Nothing is written here. A mesh is compiled by its own rule; this reads the same
	// source again only for what it declares, which is the one thing a .rdam on disk
	// cannot be asked for.
	bool registerMeshes(const std::vector<std::string>& sources) {
		std::vector<RDA::Layout::MeshThemeParam> params;
		for (const std::string& source : sources) {
			const RDA::Layout::MeshCompileResult declared = RDA::Layout::compileMesh(source);
			if (!declared.ok) {
				std::printf("rda: %s\n%s\n", source.c_str(), declared.error.c_str());
				return false;
			}
			for (const RDA::Layout::MeshParamDecl& one : declared.params) {
				params.push_back({ one.mesh + "." + one.name, one.doc,
				                   one.isColour ? RDA::Layout::ThemeFieldType::Colour
				                                : RDA::Layout::ThemeFieldType::Number });
			}
		}
		// Registered even when empty, so a command given no meshes is checked against the
		// engine's table alone rather than against whatever a previous call left behind.
		RDA::Layout::registerMeshThemeParams(params);
		return true;
	}

	int state(const std::string& source, const std::string& output) {
		RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		if (!withStateCode(schema, source)) return 1;
		std::string error;
		if (!RDA::Layout::emitStateHeader(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals%s\n", source.c_str(), output.c_str(), schema.fields.size(),
		            stateSummary(schema).c_str());
		return 0;
	}

	// The same declaration as Python. A backend written in Python reaches the engine
	// through its C ABI, and this is what gives it names to reach with.
	int python(const std::string& source, const std::string& output) {
		RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		if (!withStateCode(schema, source)) return 1;
		std::string error;
		if (!RDA::Layout::emitStatePython(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals, %zu commands%s\n", source.c_str(), output.c_str(),
		            schema.fields.size(), schema.commands.size(), stateSummary(schema).c_str());
		return 0;
	}

	// The same declaration as an ES module. Node reaches the engine through the same C
	// ABI Python does; what differs is which side calls the other, and the binding is
	// where that difference lives.
	int node(const std::string& source, const std::string& output) {
		RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		if (!withStateCode(schema, source)) return 1;
		std::string error;
		if (!RDA::Layout::emitStateNode(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals, %zu commands%s\n", source.c_str(), output.c_str(),
		            schema.fields.size(), schema.commands.size(), stateSummary(schema).c_str());
		return 0;
	}

	// The same declaration as C#. The one generator whose output the consuming compiler
	// checks as thoroughly as the engine does.
	int csharp(const std::string& source, const std::string& output) {
		RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return 1;
		}
		if (!withStateCode(schema, source)) return 1;
		std::string error;
		if (!RDA::Layout::emitStateCSharp(schema, output, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}
		std::printf("rda: %s -> %s\n"
		            "     %zu signals, %zu commands%s\n", source.c_str(), output.c_str(),
		            schema.fields.size(), schema.commands.size(), stateSummary(schema).c_str());
		return 0;
	}

	int dump(const std::string& path) {
		RDA::Layout::Blueprint blueprint;
		std::string error;
		if (!RDA::Layout::loadBlueprintFile(path, blueprint, error)) {
			std::printf("rda: %s\n", error.c_str());
			return 1;
		}

		std::printf("%s: %zu nodes, %zu bindings, %zu bytes\n",
		            path.c_str(), blueprint.nodeCount(), blueprint.bindingCount(),
		            blueprint.byteSize());
		if (blueprint.hasScript()) {
			const RDA::Layout::BlueprintScript& script = blueprint.script();
			size_t lines = script.lineMap.empty() ? 0 : 1;
			for (const char c : script.lineMap) if (c == '\n') ++lines;
			std::printf("run-time code: %zu bytes of QuickJS %s bytecode, %zu lines mapped to source\n",
			            script.code.size(), script.engine.c_str(), lines);
		}
		std::printf("\n");

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
		const std::vector<fs::path> meshes  = filesIn(res / "mesh", ".ts");
		const std::vector<fs::path> themes  = filesIn(res / "themes", ".ts");
		const std::vector<fs::path> layouts = filesIn(res / "layouts", ".tsx");
		const int64_t tool = force ? INT64_MAX : stampOf(RDA::executablePath());

		int failed = 0;
		size_t made = 0, kept = 0;

		// A file that another one imports is a part rather than a whole: a component, or
		// a palette. It is compiled as part of whatever imports it and never on its own.
		const std::set<fs::path> importedMeshes = importedByAnother(meshes);
		const std::set<fs::path> importedThemes = importedByAnother(themes);
		const std::set<fs::path> importedLayouts = importedByAnother(layouts);
		size_t parts = 0;

		// Meshes first, before the themes.
		//
		// This is the one ordering in the build that is not obvious, and it comes from
		// the meshes declaring what the theme may carry: a mesh that asks for a `seam`
		// colour adds a field to the theme's schema, so a theme cannot be checked until
		// every mesh has said what it wants. A mesh edited without its theme being
		// recompiled is the failure to watch for, which is why a mesh newer than a theme
		// makes that theme stale below.
		int64_t newestMesh = 0;
		std::vector<RDA::Layout::MeshThemeParam> meshParams;
		for (const fs::path& source : meshes) {
			if (isImported(importedMeshes, source)) { ++parts; continue; }
			const fs::path out = RDA::Layout::meshPathFor(source.string());
			// Compiled even when the .rdam is current, because what it declares is what
			// the themes are checked against and that cannot be read back out of a
			// skipped file. It is the cheapest step in the build by a wide margin.
			if (!stale(out, { source }, tool)) {
				++kept;
				newestMesh = (std::max)(newestMesh, stampOf(out.string()));
				const RDA::Layout::MeshCompileResult declared =
					RDA::Layout::compileMesh(source.string());
				if (declared.ok) {
					for (const RDA::Layout::MeshParamDecl& one : declared.params) {
						meshParams.push_back({ one.mesh + "." + one.name, one.doc,
						                       one.isColour ? RDA::Layout::ThemeFieldType::Colour
						                                    : RDA::Layout::ThemeFieldType::Number });
					}
				}
				continue;
			}
			const RDA::Layout::MeshCompileResult result =
				RDA::Layout::compileMeshToFile(source.string(), out.string());
			if (!result.ok) {
				std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
				++failed;
				continue;
			}
			std::printf("rda: %s -> %s  (%zu states, %zu vertices, %zu triangles)\n",
			            shown(source).c_str(), shown(out).c_str(),
			            result.stateCount, result.vertexCount, result.triangleCount);
			++made;
			newestMesh = (std::max)(newestMesh, stampOf(out.string()));
			for (const RDA::Layout::MeshParamDecl& one : result.params) {
				meshParams.push_back({ one.mesh + "." + one.name, one.doc,
				                       one.isColour ? RDA::Layout::ThemeFieldType::Colour
				                                    : RDA::Layout::ThemeFieldType::Number });
			}
		}

		// The schema the themes below are checked against is the engine's table merged
		// with this. Registered even when empty, so a run that removed the last mesh does
		// not leave the previous run's names accepted.
		RDA::Layout::registerMeshThemeParams(meshParams);

		// Themes next: the types read one, and a layout is checked against those.
		for (const fs::path& source : themes) {
			if (isImported(importedThemes, source)) { ++parts; continue; }
			const fs::path out = RDA::Layout::themePathFor(source.string());
			// A mesh that moved makes every theme stale, because a theme names meshes and
			// is checked against what they declare.
			if (!stale(out, { source }, (std::max)(tool, newestMesh))) { ++kept; continue; }
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

		// state.ts, read and its code compiled at most once per run: the types need the one
		// and the generated modules carry the other.
		RDA::Layout::StateSchema checkedState;
		bool stateChecked = false;

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
			// What state.ts imports too: an action's signature is read from it.
			if (haveState) {
				for (const std::string& one : RDA::Layout::importedFiles(state.string())) inputs.emplace_back(one);
				inputs.push_back(state);
			}
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
				// state.ts's code is checked whenever its types are: a mistake in a computed
				// value or an action is this build's to report, whether or not a backend
				// module was asked for.
				if (result.ok && haveState) {
					checkedState = RDA::Layout::loadStateSchema(state.string());
					stateChecked = checkedState.ok;
					if (stateChecked && !withStateCode(checkedState, state.string())) {
						stateChecked = false;
						++failed;
					}
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
				RDA::Layout::compileToFile(source.string(), out.string(), compileOptions());
			if (!result.ok) {
				std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
				++failed;
				continue;
			}
			std::printf("rda: %s -> %s  (%zu nodes, %zu bindings%s)\n", shown(source).c_str(),
			            shown(out).c_str(), result.nodeCount, result.bindingCount,
			            runtimeSummary(result).c_str());
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
			// What state.ts imports is part of it now: its code ships in these modules.
			std::vector<fs::path> stateInputs;
			for (const std::string& one : RDA::Layout::importedFiles(state.string())) stateInputs.emplace_back(one);
			if (stateInputs.empty()) stateInputs.push_back(state);
			for (const Emit& e : emits) {
				if (!e.wanted) continue;
				if (!stale(e.out, stateInputs, tool)) { ++kept; continue; }
				if (!loaded && stateChecked) {
					schema = checkedState;
					loaded = true;
				}
				if (!loaded) {
					schema = RDA::Layout::loadStateSchema(state.string());
					loaded = true;
					if (!schema.ok) {
						std::printf("rda: %s\n", schema.error.c_str());
						++failed;
						break;
					}
					if (!withStateCode(schema, state.string())) {
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
				std::printf("rda: %s -> %s  (%zu signals, %zu commands%s)\n", shown(state).c_str(),
				            shown(e.out).c_str(), schema.fields.size(), schema.commands.size(),
				            stateSummary(schema).c_str());
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

	// ---- prototype -----------------------------------------------------------------------
	//
	// A window with the layout in it and nothing behind it.
	//
	// An interface is written against state, tables and commands that a backend supplies,
	// which means that until somebody writes the backend there is nothing to look at: a
	// list with no rows is an empty box, and a detail screen with no signal set is a
	// screen full of zeroes. That is the wrong way round while the screen is the thing
	// being designed.
	//
	// So this defines everything the declaration promises, puts placeholder contents in
	// it, and opens the window. Editing a .tsx rebuilds it in place, the same hot reload
	// an application gets.
	//
	// The Router and the host outlive the call that makes them, so they live out here --
	// the same reason an application keeps its router at file scope.
	RDA::Layout::Router gPrototypeRouter;
	RDA::Layout::LayoutHost gPrototypeHost;
	bool gPrototypeRouted = false;
	// Every refusal happens inside onStart, which cannot return a code -- it can only
	// stop the loop. Without this the tool exited 0 after saying it would not do the
	// thing, which is the one way a message is worse than useless: a script believes it.
	bool gPrototypeRefused = false;

	void prototypeCommand(const char* name, const char* args) {
		// Printed rather than logged: this is the tool talking, not the engine, and the
		// whole point of pressing the button is to see that it asked for something.
		// Flushed, because stdout to a pipe is block-buffered and this process does not
		// exit -- it sits there with a window open, and a line nobody sees until then is
		// a line that told nobody anything. With what it was asked with: `[3,"x"]` is
		// printed as the call it was, commands.rename(3,"x").
		std::string inside = args ? args : "[]";
		if (inside.size() >= 2 && inside.front() == '[' && inside.back() == ']')
			inside = inside.substr(1, inside.size() - 2);
		std::printf("rda: commands.%s(%s)\n", name, inside.c_str());
		std::fflush(stdout);
	}

	// state.ts while a prototype runs: what it and its imports were when last read, so a
	// save is noticed and its code swapped in with every value kept.
	struct StateWatch {
		fs::path                                     file;
		std::vector<std::pair<fs::path, fs::file_time_type>> stamps;
		float                                        sinceCheck = 0.0f;

		void remember() {
			stamps.clear();
			std::error_code ec;
			for (const std::string& one : RDA::Layout::importedFiles(file.string())) {
				stamps.emplace_back(one, fs::last_write_time(one, ec));
			}
		}
		bool changed() {
			std::error_code ec;
			for (const auto& [path, when] : stamps) {
				if (fs::last_write_time(path, ec) != when) return true;
			}
			return false;
		}
	};
	StateWatch gStateWatch;

	// Reads state.ts again and swaps its code in. What it newly declares is defined; what
	// already existed keeps its value, which is the point of reloading rather than
	// restarting.
	void reloadPrototypeState() {
		gStateWatch.remember();
		RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(gStateWatch.file.string());
		if (!schema.ok) {
			std::printf("rda: state.ts did not reload: %s\n", schema.error.c_str());
			std::fflush(stdout);
			return;
		}
		RDA::Layout::definePrototypeState(schema, prototypeCommand, /*onlyNew*/ true);
		std::string error;
		if (!schema.hasCode()) {
			RDA::Layout::scriptHost().unloadState();
		} else if (!withStateCode(schema, gStateWatch.file.string()) ||
		           !RDA::Layout::scriptHost().loadState(schema.code.data(), schema.code.size(), error)) {
			if (!error.empty()) std::printf("rda: state.ts did not reload: %s\n", error.c_str());
			std::fflush(stdout);
			return;
		}
		std::printf("rda: state.ts reloaded%s\n", stateSummary(schema).c_str());
		std::fflush(stdout);
		rendeerRequestRedraw();
	}

	// `prototype` and `shot` are one command with two endings: everything up to the window
	// is identical -- compile the project, define the placeholder state, open the route or
	// the layout -- and then one waits for a person while the other takes a picture and
	// stops. One function is what keeps the second from drifting out of step with the first.
	int show(const std::vector<std::string>& args, bool shooting) {
		fs::path project = ".";
		std::string opening, dataFile;
		std::vector<std::string> sets, rows;
		bool compile = true;

		// Shot only.
		std::string out = "shot.png";
		unsigned width = 0, height = 0;
		float scale = 0.0f;
		int frames = 1, settleMs = 250, everyMs = 100;
		// A still shot waits for the interface to arrive: transitions ease over a theme's
		// transitionMs, and a window nobody presents runs the loop as fast as it can, so
		// without this a capture catches a hover a millisecond into a 160ms ease and the
		// picture shows a colour one step from where it started. A sequence is about motion
		// by definition, so it does not wait.
		bool still = true;
		bool stillSet = false;
		bool hovering = false, pressing = false;
		float pointerX = 0.0f, pointerY = 0.0f;
		float scrolling = 0.0f;
		// Chords to press before the picture, in order, one a frame: "--key Ctrl+S".
		std::vector<std::string> keys;
		bool holdDrag = false;   // --hold: picture a --drag before it is let go
		std::string a11yOut;     // --a11y: where to write the accessibility tree

		const auto number = [](const std::string& text, float& into) {
			try { into = std::stof(text); return true; } catch (...) { return false; }
		};

		for (size_t i = 1; i < args.size(); ++i) {
			const std::string& arg = args[i];
			if      (arg == "--set"    && i + 1 < args.size()) sets.push_back(args[++i]);
			else if (arg == "--rows"   && i + 1 < args.size()) rows.push_back(args[++i]);
			else if (arg == "--data"   && i + 1 < args.size()) dataFile = args[++i];
			else if (arg == "--layout" && i + 1 < args.size()) opening = args[++i];
			else if (arg == "--no-build") compile = false;
			else if (shooting && arg == "--out" && i + 1 < args.size()) out = args[++i];
			else if (shooting && arg == "--size" && i + 1 < args.size()) {
				const std::string size = args[++i];
				const size_t by = size.find_first_of("xX*");
				float w = 0.0f, h = 0.0f;
				if (by == std::string::npos || !number(size.substr(0, by), w)
				    || !number(size.substr(by + 1), h) || w < 1.0f || h < 1.0f) {
					std::printf("rda: --size wants WxH, like 1280x800 (got %s)\n", size.c_str());
					return 1;
				}
				width = static_cast<unsigned>(w);
				height = static_cast<unsigned>(h);
			}
			else if (shooting && arg == "--scale" && i + 1 < args.size()) {
				if (!number(args[++i], scale) || scale <= 0.0f) {
					std::printf("rda: --scale wants a number, like 1 or 1.5\n");
					return 1;
				}
			}
			else if (shooting && arg == "--frames" && i + 1 < args.size()) {
				float count = 0.0f;
				if (!number(args[++i], count) || count < 1.0f || count > 240.0f) {
					std::printf("rda: --frames wants 1 to 240\n");
					return 1;
				}
				frames = static_cast<int>(count);
			}
			else if (shooting && arg == "--every" && i + 1 < args.size()) {
				float ms = 0.0f;
				if (!number(args[++i], ms) || ms < 0.0f) {
					std::printf("rda: --every wants milliseconds\n");
					return 1;
				}
				everyMs = static_cast<int>(ms);
			}
			else if (shooting && arg == "--scroll" && i + 1 < args.size()) {
				if (!number(args[++i], scrolling)) {
					std::printf("rda: --scroll wants notches, negative for downward\n");
					return 1;
				}
			}
			else if (shooting && arg == "--key" && i + 1 < args.size()) {
				// Checked now rather than when it is sent, so a typo is a refusal before a
				// window opens instead of a picture of nothing having happened.
				RDA::Chord chord;
				std::string error;
				if (!RDA::parseChord(args[i + 1], chord, &error)) {
					std::printf("rda: --key '%s' %s\n", args[i + 1].c_str(), error.c_str());
					return 1;
				}
				keys.push_back(args[++i]);
			}
			// Text rather than a chord: what a field or a dropdown reads as typing. In
			// order with --key, so "--key Tab --type abc" is the keyboard reaching
			// something and then typing into it.
			else if (shooting && arg == "--type" && i + 1 < args.size()) {
				keys.push_back(std::string("\x01") + args[++i]);
			}
			// A drag from one point to another: down at the first, carried across in
			// steps -- a pick-up waits for the pointer to have moved, and a target is
			// found by where the pointer is on each frame -- and let go at the second,
			// unless --hold says to take the picture while it is still being carried.
			// In order with --key and --type, so a drag can be followed by Escape.
			else if (shooting && arg == "--drag" && i + 1 < args.size()) {
				const std::string spec = args[++i];
				float at[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				size_t from = 0;
				bool ok = true;
				for (int k = 0; k < 4 && ok; ++k) {
					const size_t comma = spec.find(',', from);
					const std::string part = spec.substr(from, comma == std::string::npos
					                                           ? std::string::npos : comma - from);
					ok = number(part, at[k]) && (k == 3 || comma != std::string::npos);
					from = (comma == std::string::npos) ? spec.size() : comma + 1;
				}
				if (!ok) {
					std::printf("rda: --drag wants X1,Y1,X2,Y2 in layout units, like 40,60,300,60\n");
					return 1;
				}
				auto step = [&](float x, float y, bool down) {
					keys.push_back(std::string("\x02") + std::to_string(x) + "," +
					               std::to_string(y) + "," + (down ? "1" : "0"));
				};
				step(at[0], at[1], false);
				step(at[0], at[1], true);
				constexpr int kSteps = 10;
				for (int k = 1; k <= kSteps; ++k) {
					const float t = static_cast<float>(k) / kSteps;
					step(at[0] + (at[2] - at[0]) * t, at[1] + (at[3] - at[1]) * t, true);
				}
				keys.push_back("\x03");   // let go, unless --hold
			}
			else if (shooting && arg == "--hold") holdDrag = true;
			// A click with Ctrl or Shift held through it, which a chord cannot do: a chord
			// holds its modifiers for one frame, and a click is a press and a release. In
			// order with --key and --drag, so a selection can be built a click at a time.
			else if (shooting && arg == "--click" && i + 1 < args.size()) {
				std::string spec = args[++i];
				bool ctrl = false, shift = false;
				for (;;) {
					if (spec.rfind("Ctrl+", 0) == 0)       { ctrl = true;  spec.erase(0, 5); }
					else if (spec.rfind("Shift+", 0) == 0) { shift = true; spec.erase(0, 6); }
					else break;
				}
				const size_t comma = spec.find(',');
				float x = 0.0f, y = 0.0f;
				if (comma == std::string::npos || !number(spec.substr(0, comma), x) ||
				    !number(spec.substr(comma + 1), y)) {
					std::printf("rda: --click wants X,Y in layout units, Ctrl+ and Shift+ in front "
					            "if they are held, like Ctrl+120,340\n");
					return 1;
				}
				const std::string at = std::to_string(x) + "," + std::to_string(y);
				keys.push_back(std::string("\x06") + (ctrl ? "1" : "0") + "," + (shift ? "1" : "0"));
				keys.push_back(std::string("\x02") + at + ",1");
				keys.push_back(std::string("\x02") + at + ",0");
				keys.push_back(std::string("\x06") + "0,0");
			}
			else if (shooting && arg == "--a11y" && i + 1 < args.size()) a11yOut = args[++i];
			// An input method part way through a word, in order with --key and --type, so
			// "--key Tab --compose nihao" reaches a field and starts composing in it.
			else if (shooting && arg == "--compose" && i + 1 < args.size()) {
				keys.push_back(std::string("\x05") + args[++i]);
			}
			// Files let go of where the pointer is -- put it there with --hover first.
			// Separated by ';', which is rarer in a path than a comma is.
			else if (shooting && arg == "--files" && i + 1 < args.size()) {
				keys.push_back(std::string("\x04") + args[++i]);
			}
			else if (shooting && arg == "--still")   { still = true;  stillSet = true; }
			else if (shooting && arg == "--moving")  { still = false; stillSet = true; }
			else if (shooting && arg == "--settle" && i + 1 < args.size()) {
				float count = 0.0f;
				if (!number(args[++i], count) || count < 0.0f) {
					std::printf("rda: --settle wants milliseconds\n");
					return 1;
				}
				settleMs = static_cast<int>(count);
			}
			else if (shooting && (arg == "--hover" || arg == "--press")
			         && i + 1 < args.size()) {
				const std::string at = args[++i];
				const size_t comma = at.find(',');
				if (comma == std::string::npos
				    || !number(at.substr(0, comma), pointerX)
				    || !number(at.substr(comma + 1), pointerY)) {
					std::printf("rda: %s wants X,Y in layout units, like 120,340\n",
					            arg.c_str());
					return 1;
				}
				hovering = true;
				pressing = (arg == "--press");
			}
			else if (!arg.empty() && arg[0] == '-') {
				std::printf("rda: unknown option %s\n", arg.c_str());
				return 1;
			}
			else project = arg;
		}

		std::error_code ec;
		project = fs::absolute(project, ec);
		const fs::path res = project / "res";
		if (!fs::is_directory(res, ec)) {
			std::printf("rda: no res/ directory in %s -- a project keeps its declaration, "
			            "theme and layouts there\n", shown(project).c_str());
			return 1;
		}

		// Compiled first, and by the same code `rda build` runs: a prototype of a layout
		// that has not been compiled since it was edited is a prototype of the wrong
		// thing.
		if (compile && build({ "build", project.string() }) != 0) return 1;

		const fs::path stateFile = res / "state.ts";
		RDA::Layout::StateSchema schema;
		if (fs::is_regular_file(stateFile, ec)) {
			schema = RDA::Layout::loadStateSchema(stateFile.string());
			if (!schema.ok) {
				std::printf("rda: %s\n", schema.error.c_str());
				return 1;
			}
			// state.ts's code runs here exactly as it would under a backend: compiled now,
			// loaded once its signals exist, and reloaded when it is saved.
			if (!withStateCode(schema, stateFile.string())) return 1;
			gStateWatch.file = stateFile;
			gStateWatch.remember();
		}

		const std::vector<fs::path> themes = filesIn(res / "themes", ".rdth");
		const std::vector<fs::path> blueprints = filesIn(res / "layouts", ".rdab");
		if (blueprints.empty()) {
			std::printf("rda: nothing to show -- %s holds no compiled layouts\n",
			            shown(res / "layouts").c_str());
			return 1;
		}

		// Everything a layout names -- an image, an icon, a mesh -- is relative to the
		// working directory, exactly as it will be when the application runs. So the tool
		// moves into the project rather than trying to rewrite paths it does not own.
		fs::current_path(project, ec);
		if (ec) {
			std::printf("rda: cannot work in %s\n", shown(project).c_str());
			return 1;
		}

		RDA::AppConfig config;
		config.app.name = (shooting ? "rda shot - " : "rda prototype - ")
		                + project.filename().string();
		config.app.appVersion = 1;
		config.gui.enabled = true;
		if (shooting) {
			// Built every frame and never shown. The window still exists, because the
			// device and the pipelines are chosen against a surface.
			config.offscreen = true;
			if (width > 0 && height > 0) {
				config.windowWidth = width;
				config.windowHeight = height;
			}
			// A shot says what it wants a unit to be worth, so two machines produce the
			// same picture. Left alone it asks the display, like anything else.
			if (scale > 0.0f) config.gui.uiScale = scale;
		}
		if (!themes.empty()) {
			config.gui.themePath = ("res/themes/" + themes.front().filename().string());
		}
		config.redrawMode = RDA::RedrawMode::OnDemand;
		config.viewportMode = RDA::ViewportMode::Fullscreen;
		config.threadMode = RDA::ThreadMode::Caller;

		// Captured by value and by reference deliberately: the callbacks run inside
		// rendeerRun, which returns after they are done with, so a reference to a local
		// that outlives the call is fine and copying the schema is not worth it.
		config.onStart = [&] {
			const RDA::Layout::PrototypeResult defined =
				RDA::Layout::definePrototypeState(schema, prototypeCommand);
			if (!defined.ok) {
				std::printf("rda: %s\n", defined.error.c_str());
				gPrototypeRefused = true;
				rendeerStop();
				return;
			}
			std::printf("rda: %zu signals, %zu tables, %zu commands%s\n",
			            defined.signals, defined.tables, defined.commands, stateSummary(schema).c_str());
			if (!schema.code.empty()) {
				std::string error;
				if (!RDA::Layout::scriptHost().loadState(schema.code.data(), schema.code.size(), error)) {
					std::printf("rda: %s\n", error.c_str());
					gPrototypeRefused = true;
					rendeerStop();
					return;
				}
			}

			// A file first, then generated rows, then single values: each is more
			// specific than the one before, so the last word goes to the shortest thing
			// somebody typed.
			if (!dataFile.empty()) {
				const RDA::Layout::PrototypeResult loaded =
					RDA::Layout::loadPrototypeData(dataFile);
				if (!loaded.ok) {
					std::printf("rda: %s\n%s\n", dataFile.c_str(), loaded.error.c_str());
					gPrototypeRefused = true;
				rendeerStop();
					return;
				}
				std::printf("rda: %s -- %zu signals, %zu tables, %zu rows\n",
				            dataFile.c_str(), loaded.signals, loaded.tables, loaded.rows);
			}
			for (const std::string& one : rows) {
				std::string error;
				if (!RDA::Layout::fillPrototypeRows(one, error)) {
					std::printf("rda: --rows %s: %s\n", one.c_str(), error.c_str());
					gPrototypeRefused = true;
				rendeerStop();
					return;
				}
			}
			for (const std::string& one : sets) {
				std::string error;
				if (!RDA::Layout::setPrototypeValue(one, error)) {
					std::printf("rda: --set %s: %s\n", one.c_str(), error.c_str());
					gPrototypeRefused = true;
				rendeerStop();
					return;
				}
			}

			RDA::Gui& gui = getMainWindow()->gui();

			if (!schema.routes.empty()) {
				// Declared routes, so the whole application's navigation works and
				// `--layout` picks which screen to open on.
				std::vector<RDA::Layout::Router::Route> routes;
				for (const RDA::Layout::StateRoute& declared : schema.routes) {
					RDA::Layout::Router::Route route;
					route.name = declared.name;
					route.layout = declared.layout;
					route.params = declared.params;
					routes.push_back(std::move(route));
				}
				if (!opening.empty()) {
					bool known = false;
					for (const auto& route : routes) known = known || route.name == opening;
					if (!known) {
						std::printf("rda: no route called `%s`\n", opening.c_str());
						gPrototypeRefused = true;
				rendeerStop();
						return;
					}
					RDA::signals().set(RDA::signals().find("route"), std::string_view(opening));
				}
				gPrototypeRouter.setTransitionMs(180.0f);
				if (!gPrototypeRouter.open(gui.retained(), std::move(routes),
				                           "res/layouts", "res/layouts")) {
					std::printf("rda: %s\n", gPrototypeRouter.lastError().c_str());
					gPrototypeRefused = true;
				rendeerStop();
					return;
				}
				gPrototypeRouted = true;
				std::printf("rda: showing %s -- edit a .tsx and it rebuilds\n",
				            gPrototypeRouter.current().c_str());
				std::fflush(stdout);
				return;
			}

			// No routes: one layout, named or the only one there is.
			std::string stem = opening;
			if (stem.empty()) {
				if (blueprints.size() > 1) {
					std::printf("rda: %zu layouts and no routes declared -- say which with "
					            "--layout <name>\n", blueprints.size());
					gPrototypeRefused = true;
				rendeerStop();
					return;
				}
				stem = blueprints.front().stem().string();
			}
			const std::string blueprint = "res/layouts/" + stem + ".rdab";
			const std::string source = "res/layouts/" + stem + ".tsx";
			if (!gPrototypeHost.open(gui.retained(), blueprint, source)) {
				std::printf("rda: %s\n", gPrototypeHost.lastError().c_str());
				gPrototypeRefused = true;
				rendeerStop();
				return;
			}
			std::printf("rda: showing %s -- edit it and it rebuilds\n", source.c_str());
			std::fflush(stdout);
		};

		// What the shot has done so far. Function-local rather than captured, because the
		// callback below outlives this scope only in the sense that rendeerRun has not
		// returned yet -- but a static says plainly that the loop owns it.
		int taken = 0;
		int tick = 0;
		size_t sentKeys = 0;
		float sinceStart = 0.0f;    // milliseconds the interface has been running
		float waited = 0.0f;        // since the last frame of a sequence
		float stillFor = 0.0f;      // how long nothing has moved
		// Set when it is time, acted on in onFrameEnd: a frame is not finished when
		// onUpdate runs, and the focus ring is one of the things added after it.
		bool captureNow = false;
		if (!stillSet) still = (frames == 1);

		config.onUpdate = [&](float dt) {
			// Both restructure the widget tree, which is why neither happens inside a
			// widget's own callback.
			if (gPrototypeRouted) gPrototypeRouter.update();
			else                  gPrototypeHost.reloadIfChanged();
			// state.ts, a few times a second rather than every frame: a stat of each file
			// it imports is cheap, and a save is a human pressing a key.
			if (!shooting && !gStateWatch.file.empty()) {
				gStateWatch.sinceCheck += dt;
				if (gStateWatch.sinceCheck >= 0.25f) {
					gStateWatch.sinceCheck = 0.0f;
					if (gStateWatch.changed()) reloadPrototypeState();
				}
			}
			if (!shooting || gPrototypeRefused) return;

			// The first frame's dt is not a frame's worth of time: it carries everything
			// between the clock starting and the interface existing -- compiling the
			// project, opening a window, baking a font -- which here was several seconds.
			// Counting it would satisfy any settle time before the first walk had happened,
			// and a shot would be of an interface that had not been hovered, pressed or
			// animated yet. So the opening frames are not counted, and no single frame may
			// contribute more than a frame plausibly takes.
			++tick;
			// Asked for from the first frame, so the tree exists by the time anything is
			// captured; it is built by the walk, and only once somebody wants it.
			if (tick == 1 && !a11yOut.empty()) getMainWindow()->describeForAccessibility();
			if (tick <= 2) return;
			const float ms = (std::min)(dt * 1000.0f, 100.0f);
			sinceStart += ms;

			// Held from the first frame, so a hovered widget is hovered by the time
			// anything is captured -- the GUI works out `hot` while it walks, which is
			// after this runs.
			//
			// And parked far outside the window when nothing was asked for. The window is
			// hidden, not absent: the real mouse still has a position inside it, so a shot
			// of a screen nobody is pointing at would hover whatever the pointer happened
			// to be over -- which made the same command produce different pictures on two
			// machines, and on one machine at two moments.
			if (hovering) {
				rendeerSetSyntheticPointer(pointerX, pointerY, pressing);
			} else if (scrolling != 0.0f) {
				// A wheel only reaches what the pointer is over, so asking to scroll and
				// saying nothing about the pointer means the middle of the window -- the
				// alternative was a parked pointer, a wheel nothing heard, and a picture
				// that silently had not scrolled.
				rendeerSetSyntheticPointer(static_cast<float>(width > 0 ? width : 1280) * 0.5f,
				                           static_cast<float>(height > 0 ? height : 800) * 0.5f,
				                           false);
			} else {
				rendeerSetSyntheticPointer(-10000.0f, -10000.0f, false);
			}
			// Held until the first picture, so a large number means "to the end". After
			// that it stops, or a sequence would keep scrolling between its frames.
			if (scrolling != 0.0f) rendeerSetSyntheticScroll(taken == 0 ? scrolling : 0.0f);

			// The keys, one a frame, before anything is counted as settled: a shortcut's
			// handler writes state, the binding applies on the frame after, and whatever
			// it set moving starts from there. So the settle time and the wait for
			// stillness both begin again once the last key is down.
			if (sentKeys < keys.size()) {
				const std::string& next = keys[sentKeys++];
				if (!next.empty() && next[0] == '\x02') {
					// A step of a drag. Kept in the pointer the frames after it, so the
					// pointer stays where the drag left it.
					float x = 0.0f, y = 0.0f;
					int down = 0;
					std::sscanf(next.c_str() + 1, "%f,%f,%d", &x, &y, &down);
					hovering = true;
					pointerX = x;
					pointerY = y;
					pressing = (down != 0);
					rendeerSetSyntheticPointer(pointerX, pointerY, pressing);
				}
				else if (!next.empty() && next[0] == '\x03') {
					pressing = holdDrag;
					rendeerSetSyntheticPointer(pointerX, pointerY, pressing);
				}
				else if (!next.empty() && next[0] == '\x04') {
					std::vector<std::string> paths;
					size_t from = 1;
					while (from <= next.size()) {
						const size_t semi = next.find(';', from);
						const std::string one = next.substr(from, semi == std::string::npos
						                                          ? std::string::npos : semi - from);
						if (!one.empty()) paths.push_back(one);
						if (semi == std::string::npos) break;
						from = semi + 1;
					}
					rendeerSyntheticDrop(paths);
				}
				else if (!next.empty() && next[0] == '\x06') {
					int ctrl = 0, shift = 0;
					std::sscanf(next.c_str() + 1, "%d,%d", &ctrl, &shift);
					rendeerSetSyntheticModifiers(ctrl != 0, shift != 0);
				}
				else if (!next.empty() && next[0] == '\x05') {
					// The method's cursor at the end of what it is composing, in characters.
					int characters = 0;
					for (size_t c = 1; c < next.size(); ++c)
						if ((static_cast<unsigned char>(next[c]) & 0xC0) != 0x80) ++characters;
					rendeerSyntheticComposition(next.c_str() + 1, characters);
				}
				else if (!next.empty() && next[0] == '\x01') rendeerSyntheticText(next.c_str() + 1);
				else                                     rendeerSyntheticChord(next.c_str());
				sinceStart = 0.0f;
				stillFor = 0.0f;
				return;
			}

			// The opening moments are thrown away on purpose: a layout's motion starts
			// from nothing and its first frame is a picture of an interface mid-arrival.
			if (sinceStart < static_cast<float>(settleMs)) return;

			// And then, for a still shot, until nothing is moving any more. Measured in
			// time rather than frames because a window nobody presents is not paced by
			// anything -- a thousand frames here can be a single millisecond of animation.
			if (still && taken == 0) {
				RDA::Gui& gui = getMainWindow()->gui();
				stillFor = gui.animating() ? 0.0f : (stillFor + ms);
				// Two frames' worth of quiet, so a transition that has just landed is not
				// caught one frame before its last step.
				if (stillFor < 32.0f && sinceStart < 4000.0f) return;
			}

			// Spaced in real time rather than by frames, for the same reason.
			waited += ms;
			if (taken > 0 && waited < static_cast<float>(everyMs)) return;
			waited = 0.0f;

			captureNow = true;
		};

		// The frame, whole. Everything above decided *whether* to take a picture;
		// this is the only moment at which there is a finished one to take.
		config.onFrameEnd = [&] {
			if (!captureNow) return;
			captureNow = false;
			std::vector<unsigned char> pixels;
			unsigned w = 0, h = 0;
			if (!rendeerCaptureFrame(pixels, w, h)) {
				std::printf("rda: the frame would not come back\n");
				gPrototypeRefused = true;
				rendeerStop();
				return;
			}

			// One frame keeps the name it was given; several are numbered, because a
			// sequence that overwrote itself would leave only its last frame.
			fs::path target = out;
			if (frames > 1) {
				const fs::path stem = target.stem();
				const fs::path ext = target.extension().empty() ? ".png" : target.extension();
				target = target.parent_path() / (stem.string() + "-" + std::to_string(taken)
				                                 + ext.string());
			}
			if (!RDA::Png::write(target.string(), pixels.data(), w, h)) {
				std::printf("rda: cannot write %s\n", target.string().c_str());
				gPrototypeRefused = true;
				rendeerStop();
				return;
			}
			std::printf("rda: %s  (%ux%u)\n", target.string().c_str(), w, h);
			std::fflush(stdout);

			// The tree, beside the picture of the same frame.
			if (!a11yOut.empty()) {
				const auto tree = getMainWindow()->gui().accessibleTree();
				std::ofstream text(a11yOut, std::ios::trunc);
				if (tree && text) {
					text << RDA::formatAccessibleTree(*tree);
					std::printf("rda: %s  (%zu nodes)\n", a11yOut.c_str(), tree->nodes.size());
				} else {
					std::printf("rda: no accessibility tree to write to %s\n", a11yOut.c_str());
				}
			}

			if (++taken >= frames) rendeerStop();
		};

		config.onShutdown = [] {
			gPrototypeRouter = RDA::Layout::Router{};
		};

		std::fflush(stdout);
		rendeerRun(config);
		if (shooting && !gPrototypeRefused && taken == 0) {
			std::printf("rda: nothing was captured -- the window closed before the first "
			            "frame settled\n");
			return 1;
		}
		return gPrototypeRefused ? 1 : 0;
	}

	int prototype(const std::vector<std::string>& args) { return show(args, false); }
	int shot(const std::vector<std::string>& args)      { return show(args, true); }

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

	// `--mesh <file.ts>`, repeatable. Taken out before anything positional is read, so it
	// can be written anywhere on the line and the commands that ignore it do not have to
	// know it exists.
	std::vector<std::string> meshes;
	for (size_t i = 0; i < args.size();) {
		if (args[i] == "--no-runtime-code") {
			gNoRuntimeCode = true;
			args.erase(args.begin() + static_cast<ptrdiff_t>(i));
		} else if (args[i] == "--mesh" && i + 1 < args.size()) {
			meshes.push_back(args[i + 1]);
			args.erase(args.begin() + static_cast<ptrdiff_t>(i),
			           args.begin() + static_cast<ptrdiff_t>(i) + 2);
		} else {
			++i;
		}
	}

	if (args.empty()) { usage(); return 1; }

	const std::string command = args[0];
	const std::string first   = args.size() > 1 ? args[1] : std::string();
	const std::string second  = args.size() > 2 ? args[2] : std::string();

	if (command == "build")  return build(args);
	if (command == "layout" && !first.empty()) return layout(first, second);
	const std::string third = args.size() > 3 ? args[3] : std::string();
	if (command == "types"  && !first.empty()) {
		// Before the emitter, so the generated .d.ts carries the fields a mesh asked for
		// as well as the ones the engine has.
		if (!registerMeshes(meshes)) return 1;
		return types(first, second, third);
	}
	if (command == "state"  && !first.empty() && !second.empty()) return state(first, second);
	if (command == "python" && !first.empty() && !second.empty()) return python(first, second);
	if (command == "node" && !first.empty() && !second.empty()) return node(first, second);
	if (command == "csharp" && !first.empty() && !second.empty()) return csharp(first, second);
	if (command == "theme"  && !first.empty()) {
		// Before the compiler, because the schema it checks the theme against is the
		// engine's table merged with this.
		if (!registerMeshes(meshes)) return 1;
		return theme(first, second);
	}
	if (command == "mesh"   && !first.empty()) return mesh(first, second);
	if (command == "dump"   && !first.empty()) return dump(first);
	if (command == "prototype") return prototype(args);
	if (command == "shot") return shot(args);

	usage();
	return 1;
}
