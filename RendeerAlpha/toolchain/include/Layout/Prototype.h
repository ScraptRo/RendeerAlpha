#pragma once
#include <Layout/StateSchema.h>
#include <string>
#include <vector>

// A declaration, brought to life with nothing behind it.
//
// An interface is written against `state`, `tables` and `commands` from res/state.ts, and
// normally a backend supplies all three: it defines the signals, fills the rows and
// answers the commands. That is the right arrangement and it is also the thing standing
// between somebody and *looking at the screen they are designing* -- you cannot see a
// list until something has written rows into it, and you cannot see a detail page until
// something has set the signal it reads.
//
// So this is the other half: everything the declaration promises, standing in for a
// backend that has not been written. Every signal at the value it was declared with,
// every table present, every command bound to something that says it was called. Then
// placeholder contents on top -- from the command line for one value, from a JSON file
// for a screenful, or generated when all you need is "fifty rows of something".
//
// It is not a mock framework and it does not pretend to be a backend. Nothing here
// computes, validates or persists -- what state.ts computes and answers runs as it would
// under any backend, because that is state.ts's, not a backend's. This exists so that
// `rda prototype` can put a layout on the screen with plausible things in it.
namespace RDA::Layout {

	struct PrototypeResult {
		bool        ok = false;
		std::string error;
		size_t      signals = 0;
		size_t      tables = 0;
		size_t      commands = 0;
		size_t      rows = 0;    // rows written across every table
	};

	// Defines every signal, table and command the declaration names.
	//
	// Signals take the value they were declared with, which is the same value the
	// generated C++ would have given them -- a prototype starts where the real thing
	// starts. Tables are created with their columns and no rows. Commands are bound to
	// `onCommand`, if one is given, with the arguments they were called with as a JSON
	// array, so a tool can report what an interface asked for rather than letting the
	// press do nothing at all. An action still runs: that binding only watches it.
	//
	// `onlyNew` leaves everything that already exists as it is -- for a declaration that
	// changed while the interface was running, which should gain its new names without
	// the old ones going back to where they started.
	PrototypeResult definePrototypeState(const StateSchema& schema,
	                                     void (*onCommand)(const char* name, const char* args) = nullptr,
	                                     bool onlyNew = false);

	// Placeholder contents from a JSON file. Keys are names the declaration already
	// knows:
	//
	//   {
	//     "count": 12,
	//     "text_box": "a line somebody typed",
	//     "products": [ { "title": "Bolt", "price": 3, "inStock": true } ]
	//   }
	//
	// A key naming a signal sets it; a key naming a table fills it from an array of row
	// objects. A key naming neither is an error rather than a shrug: a misspelling in a
	// mock file is a screen that stays empty for a reason nobody can see.
	PrototypeResult loadPrototypeData(const std::string& jsonPath);

	// One `name=value` from a command line. The value is read as the signal's declared
	// type, so `--set count=12` is a number and `--set title=Bolt` is text without
	// either having to say so.
	bool setPrototypeValue(const std::string& assignment, std::string& error);

	// `name=count`: fill a table with that many generated rows.
	//
	// Numbers count up, text takes the column's name and the row's number, booleans
	// alternate. It is deliberately obvious placeholder data -- the point is to see how
	// a list behaves with fifty rows in it, and data that looked real would be data
	// somebody eventually mistakes for real.
	bool fillPrototypeRows(const std::string& assignment, std::string& error);
}
