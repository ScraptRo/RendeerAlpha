#include "TestHarness.h"
#include <Core/Tables.h>
#include <Layout/Codegen/CodeWriter.h>
#include <Layout/StateSchema.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// The state generators, and the one writer they share.
//
// These build a StateSchema by hand rather than reading a state.ts, so nothing here needs
// esbuild or QuickJS: what is checked is what each generator prints for a declaration, which
// is the half that used to disagree with itself. A text default holding a newline was
// escaped for C++ and written raw into the Python, Node and C# files, where it ended the
// string at the end of the line.
//
// The generated files are read back from disk rather than taken from a function's return
// value, because writing them -- the directories above, the text mode -- is part of what a
// generator does.
using namespace RDA::Layout;
using namespace RDA::Layout::Codegen;

namespace {

	// What a text default can hold that a literal has to survive: the quote that would end
	// it, the backslash that would begin an escape, a newline, a return and a tab that would
	// break the line or hide in it, and a control character with no name of its own. It is
	// followed by "7b" because the escape for that character must not be able to take them
	// as more of itself: octal reads up to three digits and a C# hex escape up to four, and
	// both are exactly what a lazy spelling gets wrong.
	const std::string kAwkward = std::string("a\"b\\c\nd\re\tf\x01") + "7b";

	std::filesystem::path scratch() {
		return std::filesystem::temp_directory_path() / "rda_codegen_tests";
	}

	StateSchema schemaWithTitle(const std::string& value) {
		StateSchema schema;
		schema.ok = true;
		schema.source = "state.ts";
		StateField title;
		title.name = "title";
		title.type = StateType::Text;
		title.value = value;
		title.doc = "shown in the title bar";
		schema.fields.push_back(title);
		return schema;
	}

	using Emit = bool (*)(const StateSchema&, const std::string&, std::string&);

	// Runs a generator into a directory that does not exist yet, and returns the file.
	std::string generate(Emit emit, const StateSchema& schema, const char* name) {
		const std::filesystem::path path = scratch() / "out" / name;
		std::string error;
		const bool ok = emit(schema, path.string(), error);
		CHECK(ok);
		CHECK_STR(error, "");
		std::ifstream in(path);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	bool contains(const std::string& text, const std::string& part) {
		return text.find(part) != std::string::npos;
	}

	// The line of generated text that holds `part`, without its line ending; empty when none
	// does.
	std::string lineWith(const std::string& text, const std::string& part) {
		const size_t at = text.find(part);
		if (at == std::string::npos) return "";
		const size_t before = text.rfind('\n', at);
		const size_t first = before == std::string::npos ? 0 : before + 1;
		size_t last = text.find('\n', at);
		if (last == std::string::npos) last = text.size();
		std::string line = text.substr(first, last - first);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		return line;
	}
}

TEST(a_literal_escapes_what_would_end_or_break_it_in_every_language) {
	// Written as the generated source reads. The same eight characters, four spellings.
	CHECK_STR(escapeString(kAwkward, Language::Cpp),        R"x(a\"b\\c\nd\re\tf\0017b)x");
	CHECK_STR(escapeString(kAwkward, Language::Python),     R"x(a\"b\\c\nd\re\tf\x017b)x");
	CHECK_STR(escapeString(kAwkward, Language::JavaScript), R"x(a\"b\\c\nd\re\tf\x017b)x");
	CHECK_STR(escapeString(kAwkward, Language::CSharp),     R"x(a\"b\\c\nd\re\tf\u00017b)x");

	CHECK_STR(quoteString("a\nb", Language::Python), "\"a\\nb\"");
	CHECK_STR(quoteString("", Language::Cpp), "\"\"");
}

TEST(a_literal_with_nothing_to_escape_is_left_as_it_was) {
	// Including what is not ASCII: all four read UTF-8, so a word in another script is not
	// something to turn into numbers.
	const std::string plain = "Sandbox \xC3\xA9 \xE2\x82\xAC 'single' {braces}";
	CHECK_STR(escapeString(plain, Language::Cpp), plain);
	CHECK_STR(escapeString(plain, Language::Python), plain);
	CHECK_STR(escapeString(plain, Language::JavaScript), plain);
	CHECK_STR(escapeString(plain, Language::CSharp), plain);
}

TEST(a_unicode_line_separator_is_escaped_where_the_language_ends_a_line_at_it) {
	// U+2028 and U+0085. C# treats both as the end of a line, and so does a JavaScript
	// before ES2019 for the first; Python and C++ do not care.
	const std::string separators = "x\xE2\x80\xA8y\xC2\x85z";
	CHECK_STR(escapeString(separators, Language::CSharp), R"x(x\u2028y\u0085z)x");
	CHECK_STR(escapeString(separators, Language::JavaScript), std::string("x\\u2028y\xC2\x85z"));
	CHECK_STR(escapeString(separators, Language::Python), separators);
	CHECK_STR(escapeString(separators, Language::Cpp), separators);

	// A lead byte alone is not a separator, and neither is the start of another character
	// that shares it: U+2030 begins E2 80 as well.
	const std::string permille = "\xE2\x80\xB0";
	CHECK_STR(escapeString(permille, Language::CSharp), permille);
	CHECK_STR(escapeString("\xE2\x80", Language::CSharp), "\xE2\x80");
}

// The whole line that defines the signal, compared as one: a raw newline in the literal would
// cut it short, and the comparison then shows where.
TEST(a_text_default_with_a_newline_is_escaped_in_the_cpp_header) {
	const std::string out = generate(&emitStateHeader, schemaWithTitle(kAwkward), "RdaState.h");
	CHECK_STR(lineWith(out, "ids().title ="),
	          "\t\tids().title = signals().define(\"title\", std::string_view("
	          + std::string(R"x("a\"b\\c\nd\re\tf\0017b")x") + "));");
}

TEST(a_text_default_with_a_newline_is_escaped_in_the_python_module) {
	const std::string out = generate(&emitStatePython, schemaWithTitle(kAwkward), "state.py");
	CHECK_STR(lineWith(out, "define_text(\"title\""),
	          "\t_engine.define_text(\"title\", " + std::string(R"x("a\"b\\c\nd\re\tf\x017b")x") + ")");
}

TEST(a_text_default_with_a_newline_is_escaped_in_the_node_module) {
	const std::string out = generate(&emitStateNode, schemaWithTitle(kAwkward), "state.mjs");
	CHECK_STR(lineWith(out, "defineText('title'"),
	          "\tengine.defineText('title', " + std::string(R"x("a\"b\\c\nd\re\tf\x017b")x") + ")");
}

TEST(a_text_default_with_a_newline_is_escaped_in_the_csharp_file) {
	const std::string out = generate(&emitStateCSharp, schemaWithTitle(kAwkward), "state.cs");
	CHECK_STR(lineWith(out, "DefineText(\"title\""),
	          "\t\t\tEngine.DefineText(\"title\", " + std::string(R"x("a\"b\\c\nd\re\tf\u00017b")x") + ");");
}

TEST(a_declaration_with_no_commands_still_has_the_name_an_application_imports) {
	// The line an application opens with imports Commands, and it is there before the first
	// command is. The name not existing was an ImportError on the very first run.
	const StateSchema schema = schemaWithTitle("x");
	CHECK(schema.commands.empty());

	const std::string python = generate(&emitStatePython, schema, "state.py");
	CHECK(contains(python, "class _Commands:"));
	CHECK(contains(python, "Commands = _Commands()"));
	CHECK(contains(python, "def call(name: str, *args):"));

	const std::string node = generate(&emitStateNode, schema, "state.mjs");
	CHECK(contains(node, "export const Commands = Object.freeze({"));
	CHECK(contains(node, "export function call(name, ...args)"));

	const std::string csharp = generate(&emitStateCSharp, schema, "state.cs");
	CHECK(contains(csharp, "public static class Commands {"));
	CHECK(contains(csharp, "public static JsonElement Call(string name"));
}

TEST(an_ordinary_text_default_generates_what_it_always_did) {
	const StateSchema schema = schemaWithTitle("Sandbox");
	CHECK(contains(generate(&emitStateHeader, schema, "plain.h"),
	               "signals().define(\"title\", std::string_view(\"Sandbox\"));"));
	CHECK(contains(generate(&emitStatePython, schema, "plain.py"),
	               "\t_engine.define_text(\"title\", \"Sandbox\")\n"));
	CHECK(contains(generate(&emitStateNode, schema, "plain.mjs"),
	               "\tengine.defineText('title', \"Sandbox\")\n"));
	CHECK(contains(generate(&emitStateCSharp, schema, "plain.cs"),
	               "\t\t\tEngine.DefineText(\"title\", \"Sandbox\");\n"));
}

TEST(a_declaration_with_code_that_was_not_compiled_is_refused_by_every_generator) {
	StateSchema schema = schemaWithTitle("x");
	schema.fields[0].computed = true; // so hasCode(), with no code beside it
	const Emit all[] = { &emitStateHeader, &emitStatePython, &emitStateNode, &emitStateCSharp };
	for (const Emit emit : all) {
		std::string error;
		const std::filesystem::path path = scratch() / "refused" / "never.txt";
		CHECK(!emit(schema, path.string(), error));
		CHECK(contains(error, "its code was not compiled"));
		CHECK(!std::filesystem::exists(path));
	}
}

TEST(a_column_kind_is_the_number_the_engine_gives_its_column_type) {
	// The generated modules hand the engine a bare number; if ColumnType were reordered,
	// every one of them would define its columns as the wrong type and nothing would say so.
	CHECK_EQ(columnKind(StateType::Number), static_cast<int>(RDA::ColumnType::Number));
	CHECK_EQ(columnKind(StateType::Bool),   static_cast<int>(RDA::ColumnType::Bool));
	CHECK_EQ(columnKind(StateType::Text),   static_cast<int>(RDA::ColumnType::Text));
}

TEST(a_name_is_capitalised_where_the_generated_code_calls_for_it) {
	CHECK_STR(capitalised("products"), "Products");
	CHECK_STR(capitalised("Products"), "Products");
	CHECK_STR(capitalised("_hidden"), "_hidden");
	CHECK_STR(capitalised(""), "");
	CHECK_STR(setterName("count"), "setCount");
	CHECK_STR(handlerName("save"), "onSave");
	CHECK_STR(rowStructName("products"), "ProductsRow");
}

TEST(base64_matches_the_published_vectors_and_breaks_lines_at_76) {
	const auto bytes = [](const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); };
	CHECK_STR(base64(bytes("")), "");
	CHECK_STR(base64(bytes("M")), "TQ==");
	CHECK_STR(base64(bytes("Ma")), "TWE=");
	CHECK_STR(base64(bytes("Man")), "TWFu");
	CHECK_STR(base64(bytes("Many hands make light work.")), "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu");

	// 100 bytes is 136 characters: one full line and 60.
	const std::vector<std::string> lines = base64Lines(std::vector<uint8_t>(100, 0xAB));
	CHECK_EQ(lines.size(), size_t(2));
	if (lines.size() == 2) {
		CHECK_EQ(lines[0].size(), size_t(76));
		CHECK_EQ(lines[1].size(), size_t(60));
	}
}

TEST(a_generated_file_is_written_into_directories_that_did_not_exist) {
	std::filesystem::remove_all(scratch());
	const std::filesystem::path path = scratch() / "a" / "b" / "c.txt";
	std::string error;
	CHECK(writeTextFile(path.string(), "hello\n", error));
	CHECK_STR(error, "");
	CHECK(std::filesystem::exists(path));

	// A path that cannot be a file says so, with the path, rather than failing quietly.
	CHECK(!writeTextFile(scratch().string(), "x", error));
	CHECK(contains(error, "cannot write"));

	std::filesystem::remove_all(scratch());
}
