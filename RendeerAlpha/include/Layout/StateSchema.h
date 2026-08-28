#pragma once
#include <string>
#include <vector>

// What an application's state is called, and what it holds.
//
// Three things need to agree about it: the C++ that declares the signals, the C++ that
// reads and writes them, and the TypeScript a layout is checked against. Written out
// three times they drift — and the drift is invisible in the worst way, because a
// misspelled signal is not an error anywhere. The binding reads a signal that gets
// created on demand, holds zero forever, and looks like it works.
//
// So it is declared once, in a small file, and everything else is generated:
//
//   <state>
//     <number name="count"   value="0"     doc="how many times the button was pressed"/>
//     <bool   name="details" value="false" doc="whether the arithmetic is shown"/>
//     <text   name="title"   value="Sandbox"/>
//   </state>
//
// The format is XML because that is what this project already reads for themes, and
// because a declaration this small should not need a parser of its own.
namespace RDA::Layout {

	enum class StateType {
		Number,
		Bool,
		Text,
	};

	struct StateField {
		std::string name;
		StateType   type = StateType::Number;
		std::string value; // the initial value, as written
		std::string doc;
	};

	struct StateSchema {
		bool                    ok = false;
		std::string             error;
		std::string             source; // where it was read from, for the generated banner
		std::vector<StateField> fields;

		bool empty() const { return fields.empty(); }
	};

	// Reads a declaration. A missing file is an error rather than an empty schema: a
	// caller that asked for one is expecting it to exist.
	StateSchema loadStateSchema(const std::string& path);

	// Writes the C++ that declares the signals and gives typed access to them. Generating
	// the accessors as well as the definitions is what stops the C++ side misspelling a
	// name too — `State::setCount(3)` either compiles or does not.
	bool emitStateHeader(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error);
}
