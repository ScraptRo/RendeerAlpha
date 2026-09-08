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
//   export const state = {
//     count: 0,
//     details: false,
//     title: { value: "Sandbox", doc: "shown in the title bar" },
//   }
//   export const commands = { save: "write the notes out" }
//   export const tables = { products: { title: "", price: 0 } }
//   export const routes = { home: "hello", catalogue: { layout: "catalogue" } }
//
// Declaring routes adds a `route` signal holding the name of the screen showing, and the
// commands `back` and `forward`. Navigation is then an ordinary write -- `state.route =
// "catalogue"` -- which is why it needed no new grammar and why it is reactive for free.
//
// TypeScript, like everything else an application declares. A field's type is whatever
// its initial value is, which removes the ceremony of saying it twice; where one wants
// documenting, { value, doc } takes the place of the bare value.
//
// It was XML, and the XML reader is still here for a declaration written before the
// move. Nothing new should be written in it.
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
		// When the field can only hold certain words, the words. Generated TypeScript
		// then types it as a union rather than as `string`, so a misspelling is an error
		// where it is written instead of a signal that quietly holds nonsense. `routes`
		// is what fills this today.
		std::vector<std::string> choices;
	};

	// Work the interface can ask the application to do. Declared beside state because it
	// crosses the same boundary and needs the same guarantee: a name a layout calls is a
	// name the application registered, or neither side builds.
	struct StateCommand {
		std::string name;
		std::string doc;
	};

	// Rows of data a list shows. Declared here for the same reason a signal is: the
	// column a layout reads has to be a column the application filled, and one file
	// saying so is the only way both sides can be checked against it.
	struct StateTable {
		std::string             name;
		std::string             doc;
		std::vector<StateField> columns;
	};

	// One screen. A route names a layout, and optionally the signals that make up its
	// arguments -- which page of the catalogue, which product.
	//
	// Params are ordinary signals rather than a second kind of value: navigation writes
	// them the way it writes anything, and naming them here is what lets going back
	// restore them. That is the same trade commands make, and for the same reason -- one
	// channel for data is enough.
	struct StateRoute {
		std::string              name;
		std::string              layout; // blueprint stem, resolved against the layout dir
		std::string              doc;
		std::vector<std::string> params; // names of declared signals
	};

	struct StateSchema {
		bool                      ok = false;
		std::string               error;
		std::string               source; // where it was read from, for the generated banner
		std::vector<StateField>   fields;
		std::vector<StateCommand> commands;
		std::vector<StateTable>   tables;
		std::vector<StateRoute>   routes;

		bool empty() const {
			return fields.empty() && commands.empty() && tables.empty() && routes.empty();
		}
	};

	// Reads a declaration. A missing file is an error rather than an empty schema: a
	// caller that asked for one is expecting it to exist.
	StateSchema loadStateSchema(const std::string& path);

	// Writes the C++ that declares the signals and gives typed access to them. Generating
	// the accessors as well as the definitions is what stops the C++ side misspelling a
	// name too — `State::setCount(3)` either compiles or does not.
	// The same declaration as a Python module: one property per signal, one decorator
	// per command. Generated rather than looked up dynamically so that a misspelled
	// name is an AttributeError where it is written, and an editor can complete it --
	// the same guarantee the generated C++ and TypeScript give their languages.
	bool emitStatePython(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error);

	// The same declaration as an ES module, for a Node backend.
	//
	// Written as an object of real accessors rather than a Proxy: an editor infers the
	// type of every signal from its getter and completes the names, and the object is
	// made non-extensible so that assigning a name that is not there throws where it is
	// written -- ES modules are strict, so `State.notAThing = 1` is a TypeError rather
	// than a property nobody reads.
	bool emitStateNode(const StateSchema& schema, const std::string& outputPath,
	                   std::string& error);

	// The same declaration as C#, for a .NET backend.
	//
	// The one generator whose output the compiler checks as thoroughly as the engine
	// does: a row is a generated struct rather than a dictionary, so a misspelled column
	// is a build error rather than something the binding notices at run time.
	bool emitStateCSharp(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error);

	bool emitStateHeader(const StateSchema& schema, const std::string& outputPath,
	                     std::string& error);
}
