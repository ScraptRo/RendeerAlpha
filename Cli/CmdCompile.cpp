// The commands that compile one source file: a layout, a theme, a mesh -- and the two that
// read the result back or write what layouts are checked against, `types` and `dump`.
#include "Commands.h"

#include <Layout/Expression.h>
#include <Layout/ThemeCompiler.h>
#include <Layout/TypeEmitter.h>

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace Cli {

	namespace {

		int compileLayout(const std::string& source, const std::string& output) {
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
				addMeshParams(params, declared.params);
			}
			// Registered even when empty, so a command given no meshes is checked against the
			// engine's table alone rather than against whatever a previous call left behind.
			RDA::Layout::registerMeshThemeParams(params);
			return true;
		}

		int compileTheme(const std::string& source, const std::string& output) {
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
		// see `build` for the ordering that follows from that.
		int compileMesh(const std::string& source, const std::string& output) {
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

		int emitTypes(const std::string& output, const std::string& theme, const std::string& stateFile) {
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

		// ---- dump: a blueprint, in words ---------------------------------------------------

		void printScript(const RDA::Layout::Blueprint& blueprint) {
			const RDA::Layout::BlueprintScript& script = blueprint.script();
			size_t lines = script.lineMap.empty() ? 0 : 1;
			for (const char c : script.lineMap) if (c == '\n') ++lines;
			std::printf("run-time code: %zu bytes of QuickJS %s bytecode, %zu lines mapped to source\n",
			            script.code.size(), script.engine.c_str(), lines);
		}

		void printNodes(const RDA::Layout::Blueprint& blueprint) {
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
		}

		void printBindings(const RDA::Layout::Blueprint& blueprint) {
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
		}

		int dumpBlueprint(const std::string& path) {
			RDA::Layout::Blueprint blueprint;
			std::string error;
			if (!RDA::Layout::loadBlueprintFile(path, blueprint, error)) {
				std::printf("rda: %s\n", error.c_str());
				return 1;
			}

			std::printf("%s: %zu nodes, %zu bindings, %zu bytes\n",
			            path.c_str(), blueprint.nodeCount(), blueprint.bindingCount(),
			            blueprint.byteSize());
			if (blueprint.hasScript()) printScript(blueprint);
			std::printf("\n");

			printNodes(blueprint);
			printBindings(blueprint);
			return 0;
		}
	}

	int cmdLayout(const Args& args) {
		return compileLayout(argument(args, 1), argument(args, 2));
	}

	int cmdTheme(const Args& args) {
		// Before the compiler, because the schema it checks the theme against is the
		// engine's table merged with this.
		if (!registerMeshes(globalOptions().meshes)) return 1;
		return compileTheme(argument(args, 1), argument(args, 2));
	}

	int cmdMesh(const Args& args) {
		return compileMesh(argument(args, 1), argument(args, 2));
	}

	int cmdTypes(const Args& args) {
		// Before the emitter, so the generated .d.ts carries the fields a mesh asked for
		// as well as the ones the engine has.
		if (!registerMeshes(globalOptions().meshes)) return 1;
		return emitTypes(argument(args, 1), argument(args, 2), argument(args, 3));
	}

	int cmdDump(const Args& args) {
		return dumpBlueprint(argument(args, 1));
	}
}
