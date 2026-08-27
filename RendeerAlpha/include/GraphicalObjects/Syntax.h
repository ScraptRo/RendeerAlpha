#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace RDA {

	// Syntax highlighting: a data-driven description of a language, plus a tokenizer
	// that turns source text into colored spans. Deliberately not a real parser — it is
	// a lexer good enough for an editor view: comments, strings, numbers, keywords,
	// operators and call-like identifiers. That keeps one implementation working for
	// every language instead of one hand-written highlighter per language.
	//
	// Languages are values: register them from code (SyntaxRegistry::define) or load
	// them from XML (loadFromFile). "python" is built in.
	struct Language {
		std::string name;
		std::unordered_set<std::string> keywords; // control flow, declarations, ...
		std::unordered_set<std::string> types;    // builtin types + constants

		// Comment syntax. Every entry in lineComments runs to end of line; a block
		// comment spans lines (both delimiters must be non-empty to be used).
		std::vector<std::string> lineComments;
		std::string blockCommentBegin;
		std::string blockCommentEnd;

		// String delimiters, longest-first at match time so "\"\"\"" wins over "\"".
		// A multi-line delimiter (len > 1) may span lines; a single-char one ends at the
		// newline, so one stray quote cannot swallow the rest of the file.
		std::vector<std::string> stringDelims{ "\"", "'" };
		// Delimiters that span lines even though they are a single character —
		// JavaScript's backtick template literal being the reason this exists. Multi-
		// character delimiters (Python's triple quote) already span lines by virtue of
		// their length, so they belong in stringDelims above.
		std::vector<std::string> multilineStringDelims;
		bool escapeWithBackslash = true;

		// Optional prefix that marks a preprocessor/decorator run (C's "#", Python's
		// "@"). Only recognized where a token may start. Empty = unused.
		std::string preprocessorPrefix;

		bool caseSensitive = true;
	};

	// A classified span of the source: [begin, end) in bytes. Only non-Plain spans are
	// produced; anything not covered draws in the field's plain text color.
	struct Token {
		int       begin = 0;
		int       end = 0;
		TokenKind kind = TokenKind::Plain;
	};

	// The set of languages a Gui knows about, by name.
	class SyntaxRegistry {
	public:
		SyntaxRegistry(); // registers the built-in "python"

		// Registers or replaces a language under lang.name.
		void define(const Language& lang);
		// The named language, or nullptr when it was never registered.
		const Language* find(const std::string& name) const;
		bool has(const std::string& name) const { return find(name) != nullptr; }

		// Merge language definitions from an XML file / string (see Syntax.cpp for the
		// format). Returns false on a parse or file error.
		bool loadFromFile(const std::string& path);
		bool loadFromString(const char* xml);

		// Lexes `text` into `out` (cleared first), sorted by begin and non-overlapping.
		static void tokenize(const Language& lang, const std::string& text, std::vector<Token>& out);

		// The built-in Python definition, also usable as a base for derived languages.
		static Language python();

	private:
		std::unordered_map<std::string, Language> mLanguages;
	};
}
