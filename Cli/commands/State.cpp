// `state`, `python`, `node` and `csharp`: one state declaration, written out as the code a
// backend is written against. The four differ in what they emit and in what they say about
// it, and in nothing else, so they are one function and a table.
#include "Command.h"

#include <cstdio>

namespace Cli {

	namespace {

		struct StateTarget {
			bool (*emit)(const RDA::Layout::StateSchema&, const std::string&, std::string&);
			// The C++ header says how many signals it defined and stops there; the others
			// carry the commands too, and say so.
			bool countsCommands;
		};

		// Generates the C++ side of a state declaration: the signal definitions, and typed
		// access to them. The same file feeds `rda types`, which is the point -- one
		// declaration, and both sides of the boundary agree by construction.
		constexpr StateTarget kCpp = { RDA::Layout::emitStateHeader, false };

		// The same declaration as Python. A backend written in Python reaches the engine
		// through its C ABI, and this is what gives it names to reach with.
		constexpr StateTarget kPython = { RDA::Layout::emitStatePython, true };

		// The same declaration as an ES module. Node reaches the engine through the same C
		// ABI Python does; what differs is which side calls the other, and the binding is
		// where that difference lives.
		constexpr StateTarget kNode = { RDA::Layout::emitStateNode, true };

		// The same declaration as C#. The one generator whose output the consuming compiler
		// checks as thoroughly as the engine does.
		constexpr StateTarget kCSharp = { RDA::Layout::emitStateCSharp, true };

		int generate(const StateTarget& target, const std::string& source, const std::string& output) {
			RDA::Layout::StateSchema schema;
			if (!loadStateWithCode(source, schema)) return 1;
			std::string error;
			if (!target.emit(schema, output, error)) {
				std::printf("rda: %s\n", error.c_str());
				return 1;
			}
			if (target.countsCommands) {
				std::printf("rda: %s -> %s\n"
				            "     %zu signals, %zu commands%s\n", source.c_str(), output.c_str(),
				            schema.fields.size(), schema.commands.size(), stateSummary(schema).c_str());
			} else {
				std::printf("rda: %s -> %s\n"
				            "     %zu signals%s\n", source.c_str(), output.c_str(), schema.fields.size(),
				            stateSummary(schema).c_str());
			}
			return 0;
		}
	}

	static int cmdState(const Args& args)  { return generate(kCpp,    argument(args, 1), argument(args, 2)); }
	static int cmdPython(const Args& args) { return generate(kPython, argument(args, 1), argument(args, 2)); }
	static int cmdNode(const Args& args)   { return generate(kNode,   argument(args, 1), argument(args, 2)); }
	static int cmdCSharp(const Args& args) { return generate(kCSharp, argument(args, 1), argument(args, 2)); }

	namespace {
		const Command kStateCommand{ __FILE__, "state", Section::Build, 2,
			"  rda state  <state.ts>  <out.h>     generate the C++ for a state declaration\n",
			cmdState };
		const Command kPythonCommand{ __FILE__, "python", Section::Build, 2,
			"  rda python <state.ts>  <out.py>    generate the Python for a state declaration\n",
			cmdPython };
		const Command kNodeCommand{ __FILE__, "node", Section::Build, 2,
			"  rda node   <state.ts>  <out.mjs>   generate the JavaScript for a state declaration\n",
			cmdNode };
		const Command kCSharpCommand{ __FILE__, "csharp", Section::Build, 2,
			"  rda csharp <state.ts>  <out.cs>    generate the C# for a state declaration\n",
			cmdCSharp };
	}
}
