#include <Core/Json.h>
#include <Core/Utf8.h>

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace RDA::Json {

	void appendString(std::string& out, std::string_view text) {
		static const char* const kHex = "0123456789abcdef";
		out.push_back('"');
		for (const char c : text) {
			switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					out += "\\u00";
					out.push_back(kHex[(c >> 4) & 0xF]);
					out.push_back(kHex[c & 0xF]);
				} else {
					out.push_back(c);
				}
			}
		}
		out.push_back('"');
	}

	void appendNumber(std::string& out, double value) {
		if (!std::isfinite(value)) { out += "null"; return; }
		char buffer[32];
		// The shortest text that reads back as the same double: 3 rather than 3.0000000001
		// rounding noise, and 0.1 rather than seventeen digits of it.
		const auto written = std::to_chars(buffer, buffer + sizeof buffer, value);
		out.append(buffer, written.ptr);
	}

	namespace {
		struct Reader {
			std::string_view text;
			size_t           at = 0;
			std::string      error;

			bool fail(const char* what) {
				if (error.empty()) error = std::string(what) + " at character " + std::to_string(at + 1);
				return false;
			}
			void space() {
				while (at < text.size() && (text[at] == ' ' || text[at] == '\t' ||
				                            text[at] == '\n' || text[at] == '\r')) ++at;
			}
			bool literal(std::string_view word) {
				if (text.substr(at, word.size()) != word) return fail("expected a value");
				at += word.size();
				return true;
			}
			int hex(char c) {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			}
			bool quad(uint32_t& out) {
				if (at + 4 > text.size()) return fail("a \\u escape is cut short");
				out = 0;
				for (int i = 0; i < 4; ++i) {
					const int d = hex(text[at + i]);
					if (d < 0) return fail("a \\u escape needs four hex digits");
					out = (out << 4) | static_cast<uint32_t>(d);
				}
				at += 4;
				return true;
			}
			bool string(std::string* into) {
				++at; // the quote
				for (;;) {
					if (at >= text.size()) return fail("a string is not closed");
					const char c = text[at++];
					if (c == '"') return true;
					if (static_cast<unsigned char>(c) < 0x20) return fail("a control character inside a string");
					if (c != '\\') { if (into) into->push_back(c); continue; }
					if (at >= text.size()) return fail("a string is not closed");
					const char e = text[at++];
					switch (e) {
					case '"': case '\\': case '/': if (into) into->push_back(e); break;
					case 'n': if (into) into->push_back('\n'); break;
					case 'r': if (into) into->push_back('\r'); break;
					case 't': if (into) into->push_back('\t'); break;
					case 'b': if (into) into->push_back('\b'); break;
					case 'f': if (into) into->push_back('\f'); break;
					case 'u': {
						uint32_t cp = 0;
						if (!quad(cp)) return false;
						// A surrogate pair is one codepoint written as two escapes.
						if (cp >= 0xD800 && cp <= 0xDBFF && at + 6 <= text.size() &&
						    text[at] == '\\' && text[at + 1] == 'u') {
							const size_t mark = at;
							at += 2;
							uint32_t low = 0;
							if (!quad(low)) return false;
							if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
							else at = mark;
						}
						if (into) Utf8::encode(cp, *into);
						break;
					}
					default: return fail("an unknown escape in a string");
					}
				}
			}
			bool number(double* into) {
				const size_t start = at;
				if (at < text.size() && text[at] == '-') ++at;
				if (at >= text.size() || text[at] < '0' || text[at] > '9') return fail("expected a value");
				while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
				if (at < text.size() && text[at] == '.') {
					++at;
					if (at >= text.size() || text[at] < '0' || text[at] > '9') return fail("a number ends at its point");
					while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
				}
				if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
					++at;
					if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
					if (at >= text.size() || text[at] < '0' || text[at] > '9') return fail("a number's exponent has no digits");
					while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
				}
				if (into) {
					const std::string digits(text.substr(start, at - start));
					*into = std::strtod(digits.c_str(), nullptr);
				}
				return true;
			}
			// One value, filled into `item` when given; nested ones are only checked.
			bool value(Item* item, int depth) {
				if (depth > 64) return fail("nested too deeply");
				space();
				if (at >= text.size()) return fail("expected a value");
				const size_t start = at;
				const char c = text[at];
				bool ok = true;
				Item::Kind kind = Item::Kind::Null;
				if (c == '"') {
					kind = Item::Kind::Text;
					ok = string(item ? &item->text : nullptr);
				} else if (c == '[' || c == '{') {
					kind = c == '[' ? Item::Kind::Array : Item::Kind::Object;
					const char close = c == '[' ? ']' : '}';
					++at;
					space();
					if (at < text.size() && text[at] == close) { ++at; }
					else for (;;) {
						if (close == '}') {
							space();
							if (at >= text.size() || text[at] != '"') return fail("expected a key");
							if (!string(nullptr)) return false;
							space();
							if (at >= text.size() || text[at] != ':') return fail("expected ':'");
							++at;
						}
						if (!value(nullptr, depth + 1)) return false;
						space();
						if (at < text.size() && text[at] == ',') { ++at; continue; }
						if (at < text.size() && text[at] == close) { ++at; break; }
						return fail(close == ']' ? "expected ',' or ']'" : "expected ',' or '}'");
					}
				} else if (c == 't') { kind = Item::Kind::Bool; ok = literal("true"); if (item) item->flag = true; }
				else if (c == 'f')   { kind = Item::Kind::Bool; ok = literal("false"); }
				else if (c == 'n')   { kind = Item::Kind::Null; ok = literal("null"); }
				else                 { kind = Item::Kind::Number; ok = number(item ? &item->number : nullptr); }
				if (!ok) return false;
				if (item) {
					item->kind = kind;
					item->raw.assign(text.substr(start, at - start));
				}
				return true;
			}
		};
	}

	bool readArray(std::string_view json, std::vector<Item>& out, std::string* error) {
		out.clear();
		Reader reader;
		reader.text = json;
		auto failed = [&](const char* what) {
			reader.fail(what);
			if (error) *error = reader.error;
			out.clear();
			return false;
		};
		reader.space();
		if (reader.at >= json.size() || json[reader.at] != '[') return failed("expected an array");
		++reader.at;
		reader.space();
		if (reader.at < json.size() && json[reader.at] == ']') {
			++reader.at;
		} else {
			for (;;) {
				Item item;
				if (!reader.value(&item, 1)) return failed("expected a value");
				out.push_back(std::move(item));
				reader.space();
				if (reader.at < json.size() && json[reader.at] == ',') { ++reader.at; continue; }
				if (reader.at < json.size() && json[reader.at] == ']') { ++reader.at; break; }
				return failed("expected ',' or ']'");
			}
		}
		reader.space();
		if (reader.at != json.size()) return failed("something follows the array");
		return true;
	}
}
