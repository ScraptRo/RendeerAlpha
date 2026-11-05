#include "SourceMap.h"
#include <Core/Json.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace RDA::Layout {

	namespace {

		// ---- reading the map ---------------------------------------------------------
		//
		// Not a JSON reader. esbuild writes the map, and what it writes is one object of
		// strings, numbers and arrays of strings -- so a value is skipped by knowing where a
		// string ends and how brackets nest, and the one array wanted is handed to the
		// engine's own reader, which knows the escapes a path may be written with.

		void skipSpace(std::string_view text, size_t& at) {
			while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
		}

		// Past one string, both quotes included. False when it does not end.
		bool skipString(std::string_view text, size_t& at) {
			if (at >= text.size() || text[at] != '"') return false;
			for (++at; at < text.size(); ++at) {
				if (text[at] == '\\') { ++at; continue; }
				if (text[at] == '"') { ++at; return true; }
			}
			return false;
		}

		bool skipValue(std::string_view text, size_t& at) {
			if (at >= text.size()) return false;
			if (text[at] == '"') return skipString(text, at);
			if (text[at] == '[' || text[at] == '{') {
				int depth = 0;
				while (at < text.size()) {
					const char c = text[at];
					if (c == '"') { if (!skipString(text, at)) return false; continue; }
					if (c == '[' || c == '{') ++depth;
					if (c == ']' || c == '}') { if (--depth == 0) { ++at; return true; } }
					++at;
				}
				return false;
			}
			while (at < text.size() && text[at] != ',' && text[at] != '}' && text[at] != ']' &&
			       !std::isspace(static_cast<unsigned char>(text[at]))) ++at;
			return true;
		}

		// The map's `sources` and `mappings`, out of the object it is.
		bool readFields(std::string_view json, std::vector<std::string>& sources, std::string_view& mappings) {
			size_t at = 0;
			skipSpace(json, at);
			if (at >= json.size() || json[at] != '{') return false;
			++at;
			for (;;) {
				skipSpace(json, at);
				if (at < json.size() && json[at] == '}') return true;
				const size_t keyStart = at;
				if (!skipString(json, at)) return false;
				const std::string_view key = json.substr(keyStart + 1, at - keyStart - 2);
				skipSpace(json, at);
				if (at >= json.size() || json[at] != ':') return false;
				++at;
				skipSpace(json, at);
				const size_t valueStart = at;
				if (!skipValue(json, at)) return false;
				const std::string_view value = json.substr(valueStart, at - valueStart);

				if (key == "sources") {
					std::vector<Json::Item> items;
					if (!Json::readArray(value, items)) return false;
					for (const Json::Item& item : items)
						sources.push_back(item.kind == Json::Item::Kind::Text ? item.text : std::string());
				} else if (key == "mappings") {
					// Base64 digits, commas and semicolons: nothing in it is ever escaped.
					if (value.size() < 2 || value.front() != '"') return false;
					mappings = value.substr(1, value.size() - 2);
				}

				skipSpace(json, at);
				if (at < json.size() && json[at] == ',') { ++at; continue; }
				if (at < json.size() && json[at] == '}') return true;
				return false;
			}
		}

		int base64Digit(char c) {
			if (c >= 'A' && c <= 'Z') return c - 'A';
			if (c >= 'a' && c <= 'z') return c - 'a' + 26;
			if (c >= '0' && c <= '9') return c - '0' + 52;
			if (c == '+') return 62;
			if (c == '/') return 63;
			return -1;
		}

		// ---- naming a source ---------------------------------------------------------

		std::string percentDecoded(std::string_view text) {
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			std::string out;
			for (size_t i = 0; i < text.size(); ++i) {
				if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
					out.push_back(static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2])));
					i += 2;
				} else {
					out.push_back(text[i]);
				}
			}
			return out;
		}

		// Text the engine holds is UTF-8, and so is the map. A path made from a std::string
		// on Windows is read in the process's code page instead, which is UTF-8 for the tool
		// and anything at all for a backend's process -- so both ways go through u8string.
		std::filesystem::path pathOf(const std::string& text) {
			return std::filesystem::path(std::u8string(text.begin(), text.end()));
		}
		std::string textOf(const std::filesystem::path& path) {
			const std::u8string text = path.generic_u8string();
			return std::string(text.begin(), text.end());
		}

		// A source as the map writes it, as a path: relative to the directory esbuild ran
		// in, which is this process's, or a file:// URL when there is no relative way there
		// -- a file on another drive than that directory.
		std::filesystem::path sourcePath(const std::string& source) {
			std::string text = source;
			if (text.rfind("file://", 0) == 0) {
				text = percentDecoded(std::string_view(text).substr(7));
				// file:///C:/x is the path /C:/x, which on Windows is C:/x.
				if (text.size() > 2 && text[0] == '/' && std::isalpha(static_cast<unsigned char>(text[1])) &&
				    text[2] == ':') text.erase(0, 1);
			}
			return pathOf(text);
		}

		// What a frame of `source` is called, given that the file compiled was `entry`.
		std::string shownName(const std::string& source, const std::string& entry) {
			if (source.empty()) return source;
			std::error_code ec;
			const std::filesystem::path file = std::filesystem::absolute(sourcePath(source), ec).lexically_normal();
			if (ec) return source;
			const std::filesystem::path compiled = std::filesystem::absolute(pathOf(entry), ec).lexically_normal();
			if (ec) return source;

			// The file that was compiled, under the name it was compiled as -- which is what
			// the line above the error already says. Compared by what is on disk too, since
			// on Windows two spellings of a path can be one file.
			if (file == compiled || std::filesystem::equivalent(file, compiled, ec)) return entry;

			const std::filesystem::path relative = file.lexically_relative(compiled.parent_path());
			if (relative.empty()) return textOf(file);
			return textOf((pathOf(entry).parent_path() / relative).lexically_normal());
		}

		bool isDigit(char c) { return c >= '0' && c <= '9'; }
	}

	SourceMap::SourceMap(std::string_view json, std::string entry, size_t linesBefore)
		: mEntry(std::move(entry)), mLinesBefore(linesBefore) {
		std::vector<std::string> sources;
		std::string_view mappings;
		if (json.empty() || !readFields(json, sources, mappings)) return;

		// One group per line of the bundle, split by ';'; one segment per ',', each a run
		// of VLQ fields. The first field is the column of the bundle, counted from the
		// previous segment on the same line; the next three -- which source, its line, its
		// column -- are counted from the previous segment anywhere in the map. A segment
		// of one field says only that generated code starts there, and is not kept.
		std::vector<std::vector<Segment>> lines(1);
		int64_t fields[5] = {};
		int fieldCount = 0;
		int64_t value = 0;
		int shift = 0;
		int64_t column = 0, source = 0, line = 0, sourceColumn = 0;

		auto endSegment = [&]() {
			if (fieldCount >= 1) column += fields[0];
			if (fieldCount >= 4) {
				source += fields[1];
				line += fields[2];
				sourceColumn += fields[3];
				if (column >= 0 && source >= 0 && static_cast<size_t>(source) < sources.size() &&
				    line >= 0 && sourceColumn >= 0) {
					lines.back().push_back(Segment{ static_cast<uint32_t>(column), static_cast<uint32_t>(source),
					                                static_cast<uint32_t>(line), static_cast<uint32_t>(sourceColumn) });
				}
			}
			fieldCount = 0;
		};

		for (const char c : mappings) {
			if (c == ',' || c == ';') {
				endSegment();
				if (c == ';') { lines.emplace_back(); column = 0; }
				continue;
			}
			const int digit = base64Digit(c);
			// Not a map this can read. Said nowhere: the error it was wanted for is still
			// reported, with the bundle's lines, as it always was.
			if (digit < 0 || shift > 30) return;
			value += static_cast<int64_t>(digit & 31) << shift;
			if (digit & 32) { shift += 5; continue; }
			const int64_t decoded = (value & 1) ? -(value >> 1) : (value >> 1);
			if (fieldCount < 5) fields[fieldCount] = decoded;
			++fieldCount;
			value = 0;
			shift = 0;
		}
		endSegment();

		for (std::vector<Segment>& one : lines) {
			std::stable_sort(one.begin(), one.end(),
			                 [](const Segment& a, const Segment& b) { return a.column < b.column; });
		}
		mLines = std::move(lines);
		mSources.reserve(sources.size());
		for (const std::string& one : sources) {
			// A name is a courtesy to an error that is already being reported, and must not
			// become a second one: a path that will not convert is shown as the map wrote it.
			try { mSources.push_back(shownName(one, mEntry)); }
			catch (...) { mSources.push_back(one); }
		}
	}

	bool SourceMap::find(uint32_t line, uint32_t column, Origin& out) const {
		if (line <= mLinesBefore) return false;
		const size_t index = line - 1 - mLinesBefore;
		if (index >= mLines.size() || mLines[index].empty()) return false;

		// The last segment at or before the column: the token the position is in. A
		// position before the first one is in that one's code all the same -- the indent
		// in front of it, which QuickJS does not point at but nothing promises it never will.
		const std::vector<Segment>& segments = mLines[index];
		const uint32_t at = column > 0 ? column - 1 : 0;
		const Segment* found = &segments.front();
		for (const Segment& one : segments) {
			if (one.column > at) break;
			found = &one;
		}
		out.file = mSources[found->source];
		out.line = found->line + 1;
		out.column = found->sourceColumn + 1;
		return true;
	}

	bool SourceMap::nextPosition(const std::string& text, size_t& search, Position& out) const {
		const std::string marker = mEntry + ":";
		auto number = [&text](size_t& at, uint32_t& value) {
			const size_t start = at;
			while (at < text.size() && isDigit(text[at]) && value < 100000000)
				value = value * 10 + static_cast<uint32_t>(text[at++] - '0');
			return at > start;
		};

		for (;;) {
			const size_t found = text.find(marker, search);
			if (found == std::string::npos) return false;
			search = found + 1;
			// Only the whole name: a frame is "    at <entry>:1:2" or " (<entry>:1:2)", and
			// the same text at the end of a longer path is some other file.
			if (found > 0 && text[found - 1] != '(' && text[found - 1] != ' ' &&
			    text[found - 1] != '\t' && text[found - 1] != '\n') continue;

			size_t at = found + marker.size();
			uint32_t line = 0, column = 0;
			if (!number(at, line)) continue;
			if (at + 1 < text.size() && text[at] == ':' && isDigit(text[at + 1])) {
				++at;
				number(at, column);
			}
			out.start = found;
			out.end = at;
			out.line = line;
			out.column = column;
			search = at;
			return true;
		}
	}

	std::string SourceMap::rewrite(const std::string& text) const {
		if (empty() || mEntry.empty()) return text;
		std::string out;
		size_t from = 0, search = 0;
		Position position;
		while (nextPosition(text, search, position)) {
			out.append(text, from, position.start - from);
			Origin origin;
			if (find(position.line, position.column, origin)) {
				out += origin.file + ":" + std::to_string(origin.line) + ":" + std::to_string(origin.column);
			} else {
				out += "the engine's own code";
			}
			from = position.end;
		}
		out.append(text, from, std::string::npos);
		return out;
	}

	std::string SourceMap::ownFrames(const std::string& stack) const {
		if (empty() || mEntry.empty()) return {};
		std::string out;
		size_t lineStart = 0;
		while (lineStart < stack.size()) {
			size_t lineEnd = stack.find('\n', lineStart);
			if (lineEnd == std::string::npos) lineEnd = stack.size();
			const std::string line = stack.substr(lineStart, lineEnd - lineStart);
			lineStart = lineEnd + 1;

			// A frame the map accounts for. One with no position in a file is the engine's
			// helpers, or esbuild's, calling each other -- true, and of no use to whoever has
			// to find the line to change.
			size_t search = 0;
			Position position;
			Origin origin;
			if (!nextPosition(line, search, position) || !find(position.line, position.column, origin)) continue;
			size_t end = line.size();
			while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\r')) --end;
			if (!out.empty()) out += "\n";
			out += rewrite(line.substr(0, end));
		}
		return out;
	}
}
