#include "CodeWriter.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace RDA::Layout::Codegen {

	namespace {
		// A Unicode line separator that starts at a position of a UTF-8 string: its code
		// point and how many bytes it takes, or a length of zero when none starts there.
		// These are the three that are line breaks to somebody other than the language
		// reading the literal, which is how one in a text default ends up splitting it.
		struct Separator {
			uint32_t point = 0;
			size_t   length = 0;
		};

		Separator separatorAt(std::string_view text, size_t at) {
			const auto byte = [&](size_t index) { return static_cast<unsigned char>(text[index]); };
			if (byte(at) == 0xC2 && at + 1 < text.size() && byte(at + 1) == 0x85) {
				return { 0x85, 2 }; // NEL
			}
			if (byte(at) == 0xE2 && at + 2 < text.size() && byte(at + 1) == 0x80) {
				if (byte(at + 2) == 0xA8) return { 0x2028, 3 }; // LINE SEPARATOR
				if (byte(at + 2) == 0xA9) return { 0x2029, 3 }; // PARAGRAPH SEPARATOR
			}
			return {};
		}

		// Whether the language has to be told about a separator. C# ends a literal's line
		// at any of them, so a regular string cannot hold one. JavaScript allows U+2028 and
		// U+2029 only from ES2019, and says nothing of NEL being special. Python and C++
		// take all three as they come.
		bool mustEscapeSeparator(uint32_t point, Language language) {
			switch (language) {
			case Language::CSharp:     return true;
			case Language::JavaScript: return point == 0x2028 || point == 0x2029;
			default:                   return false;
			}
		}

		const char* const kHex = "0123456789abcdef";

		// A control character that has no name of its own, as the language reads it back.
		std::string controlEscape(unsigned char c, Language language) {
			std::string out = "\\";
			switch (language) {
			case Language::Cpp:
				// Always three digits. An octal escape stops after three at most, so what
				// follows it is never taken as part of it -- which \x cannot promise.
				out += static_cast<char>('0' + ((c >> 6) & 7));
				out += static_cast<char>('0' + ((c >> 3) & 7));
				out += static_cast<char>('0' + (c & 7));
				break;
			case Language::CSharp:
				// \x is one to four digits there, so it would swallow a hex digit that
				// follows. \u is exactly four.
				out += "u00";
				out += kHex[c >> 4];
				out += kHex[c & 15];
				break;
			case Language::Python:
			case Language::JavaScript:
				out += 'x';
				out += kHex[c >> 4];
				out += kHex[c & 15];
				break;
			}
			return out;
		}

		std::string unicodeEscape(uint32_t point) {
			std::string out = "\\u";
			for (int shift = 12; shift >= 0; shift -= 4) out += kHex[(point >> shift) & 15];
			return out;
		}
	}

	std::string escapeString(std::string_view text, Language language) {
		std::string out;
		out.reserve(text.size() + 2);
		for (size_t i = 0; i < text.size(); ++i) {
			const unsigned char c = static_cast<unsigned char>(text[i]);
			switch (c) {
			case '\\': out += "\\\\"; continue;
			case '"':  out += "\\\""; continue;
			case '\n': out += "\\n";  continue;
			case '\r': out += "\\r";  continue;
			case '\t': out += "\\t";  continue;
			default:   break;
			}
			if (c < 0x20 || c == 0x7f) {
				out += controlEscape(c, language);
				continue;
			}
			if (c >= 0x80) {
				const Separator separator = separatorAt(text, i);
				if (separator.length && mustEscapeSeparator(separator.point, language)) {
					out += unicodeEscape(separator.point);
					i += separator.length - 1;
					continue;
				}
			}
			out += static_cast<char>(c);
		}
		return out;
	}

	std::string quoteString(std::string_view text, Language language) {
		return "\"" + escapeString(text, language) + "\"";
	}

	std::string base64(const std::vector<uint8_t>& bytes) {
		static const char* const kAlphabet =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((bytes.size() + 2) / 3 * 4);
		for (size_t i = 0; i < bytes.size(); i += 3) {
			const uint32_t a = bytes[i];
			const uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
			const uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
			const uint32_t n = (a << 16) | (b << 8) | c;
			out += kAlphabet[(n >> 18) & 63];
			out += kAlphabet[(n >> 12) & 63];
			out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
			out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
		}
		return out;
	}

	std::vector<std::string> base64Lines(const std::vector<uint8_t>& bytes) {
		const std::string all = base64(bytes);
		std::vector<std::string> lines;
		for (size_t at = 0; at < all.size(); at += 76) lines.push_back(all.substr(at, 76));
		return lines;
	}

	std::string capitalised(const std::string& name) {
		std::string out = name;
		if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
		return out;
	}

	std::string rowStructName(const std::string& table) {
		return capitalised(table) + "Row";
	}

	std::string handlerName(const std::string& name) {
		return "on" + capitalised(name);
	}

	std::string setterName(const std::string& field) {
		return "set" + capitalised(field);
	}

	int columnKind(StateType type) {
		switch (type) {
		case StateType::Number: return 0;
		case StateType::Bool:   return 1;
		case StateType::Text:   return 2;
		}
		return 0;
	}

	bool codeReady(const StateSchema& schema, std::string& error) {
		if (schema.hasCode() && schema.code.empty()) {
			error = "state.ts has computed values or actions, and its code was not compiled -- "
			        "generate the state module with `rda build`, which compiles it";
			return false;
		}
		return true;
	}

	bool writeTextFile(const std::string& path, const std::string& text, std::string& error) {
		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(path).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		// Opened as text on purpose: on Windows the file then ends its lines the way the
		// sources around it do.
		std::ofstream file(path, std::ios::trunc);
		if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
			error = "cannot write " + path;
			return false;
		}
		return true;
	}
}
