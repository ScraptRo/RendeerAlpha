#pragma once
#include <string>
#include <string_view>
#include <vector>

// The little JSON the engine itself has to read and write: a command's arguments.
//
// Arguments cross into Python, Node and C#, each of which parses JSON natively, so JSON is
// what they are -- one array per call, `[3, "notes.txt"]`. The C++ side needs to build that
// array from the values a binding produced, and to read one back for a handler written in
// C++, and that is all: no objects to walk, no documents. An argument that is itself an
// array or an object -- something run-time code passed -- is kept as its own JSON text for
// whoever asked for it to read.
//
// Malformed input is refused rather than guessed at: an array that arrives from a C ABI
// caller is something to check, not something to trust.
namespace RDA::Json {

	// Appends `text` as a quoted JSON string. Control characters are escaped, and so is
	// everything JSON requires; UTF-8 passes through as it is.
	void appendString(std::string& out, std::string_view text);

	// Appends a number as JSON writes one. JSON has no NaN or Infinity, so those are null,
	// which every reader takes rather than chokes on.
	void appendNumber(std::string& out, double value);

	struct Item {
		enum class Kind { Null, Bool, Number, Text, Array, Object };
		Kind        kind = Kind::Null;
		bool        flag = false;
		double      number = 0.0;
		std::string text;  // a Text's value, unescaped
		std::string raw;   // the item's own JSON, exactly as it was written
	};

	// The elements of a JSON array, in order. False, with `error` saying where, when the
	// text is not one array of valid JSON with nothing after it.
	bool readArray(std::string_view json, std::vector<Item>& out, std::string* error = nullptr);
}
