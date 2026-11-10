#include <Layout/JsxSource.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace RDA::Layout::Jsx {

	namespace {

		bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
		bool nameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
		bool namePart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
		bool tagPart(char c) { return namePart(c) || c == '.' || c == '-' || c == ':'; }

		// Reads the file once from the top. JavaScript is only followed as far as it takes not
		// to be fooled: a `<` in a string, a comment or a comparison is not an element, and a
		// `}` in one does not end the expression it is in.
		class Scanner {
		public:
			Scanner(std::string_view text, std::vector<Element>& out) : mText(text), mOut(out) {}

			void run() { code(-1, false); }

		private:
			bool atEnd() const { return mAt >= mText.size(); }
			char peek(size_t ahead = 0) const {
				return mAt + ahead < mText.size() ? mText[mAt + ahead] : '\0';
			}

			bool skipComment() {
				if (peek() != '/') return false;
				if (peek(1) == '/') {
					while (!atEnd() && peek() != '\n') ++mAt;
					return true;
				}
				if (peek(1) == '*') {
					const size_t close = mText.find("*/", mAt + 2);
					mAt = close == std::string_view::npos ? mText.size() : close + 2;
					return true;
				}
				return false;
			}

			void skipSpace() {
				for (;;) {
					while (!atEnd() && isSpace(peek())) ++mAt;
					if (!skipComment()) return;
				}
			}

			void string(char quote) {
				++mAt;
				while (!atEnd() && peek() != quote) mAt += peek() == '\\' ? 2 : 1;
				++mAt;
			}

			// `${...}` holds code, and that code can hold anything code can.
			void templateString(int parent) {
				++mAt;
				while (!atEnd() && peek() != '`') {
					if (peek() == '\\') { mAt += 2; continue; }
					if (peek() == '$' && peek(1) == '{') {
						mAt += 2;
						code(parent, true);
						++mAt;
						continue;
					}
					++mAt;
				}
				++mAt;
			}

			// An element, rather than a less-than or a type's parameters. What decides it is what
			// came before: an element is a value, so it stands where a value can, and `a < b` and
			// `Array<string>` both have a name in front of them.
			bool startsElement() const {
				const char next = peek(1);
				if (!nameStart(next) && next != '>') return false;

				size_t before = mAt;
				while (before > 0 && isSpace(mText[before - 1])) --before;
				if (before == 0) return true;
				const char last = mText[before - 1];
				if (namePart(last)) {
					size_t word = before;
					while (word > 0 && namePart(mText[word - 1])) --word;
					const std::string_view name = mText.substr(word, before - word);
					return name == "return" || name == "default" || name == "yield" ||
					       name == "case" || name == "await";
				}
				return last != ')' && last != ']' && last != '}' && last != '"' && last != '\'' &&
				       last != '`';
			}

			// Stops at the end of the file or, when `braced`, at the `}` that closes what it was
			// started in. An element met in here belongs to `parent` as something code makes.
			void code(int parent, bool braced) {
				int depth = 0;
				while (!atEnd()) {
					if (skipComment()) continue;
					const char c = peek();
					if (c == '"' || c == '\'') { string(c); continue; }
					if (c == '`') { templateString(parent); continue; }
					if (c == '{') { ++depth; ++mAt; continue; }
					if (c == '}') {
						if (braced && depth == 0) return;
						--depth;
						++mAt;
						continue;
					}
					if (c == '<' && startsElement()) {
						const size_t begin = mAt;
						if (!element(parent, parent >= 0)) mAt = begin + 1;
						continue;
					}
					++mAt;
				}
			}

			bool attribute(int self) {
				Attribute one;
				one.begin = mAt;
				if (peek() == '{') {
					one.kind = Attribute::Kind::Spread;
					++mAt;
					one.valueBegin = mAt;
					code(self, true);
					one.valueEnd = mAt;
					if (peek() != '}') return false;
					one.end = ++mAt;
					mOut[static_cast<size_t>(self)].attributes.push_back(std::move(one));
					return true;
				}
				if (!nameStart(peek())) return false;
				const size_t nameBegin = mAt;
				while (!atEnd() && (tagPart(peek()))) ++mAt;
				one.name = std::string(mText.substr(nameBegin, mAt - nameBegin));
				one.end = mAt;

				const size_t afterName = mAt;
				skipSpace();
				if (peek() != '=') {
					mAt = afterName;
					mOut[static_cast<size_t>(self)].attributes.push_back(std::move(one));
					return true;
				}
				++mAt;
				skipSpace();
				const char c = peek();
				if (c == '"' || c == '\'') {
					// JSX has no escapes in a quoted attribute: the next quote is the end.
					one.kind = Attribute::Kind::Text;
					one.valueBegin = mAt + 1;
					const size_t close = mText.find(c, mAt + 1);
					if (close == std::string_view::npos) return false;
					one.valueEnd = close;
					mAt = close + 1;
				} else if (c == '{') {
					one.kind = Attribute::Kind::Expression;
					++mAt;
					one.valueBegin = mAt;
					code(self, true);
					one.valueEnd = mAt;
					if (peek() != '}') return false;
					++mAt;
				} else {
					return false;
				}
				one.end = mAt;
				mOut[static_cast<size_t>(self)].attributes.push_back(std::move(one));
				return true;
			}

			bool childrenOf(int self) {
				for (;;) {
					if (atEnd()) return false;
					const char c = peek();
					if (c == '<' && peek(1) == '/') {
						mOut[static_cast<size_t>(self)].closeBegin = mAt;
						const size_t close = mText.find('>', mAt);
						if (close == std::string_view::npos) return false;
						mAt = close + 1;
						mOut[static_cast<size_t>(self)].end = mAt;
						return true;
					}
					if (c == '<') {
						if (!element(self, false)) return false;
						continue;
					}
					if (c == '{') {
						++mAt;
						code(self, true);
						if (peek() != '}') return false;
						++mAt;
						continue;
					}
					++mAt;
				}
			}

			// False when this turned out not to be an element after all, with nothing kept. By
			// index throughout: anything nested adds to mOut, and that moves what a reference
			// would be holding.
			bool element(int parent, bool generated) {
				const int self = static_cast<int>(mOut.size());
				const size_t index = static_cast<size_t>(self);
				Element made;
				made.begin = mAt;
				made.parent = parent;
				made.generated = generated;
				++mAt;
				const size_t nameBegin = mAt;
				while (!atEnd() && tagPart(peek())) ++mAt;
				made.tag = std::string(mText.substr(nameBegin, mAt - nameBegin));
				made.nameEnd = mAt;
				mOut.push_back(std::move(made));

				const auto abandon = [&] {
					mOut.resize(index);
					return false;
				};

				for (;;) {
					skipSpace();
					if (atEnd()) return abandon();
					if (peek() == '/' && peek(1) == '>') {
						mAt += 2;
						Element& done = mOut[index];
						done.selfClosing = true;
						done.openEnd = done.closeBegin = done.end = mAt;
						break;
					}
					if (peek() == '>') {
						++mAt;
						mOut[index].openEnd = mAt;
						if (!childrenOf(self)) return abandon();
						break;
					}
					if (!attribute(self)) return abandon();
				}
				if (parent >= 0 && !generated) mOut[static_cast<size_t>(parent)].children.push_back(self);
				return true;
			}

			std::string_view      mText;
			std::vector<Element>& mOut;
			size_t                mAt = 0;
		};

		std::string_view trimmed(std::string_view text) {
			while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
			while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
			return text;
		}

		bool isNumber(std::string_view text) {
			if (text.empty()) return false;
			const std::string copy(text);
			char* end = nullptr;
			std::strtod(copy.c_str(), &end);
			return end != copy.c_str() && *end == '\0';
		}

		std::vector<std::string_view> segmentsOf(std::string_view path) {
			std::vector<std::string_view> out;
			size_t at = 0;
			while (at <= path.size()) {
				size_t slash = path.find('/', at);
				if (slash == std::string_view::npos) slash = path.size();
				out.push_back(path.substr(at, slash - at));
				at = slash + 1;
			}
			return out;
		}
	}

	Document::Document(std::string text) : mText(std::move(text)) { parse(); }

	void Document::parse() {
		mElements.clear();
		Scanner(mText, mElements).run();
	}

	void Document::replace(size_t from, size_t to, std::string_view with) {
		mText.replace(from, to - from, with);
		parse();
	}

	size_t Document::lineStart(size_t at) const {
		if (at == 0) return 0;
		const size_t before = mText.rfind('\n', at - 1);
		return before == std::string::npos ? 0 : before + 1;
	}

	bool Document::firstOnLine(size_t at) const {
		for (size_t i = lineStart(at); i < at; ++i) {
			if (!isSpace(mText[i])) return false;
		}
		return true;
	}

	bool Document::lastOnLine(size_t at) const {
		for (size_t i = at; i < mText.size(); ++i) {
			if (mText[i] == '\n') return true;
			if (!isSpace(mText[i])) return false;
		}
		return true;
	}

	std::string Document::indentAt(size_t at) const {
		const size_t start = lineStart(at);
		size_t end = start;
		while (end < mText.size() && (mText[end] == ' ' || mText[end] == '\t')) ++end;
		return mText.substr(start, end - start);
	}

	std::string Document::newline() const {
		return mText.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	}

	// A tab if the file indents with them anywhere, and two spaces if it does not.
	std::string Document::indentStep() const {
		return mText.find("\n\t") != std::string::npos ? "\t" : "  ";
	}

	const Attribute* Document::attribute(int element, std::string_view name) const {
		for (const Attribute& one : mElements[static_cast<size_t>(element)].attributes) {
			if (one.name == name) return &one;
		}
		return nullptr;
	}

	Value Document::value(int element, std::string_view name) const {
		Value out;
		const Attribute* one = attribute(element, name);
		if (!one) return out;
		out.present = true;
		const std::string_view inside =
			std::string_view(mText).substr(one->valueBegin, one->valueEnd - one->valueBegin);
		switch (one->kind) {
			case Attribute::Kind::Bare:
				out.text = "true";
				break;
			case Attribute::Kind::Text:
				out.text = std::string(inside);
				break;
			case Attribute::Kind::Expression: {
				const std::string_view code = trimmed(inside);
				out.text = std::string(code);
				const bool quoted = code.size() >= 2 && (code.front() == '"' || code.front() == '\'') &&
				                    code.back() == code.front() &&
				                    code.find('\\') == std::string_view::npos &&
				                    code.find(code.front(), 1) == code.size() - 1;
				if (quoted) out.text = std::string(code.substr(1, code.size() - 2));
				else if (!isNumber(code) && code != "true" && code != "false") out.computed = true;
				break;
			}
			case Attribute::Kind::Spread:
				out.computed = true;
				break;
		}
		return out;
	}

	int Document::find(std::string_view path, std::string_view type, std::string* why) const {
		const std::vector<std::string_view> segments = segmentsOf(path);
		std::string_view last = segments.back();

		// A repeat among siblings: the compiler numbers the second `label` label#2.
		size_t repeat = 1;
		if (const size_t mark = last.find('#'); mark != std::string_view::npos) {
			repeat = static_cast<size_t>(std::strtoul(std::string(last.substr(mark + 1)).c_str(), nullptr, 10));
			last = last.substr(0, mark);
		}

		const auto localId = [this](int index) {
			const Element& element = mElements[static_cast<size_t>(index)];
			const Value id = value(index, "id");
			return id.present && !id.computed ? id.text : element.tag;
		};

		// Whether what is above this element appears in the path, in order, reading upward.
		const auto underPath = [&](int index) {
			size_t segment = segments.size() - 1;
			for (int above = mElements[static_cast<size_t>(index)].parent; above >= 0;
			     above = mElements[static_cast<size_t>(above)].parent) {
				const Element& element = mElements[static_cast<size_t>(above)];
				if (element.tag.empty()) continue;
				// What a function returns: a call site's id replaces the one it wrote, so
				// that one may not be in the path at all.
				if (element.parent < 0) continue;
				const bool component = std::isupper(static_cast<unsigned char>(element.tag.front())) != 0;
				if (component && !value(above, "id").present) continue;
				const std::string local = localId(above);
				bool found = false;
				while (segment > 0 && !found) {
					--segment;
					std::string_view one = segments[segment];
					if (const size_t mark = one.find('#'); mark != std::string_view::npos) one = one.substr(0, mark);
					found = one == local;
				}
				if (!found) return false;
			}
			return true;
		};

		std::vector<int> matches;
		for (size_t i = 0; i < mElements.size(); ++i) {
			const int index = static_cast<int>(i);
			if (mElements[i].tag != type || localId(index) != last) continue;
			if (underPath(index)) matches.push_back(index);
		}

		if (matches.empty()) {
			if (why) *why = "no <" + std::string(type) + "> in this file is `" + std::string(path) + "`";
			return -1;
		}
		if (matches.size() == 1) return repeat == 1 ? matches.front() : -1;

		// Several: fine if they are siblings, because then the number says which. Two with no
		// parent are not siblings, they are what two functions return.
		const int parent = mElements[static_cast<size_t>(matches.front())].parent;
		const bool siblings = parent >= 0 && std::all_of(matches.begin(), matches.end(), [&](int one) {
			return mElements[static_cast<size_t>(one)].parent == parent;
		});
		if (siblings && repeat >= 1 && repeat <= matches.size()) return matches[repeat - 1];
		if (why) {
			*why = std::to_string(matches.size()) + " elements could be `" + std::string(path) +
			       "`; an id on each would tell them apart";
		}
		return -1;
	}

	int Document::editableRoot(std::string_view type) const {
		int found = -1;
		for (size_t i = 0; i < mElements.size(); ++i) {
			const int index = static_cast<int>(i);
			if (mElements[i].parent >= 0 || mElements[i].tag != type || !attribute(index, "editable")) continue;
			if (found >= 0) return -1;
			found = index;
		}
		return found;
	}

	std::string Document::uniqueId(std::string_view base) const {
		for (int n = 1;; ++n) {
			const std::string candidate = std::string(base) + std::to_string(n);
			bool used = false;
			for (size_t i = 0; i < mElements.size() && !used; ++i) {
				const Value id = value(static_cast<int>(i), "id");
				used = id.present && id.text == candidate;
			}
			if (!used) return candidate;
		}
	}

	void Document::setAttribute(int element, std::string_view name, std::string_view value) {
		const std::string whole = std::string(name) + "=" + std::string(value);
		if (const Attribute* one = attribute(element, name)) {
			replace(one->begin, one->end, whole);
			return;
		}

		// After the last one, and in the same way: an author who gives each attribute a line
		// gets another line.
		const Element& target = mElements[static_cast<size_t>(element)];
		if (target.attributes.empty()) {
			replace(target.nameEnd, target.nameEnd, " " + whole);
			return;
		}
		const Attribute& last = target.attributes.back();
		if (firstOnLine(last.begin)) {
			replace(last.end, last.end, newline() + indentAt(last.begin) + whole);
		} else {
			replace(last.end, last.end, " " + whole);
		}
	}

	void Document::removeAttribute(int element, std::string_view name) {
		const Attribute* one = attribute(element, name);
		if (!one) return;
		size_t from = one->begin;
		size_t to = one->end;
		if (firstOnLine(from) && lastOnLine(to)) {
			// Alone on its line, so the line goes.
			from = lineStart(from);
			const size_t newlineAt = mText.find('\n', to);
			to = newlineAt == std::string::npos ? mText.size() : newlineAt + 1;
		} else {
			while (from > 0 && (mText[from - 1] == ' ' || mText[from - 1] == '\t')) --from;
		}
		replace(from, to, "");
	}

	void Document::insertChild(int element, std::string_view child, size_t index) {
		const Element& parent = mElements[static_cast<size_t>(element)];
		const std::string nl = newline();
		const std::string outer = indentAt(parent.begin);

		// Where its brothers are, if it has any on lines of their own.
		std::string inner = outer + indentStep();
		if (!parent.children.empty()) {
			const Element& first = mElements[static_cast<size_t>(parent.children.front())];
			if (firstOnLine(first.begin)) inner = indentAt(first.begin);
		}

		std::string block;
		size_t at = 0;
		while (at <= child.size()) {
			size_t end = child.find('\n', at);
			if (end == std::string_view::npos) end = child.size();
			std::string_view line = child.substr(at, end - at);
			if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
			if (!block.empty()) block += nl;
			block += inner;
			block += line;
			at = end + 1;
		}

		if (parent.selfClosing) {
			size_t from = parent.end - 2;
			while (from > 0 && isSpace(mText[from - 1])) --from;
			replace(from, parent.end, ">" + nl + block + nl + outer + "</" + parent.tag + ">");
			return;
		}
		if (index < parent.children.size()) {
			const size_t before = mElements[static_cast<size_t>(parent.children[index])].begin;
			if (firstOnLine(before)) {
				const size_t start = lineStart(before);
				replace(start, start, block + nl);
			} else {
				replace(before, before, std::string(child) + " ");
			}
			return;
		}
		if (firstOnLine(parent.closeBegin)) {
			const size_t start = lineStart(parent.closeBegin);
			replace(start, start, block + nl);
		} else {
			replace(parent.closeBegin, parent.closeBegin, nl + block + nl + outer);
		}
	}

	void Document::removeElement(int element) {
		const Element& target = mElements[static_cast<size_t>(element)];
		size_t from = target.begin;
		size_t to = target.end;
		if (firstOnLine(from) && lastOnLine(to)) {
			from = lineStart(from);
			const size_t newlineAt = mText.find('\n', to);
			to = newlineAt == std::string::npos ? mText.size() : newlineAt + 1;
		}
		replace(from, to, "");
	}

	bool formatValue(PropType type, std::string_view typed, std::string& out, std::string& why) {
		const std::string_view text = trimmed(typed);
		const auto quoted = [&] {
			// A quoted attribute has no escapes, so text holding the quote goes in braces, as a
			// string the compiler reads rather than one JSX does.
			if (typed.find('"') == std::string_view::npos) {
				out = "\"" + std::string(typed) + "\"";
				return true;
			}
			std::string escaped;
			for (const char c : typed) {
				if (c == '"' || c == '\\') escaped.push_back('\\');
				escaped.push_back(c);
			}
			out = "{\"" + escaped + "\"}";
			return true;
		};

		switch (type) {
			case PropType::Number:
				if (!isNumber(text)) { why = "a number"; return false; }
				out = "{" + std::string(text) + "}";
				return true;
			case PropType::Bool:
				if (text != "true" && text != "false") { why = "true or false"; return false; }
				out = "{" + std::string(text) + "}";
				return true;
			case PropType::Size:
				if (isNumber(text)) { out = "{" + std::string(text) + "}"; return true; }
				if (text.rfind("fill", 0) == 0 || text.rfind("content", 0) == 0) {
					out = "\"" + std::string(text) + "\"";
					return true;
				}
				why = "a number, content or fill";
				return false;
			case PropType::Event:
				why = "code, which belongs in the file";
				return false;
			default:
				return quoted();
		}
	}
}
