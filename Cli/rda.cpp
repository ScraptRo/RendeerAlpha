// rda — the layout toolchain.
//
//   rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint
//   rda types  <out.d.ts> [theme.xml]  emit the TypeScript definitions layouts use
//   rda theme  <theme.ts> [out.rdth]   compile a theme
//   rda state  <state.xml> <out.h>     generate the C++ for a state declaration
//   rda dump   <file.rdab>             print what a compiled layout contains
//
// Two commands, both compilers. Everything else this program used to do — scaffolding a
// project, building it, running it — is a script's job now, and lives in scripts/.
// Those tasks are shell work wearing a C++ coat; these two are not, because they need
// the widget schema and a JavaScript engine to evaluate a layout once.
//
// The intent is that a build invokes this rather than a person doing so by hand, the way
// nobody thinks about compiling shaders. Running it directly is for when something went
// wrong and the timings and node counts matter.
#include <Core/BuildMode.h>
#include <Layout/LayoutCompiler.h>
#include <Layout/TypeEmitter.h>
#include <Layout/StateSchema.h>
#include <Layout/ThemeCompiler.h>
#include <Layout/Expression.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

	void usage() {
		std::printf(
			"rda - the Rendeer layout toolchain\n\n"
			"  rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint\n"
			"  rda types  <out.d.ts> [theme] [state]  emit the TypeScript definitions\n"
			"  rda theme  <theme.ts> [out.rdth]   compile a theme\n"
			"  rda state  <state.xml> <out.h>     generate the C++ for a state declaration\n"
			"  rda dump   <file.rdab>             print what a compiled layout contains\n\n"
			"A layout is TypeScript with JSX in it. Compiling turns it into a flat file of\n"
			"nodes and strings that an application reads without a parser of any kind.\n");
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

	if (command == "layout" && !first.empty()) return layout(first, second);
	const std::string third = args.size() > 3 ? args[3] : std::string();
	if (command == "types"  && !first.empty()) return types(first, second, third);
	if (command == "state"  && !first.empty() && !second.empty()) return state(first, second);
	if (command == "theme"  && !first.empty()) return theme(first, second);
	if (command == "dump"   && !first.empty()) return dump(first);

	usage();
	return 1;
}
