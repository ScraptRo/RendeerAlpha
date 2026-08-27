// rda — the layout toolchain.
//
//   rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint
//   rda types  <out.d.ts> [theme.xml]  emit the TypeScript definitions layouts use
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

#include <cstdio>
#include <string>
#include <vector>

namespace {

	void usage() {
		std::printf(
			"rda - the Rendeer layout toolchain\n\n"
			"  rda layout <file.tsx> [out.rdab]   compile a layout into a blueprint\n"
			"  rda types  <out.d.ts> [theme.xml]  emit the TypeScript definitions\n\n"
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
		            "     %zu nodes, %zu bytes  (transform %.1f ms, evaluate %.1f ms)\n",
		            source.c_str(), target.c_str(), result.nodeCount,
		            result.bytes.size(), result.transformMs, result.evaluateMs);
		return 0;
	}

	int types(const std::string& output, const std::string& theme) {
		const std::string target = output.empty() ? std::string("rda.d.ts") : output;

		const RDA::Layout::TypesResult result = RDA::Layout::emitTypeDefinitions(target, theme);
		if (!result.ok) {
			std::printf("rda: %s\n", result.error.c_str());
			return 1;
		}
		std::printf("rda: wrote %s\n"
		            "     %zu elements, %zu theme variants, %zu bytes\n",
		            target.c_str(), result.widgetCount, result.variantCount, result.byteSize);
		if (theme.empty()) {
			std::printf("     (no theme given, so variants are plain strings)\n");
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
	if (command == "types"  && !first.empty()) return types(first, second);

	usage();
	return 1;
}
