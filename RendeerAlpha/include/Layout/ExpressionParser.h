#pragma once
#include <Layout/Expression.h>
#include <string>

// Turning the source of a binding into a program.
//
// A binding is written as a thunk -- `text={() => `${state.count} items`}` -- and the
// layout compiler recovers its source with Function.prototype.toString(). This parses
// that source into stack operations.
//
// The grammar is deliberately small: literals, `state.<name>` reads, arithmetic,
// comparison, logical operators, ternaries, template literals, and -- in a handler --
// assignment to a signal and the one parameter naming the value the widget passed.
// Nothing else. No calls, no indexing, no arrays or objects, no control flow.
//
// Everything outside it is a *reported error*, never a silent acceptance. A binding that
// looks reactive and is not is the worst failure this system can have: it works when
// first written, and stops being true later without anything going wrong visibly. So
// `() => formatDate(x)` is refused, by name, with the reason -- rather than compiled
// into something that evaluates once.
namespace RDA::Layout {

	struct ParseResult {
		bool        ok = false;
		Program     program;
		std::string error;      // what was wrong, in words a person can act on
		size_t      position = 0; // where in the source, for pointing at it
	};

	// Parses a whole thunk, including its `() =>` prefix. A body in braces is a sequence
	// of statements, which is what an event handler usually is; a bare expression is what
	// a value binding usually is. Both are accepted either way round.
	// `isHandler` says this is an event handler rather than a value binding. A handler is
	// called with the widget's new value, so it may name one parameter and read it; a
	// binding is called with nothing, so naming one there is refused rather than compiled
	// into something that waits for a value that never arrives.
	// `rowParam` names the parameter a row template was written with, so `item.price`
	// inside one compiles to a column read. Empty everywhere else, which is what makes
	// the same name outside a template the ordinary "not in scope" error.
	ParseResult parseBinding(const std::string& source, bool isHandler = false,
	                         const std::string& rowParam = {});

	// The source with a caret under `position`, for an error message that shows the spot
	// rather than describing it.
	std::string pointAt(const std::string& source, size_t position);
}
