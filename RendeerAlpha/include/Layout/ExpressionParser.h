#pragma once
#include <Layout/Expression.h>
#include <functional>
#include <string>
#include <vector>

// Turning the source of a binding into a program.
//
// A binding is written as a thunk -- `text={() => `${state.count} items`}` -- and the
// layout compiler recovers its source with Function.prototype.toString(). This parses
// that source into stack operations.
//
// The grammar is deliberately small: literals, `state.<name>` reads, arithmetic,
// comparison, logical operators, ternaries, template literals, a call to a function the
// layout defined, and -- in a handler -- assignment to a signal and the one parameter
// naming the value the widget passed. No indexing, no arrays or objects, no control flow.
//
// What falls outside it is not lost: the compiler then compiles the whole binding to
// JavaScript instead (see docs/architecture/runtime-code.md). This grammar is the fast
// path -- no runtime, dependencies known at build time -- and is tried first.
namespace RDA::Layout {

	struct ParseResult {
		bool        ok = false;
		Program     program;
		std::string error;      // what was wrong, in words a person can act on
		size_t      position = 0; // where in the source, for pointing at it
		// The layout's own functions the binding calls, by name, each once.
		std::vector<std::string> calls;
	};

	// Whether a name followed by `(` is a function the layout defined at its top level,
	// and so may be called. Empty -- the default -- refuses every call, as before.
	using CallableName = std::function<bool(const std::string&)>;

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
	                         const std::string& rowParam = {},
	                         const CallableName& callable = {});

	// The source with a caret under `position`, for an error message that shows the spot
	// rather than describing it.
	std::string pointAt(const std::string& source, size_t position);
}
