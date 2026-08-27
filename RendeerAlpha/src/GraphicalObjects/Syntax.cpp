#include <GraphicalObjects/Syntax.h>
#include <Logger/Logger.h>
#include <vendor/tinyxml2/tinyxml2.h>
#include <algorithm>
#include <cctype>
#include <cstring>

// Syntax highlighting. One data-driven lexer serves every language: the Language
// struct says what a comment / string / keyword looks like, and tokenize() walks the
// text once producing colored spans. Languages load from XML:
//
//   <languages>
//     <language name="cpp">
//       <keywords>if else for while return class struct namespace</keywords>
//       <types>int float double bool char void size_t</types>
//       <lineComment>//</lineComment>
//       <blockComment begin="/*" end="*/"/>
//       <string>"</string>
//       <string>'</string>
//       <preprocessor>#</preprocessor>
//     </language>
//   </languages>
//
// A language may derive from another with base="name" and override only what differs.
namespace RDA {

	using tinyxml2::XMLDocument;
	using tinyxml2::XMLElement;

	namespace {
		bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
		bool isIdentChar(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
		bool isDigit(char c)      { return c >= '0' && c <= '9'; }

		bool isOperatorChar(char c) {
			return std::strchr("+-*/%=<>!&|^~?:;,.()[]{}", c) != nullptr;
		}

		std::string toLower(std::string s) {
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// Does `text` contain `what` at index i?
		bool matchAt(const std::string& text, int i, const std::string& what) {
			if (what.empty()) return false;
			if (i + static_cast<int>(what.size()) > static_cast<int>(text.size())) return false;
			return std::memcmp(text.data() + i, what.data(), what.size()) == 0;
		}

		// Is everything from the start of this line up to i whitespace? Used to keep a
		// preprocessor prefix ("#", "@") from matching mid-expression.
		bool onlyBlanksBefore(const std::string& text, int i) {
			for (int j = i - 1; j >= 0 && text[j] != '\n'; --j) {
				if (!std::isspace(static_cast<unsigned char>(text[j]))) return false;
			}
			return true;
		}

		// Splits whitespace-separated words (the XML keyword/type lists).
		void splitWords(const char* text, std::unordered_set<std::string>& out) {
			if (!text) return;
			const char* p = text;
			while (*p) {
				while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
				const char* begin = p;
				while (*p && !std::isspace(static_cast<unsigned char>(*p))) ++p;
				if (p > begin) out.insert(std::string(begin, p - begin));
			}
		}
	}

	// ---- registry -------------------------------------------------------------------
	SyntaxRegistry::SyntaxRegistry() {
		define(python());
	}

	void SyntaxRegistry::define(const Language& lang) {
		Language copy = lang;
		// Longest delimiter first, so `"""` is tried before `"`.
		std::sort(copy.stringDelims.begin(), copy.stringDelims.end(),
		          [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
		// Case-insensitive languages match on lowercase, so fold the tables once here
		// rather than on every lookup.
		if (!copy.caseSensitive) {
			std::unordered_set<std::string> k, t;
			for (const auto& w : copy.keywords) k.insert(toLower(w));
			for (const auto& w : copy.types)    t.insert(toLower(w));
			copy.keywords = std::move(k);
			copy.types = std::move(t);
		}
		mLanguages[copy.name] = std::move(copy);
	}

	const Language* SyntaxRegistry::find(const std::string& name) const {
		auto it = mLanguages.find(name);
		return it != mLanguages.end() ? &it->second : nullptr;
	}

	Language SyntaxRegistry::python() {
		Language lang;
		lang.name = "python";
		lang.keywords = {
			"and", "as", "assert", "async", "await", "break", "case", "class", "continue",
			"def", "del", "elif", "else", "except", "finally", "for", "from", "global",
			"if", "import", "in", "is", "lambda", "match", "nonlocal", "not", "or", "pass",
			"raise", "return", "try", "while", "with", "yield",
		};
		// Types and constants. Builtin *functions* (print, len, ...) are left out on
		// purpose: they get the Function color from the call-site rule below.
		lang.types = {
			"True", "False", "None", "self", "cls",
			"int", "float", "str", "bool", "bytes", "bytearray", "complex",
			"list", "dict", "set", "frozenset", "tuple", "object", "type",
			"Exception", "BaseException", "ValueError", "TypeError", "KeyError",
			"IndexError", "RuntimeError", "StopIteration", "NotImplemented",
		};
		lang.lineComments = { "#" };
		lang.blockCommentBegin.clear(); // Python has none; docstrings are just strings
		lang.blockCommentEnd.clear();
		lang.stringDelims = { "\"\"\"", "'''", "\"", "'" };
		lang.preprocessorPrefix = "@"; // decorators
		lang.caseSensitive = true;
		return lang;
	}

	// ---- the lexer ------------------------------------------------------------------
	void SyntaxRegistry::tokenize(const Language& lang, const std::string& text,
	                              std::vector<Token>& out) {
		out.clear();
		const int n = static_cast<int>(text.size());
		int i = 0;

		auto emit = [&](int begin, int end, TokenKind kind) {
			if (end > begin) out.push_back({ begin, end, kind });
		};

		while (i < n) {
			const char c = text[i];

			// Whitespace: no token, Plain covers it.
			if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

			// Block comment (spans lines).
			if (!lang.blockCommentBegin.empty() && !lang.blockCommentEnd.empty() &&
			    matchAt(text, i, lang.blockCommentBegin)) {
				int j = i + static_cast<int>(lang.blockCommentBegin.size());
				while (j < n && !matchAt(text, j, lang.blockCommentEnd)) ++j;
				if (j < n) j += static_cast<int>(lang.blockCommentEnd.size());
				emit(i, j, TokenKind::Comment);
				i = j;
				continue;
			}

			// Line comment.
			{
				bool matched = false;
				for (const std::string& marker : lang.lineComments) {
					if (matchAt(text, i, marker)) {
						int j = i;
						while (j < n && text[j] != '\n') ++j;
						emit(i, j, TokenKind::Comment);
						i = j;
						matched = true;
						break;
					}
				}
				if (matched) continue;
			}

			// String literal. Delimiters are sorted longest-first, so a triple quote is
			// matched before a single one; only multi-char delimiters may span lines.
			{
				const std::string* delim = nullptr;
				bool forcedMultiline = false;
				// Explicitly multi-line delimiters are checked first, so a single
				// character can still span lines when the language says it does.
				for (const std::string& d : lang.multilineStringDelims) {
					if (matchAt(text, i, d)) { delim = &d; forcedMultiline = true; break; }
				}
				if (!delim) {
					for (const std::string& d : lang.stringDelims) {
						if (matchAt(text, i, d)) { delim = &d; break; }
					}
				}
				if (delim) {
					const int len = static_cast<int>(delim->size());
					const bool multiline = forcedMultiline || len > 1;
					int j = i + len;
					while (j < n) {
						if (lang.escapeWithBackslash && text[j] == '\\' && j + 1 < n) { j += 2; continue; }
						if (!multiline && text[j] == '\n') break; // unterminated; stop at the line
						if (matchAt(text, j, *delim)) { j += len; break; }
						++j;
					}
					emit(i, (std::min)(j, n), TokenKind::String);
					i = (std::min)(j, n);
					continue;
				}
			}

			// Preprocessor / decorator: only at the head of a line, so Python's `@` as an
			// operator and C's `#` inside an expression don't trigger it.
			if (!lang.preprocessorPrefix.empty() && matchAt(text, i, lang.preprocessorPrefix) &&
			    onlyBlanksBefore(text, i)) {
				int j = i + static_cast<int>(lang.preprocessorPrefix.size());
				while (j < n && (isIdentChar(text[j]) || text[j] == '.')) ++j;
				emit(i, j, TokenKind::Preprocessor);
				i = j;
				continue;
			}

			// Number literal (including 0x.. / 1e-9 / 1_000 and trailing type suffixes).
			if (isDigit(c) || (c == '.' && i + 1 < n && isDigit(text[i + 1]))) {
				int j = i;
				if (c == '0' && i + 1 < n && std::strchr("xXbBoO", text[i + 1])) {
					j = i + 2;
					while (j < n && (isIdentChar(text[j]))) ++j;
				} else {
					while (j < n && (isDigit(text[j]) || text[j] == '_' || text[j] == '.')) ++j;
					if (j < n && (text[j] == 'e' || text[j] == 'E')) {
						int k = j + 1;
						if (k < n && (text[k] == '+' || text[k] == '-')) ++k;
						if (k < n && isDigit(text[k])) { j = k; while (j < n && isDigit(text[j])) ++j; }
					}
					while (j < n && std::strchr("fFuUlLjJ", text[j])) ++j; // suffixes
				}
				emit(i, j, TokenKind::Number);
				i = j;
				continue;
			}

			// Identifier: keyword, type, call, or plain.
			if (isIdentStart(c)) {
				int j = i;
				while (j < n && isIdentChar(text[j])) ++j;
				std::string word = text.substr(i, j - i);
				if (!lang.caseSensitive) word = toLower(word); // tables were folded in define()

				if (lang.keywords.count(word))     emit(i, j, TokenKind::Keyword);
				else if (lang.types.count(word))   emit(i, j, TokenKind::Type);
				else {
					// An identifier directly followed by '(' reads as a call.
					int k = j;
					while (k < n && (text[k] == ' ' || text[k] == '\t')) ++k;
					if (k < n && text[k] == '(') emit(i, j, TokenKind::Function);
				}
				i = j;
				continue;
			}

			// Operators / punctuation: merge a run into one span (same color anyway).
			if (isOperatorChar(c)) {
				int j = i;
				while (j < n && isOperatorChar(text[j])) ++j;
				emit(i, j, TokenKind::Operator);
				i = j;
				continue;
			}

			++i; // anything else stays Plain
		}
	}

	// ---- XML loading ----------------------------------------------------------------
	namespace {
		void applyLanguage(SyntaxRegistry& registry, const XMLElement* e) {
			const char* name = e->Attribute("name");
			if (!name || !name[0]) {
				RDA_LOG_WARNING("Skipping <language> with no name attribute");
				return;
			}

			// Derive from another language when asked, so a variant only lists changes.
			Language lang;
			if (const char* base = e->Attribute("base")) {
				if (const Language* from = registry.find(base)) lang = *from;
				else RDA_LOG_WARNING("Unknown base language '" << base << "' for '" << name << "'");
			}
			lang.name = name;
			e->QueryBoolAttribute("caseSensitive", &lang.caseSensitive);

			if (const XMLElement* k = e->FirstChildElement("keywords")) {
				lang.keywords.clear();
				splitWords(k->GetText(), lang.keywords);
			}
			if (const XMLElement* t = e->FirstChildElement("types")) {
				lang.types.clear();
				splitWords(t->GetText(), lang.types);
			}
			if (e->FirstChildElement("lineComment")) {
				lang.lineComments.clear();
				for (const XMLElement* c = e->FirstChildElement("lineComment"); c;
				     c = c->NextSiblingElement("lineComment")) {
					if (const char* v = c->GetText()) lang.lineComments.push_back(v);
				}
			}
			if (const XMLElement* b = e->FirstChildElement("blockComment")) {
				if (const char* begin = b->Attribute("begin")) lang.blockCommentBegin = begin;
				if (const char* end = b->Attribute("end"))     lang.blockCommentEnd = end;
			}
			if (e->FirstChildElement("string")) {
				lang.stringDelims.clear();
				for (const XMLElement* s = e->FirstChildElement("string"); s;
				     s = s->NextSiblingElement("string")) {
					if (const char* v = s->GetText()) lang.stringDelims.push_back(v);
				}
			}
			if (e->FirstChildElement("multilineString")) {
				lang.multilineStringDelims.clear();
				for (const XMLElement* s = e->FirstChildElement("multilineString"); s;
				     s = s->NextSiblingElement("multilineString")) {
					if (const char* v = s->GetText()) lang.multilineStringDelims.push_back(v);
				}
			}
			if (const XMLElement* p = e->FirstChildElement("preprocessor")) {
				if (const char* v = p->GetText()) lang.preprocessorPrefix = v;
			}

			registry.define(lang);
		}

		bool applyDocument(SyntaxRegistry& registry, const XMLDocument& doc) {
			const XMLElement* root = doc.RootElement();
			if (!root) {
				RDA_LOG_ERROR("Language XML has no root element");
				return false;
			}
			for (const XMLElement* e = root->FirstChildElement("language"); e;
			     e = e->NextSiblingElement("language")) {
				applyLanguage(registry, e);
			}
			return true;
		}
	}

	bool SyntaxRegistry::loadFromString(const char* xml) {
		XMLDocument doc;
		if (doc.Parse(xml) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_ERROR("Failed to parse language XML: " << doc.ErrorStr());
			return false;
		}
		return applyDocument(*this, doc);
	}

	bool SyntaxRegistry::loadFromFile(const std::string& path) {
		XMLDocument doc;
		if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_WARNING("Language file not loaded (" << path << "): " << doc.ErrorStr());
			return false;
		}
		bool ok = applyDocument(*this, doc);
		if (ok) {
			RDA_LOG_INFO("Loaded syntax languages: " << path);
		}
		return ok;
	}
}
