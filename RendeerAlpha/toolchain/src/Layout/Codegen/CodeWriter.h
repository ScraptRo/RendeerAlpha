#pragma once
#include <Layout/StateSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What the four state generators have in common.
//
// A declaration becomes C++, Python, JavaScript and C#, and each of those is written by
// printing. What they share is the part that has no business being written four times:
// getting a string into a literal without breaking it, getting bytes into text, spelling a
// name the way the generated code calls it, and putting the finished file where it goes.
//
// They used to be copies of each other, and the copies drifted. The C++ one escaped a
// newline in a text default and the other three did not, so `title: "a\nb"` generated a
// Python module that would not import, and a Node and a C# file that would not compile.
// Written once here, a language that needs a different rule says so in one place.
//
// What is not here is anything that belongs to one language: its types, its accessors, how
// it spells a boolean, how it quotes a doc comment. Those stay with the generator, because
// a shared version of them is only a place for the languages to be confused with each other.
namespace RDA::Layout::Codegen {

	// The languages a string literal can be written for.
	enum class Language {
		Cpp,
		Python,
		JavaScript,
		CSharp,
	};

	// The text as it is written between the quotes of a double-quoted literal in the
	// language, without the quotes.
	//
	// A backslash and a quote are escaped everywhere. So is every character that would end
	// the line the literal sits on or be unreadable in a diff: \n, \r and \t by name, and
	// any other control character by number, spelled the way the language reads it back --
	// octal for C++, whose \x takes as many digits as it is given and would swallow a digit
	// that follows; \xNN for Python and JavaScript; \u00NN for C#, whose \x does the same
	// thing C++'s does. Beyond ASCII the text is left as it is, because every one of them
	// reads UTF-8 -- except the Unicode line separators, which C# does not allow in a
	// literal at all and JavaScript only allows from ES2019.
	std::string escapeString(std::string_view text, Language language);

	// The same, with the quotes.
	std::string quoteString(std::string_view text, Language language);

	// state.ts's code as text a generated file can carry: base64, which Python, Node and
	// C# all decode with what they already have.
	std::string base64(const std::vector<uint8_t>& bytes);

	// The same, in lines short enough to read past -- for a language whose string literals
	// concatenate across lines.
	std::vector<std::string> base64Lines(const std::vector<uint8_t>& bytes);

	// "products" -> "Products". Only the first letter moves; a name already capitalised
	// keeps its shape.
	std::string capitalised(const std::string& name);

	// "products" -> "ProductsRow"
	std::string rowStructName(const std::string& table);

	// A command is registered rather than assigned, so it reads as one: onSave(...) says
	// what the function is for, where setSave(...) would suggest it holds a value.
	// "save" -> "onSave"
	std::string handlerName(const std::string& name);

	// "count" -> "setCount"
	std::string setterName(const std::string& field);

	// The number a generated module hands the engine to say what a column holds. It is the
	// engine's ColumnType, which the toolchain is meant not to depend on, so the numbers
	// are repeated here and a test holds the two together.
	int columnKind(StateType type);

	// A declaration that has code and none compiled is a generator called the wrong way
	// round, and a module generated from it would define computed signals that never
	// compute. Refused where it would happen.
	bool codeReady(const StateSchema& schema, std::string& error);

	// Writes a generated file, making the directories above it first. Generated output
	// usually lands in a build directory that does not exist yet, so making it is part of
	// writing the file rather than the caller's job.
	bool writeTextFile(const std::string& path, const std::string& text, std::string& error);
}
