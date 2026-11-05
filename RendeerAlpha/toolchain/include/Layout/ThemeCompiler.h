#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Compiling a theme written in TypeScript.
//
// A theme was XML, one tag pair per scalar, with a `base=` attribute for the one thing
// XML cannot express -- inheritance. TextFieldStyle alone has twenty fields, and a
// SyntaxStyle nine colours, so a real theme in that format is unmaintainable by about
// the third variant.
//
// As TypeScript it is object literals:
//
//   const primary = { normal: "#3A6AD0", hovered: "#4C7CE6", radius: 6 };
//   export const button = {
//     primary,
//     danger: { ...primary, normal: "#B23A3A", text: "#FFECEC" },
//   };
//
// `base=` becomes object spread, which is a language feature rather than a parser
// feature, and partial overrides work because that is simply what spread does. Colours
// can be computed. The names are checked while typing.
//
// The output is a Blueprint. A theme entry is an element with a variant name and a set
// of fields, which is exactly the shape a blueprint node already has -- so this needs no
// new file format, and reuses a reader whose bounds are already validated.
namespace RDA::Layout {

	struct ThemeCompileResult {
		bool                 ok = false;
		std::vector<uint8_t> bytes;
		std::string          error;
		size_t               variantCount = 0;
		double               transformMs = 0.0;
		double               evaluateMs = 0.0;

		// Every (element, variant) it declared, so the type emitter can build its unions
		// without evaluating the same file a second time.
		std::vector<std::pair<std::string, std::string>> variants;
	};

	ThemeCompileResult compileTheme(const std::string& sourcePath,
	                                const std::string& esbuildPath = {});

	ThemeCompileResult compileThemeToFile(const std::string& sourcePath,
	                                      const std::string& outputPath,
	                                      const std::string& esbuildPath = {});

	// Where a compiled theme conventionally lives: the source path with .rdth instead.
	std::string themePathFor(const std::string& sourcePath);
}
