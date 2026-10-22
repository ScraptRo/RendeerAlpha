#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace RDA {
	class Signals;
	class Table;
}

// A compiled expression: what a binding actually is.
//
// `text={() => `${state.count} items`}` does not survive into a running application as
// JavaScript. It is parsed at build time and becomes the program below — a short
// sequence of stack operations over constants and signal reads. Evaluating it needs no
// interpreter, which is the property that lets a shipped binary contain no JavaScript
// engine while still being reactive.
//
// The instruction is a struct rather than a packed byte stream. Expressions are a
// handful of operations each and there are as many of them as there are bound
// properties, so density buys nothing here and costs the ability to read a disassembly.
//
// Signals are referenced by position in the program's own `signals` list rather than by
// a global index, so a program is independent of the table it will be evaluated against
// and the same compiled layout can be instantiated more than once.
namespace RDA::Layout {

	enum class Op : uint8_t {
		Halt,        // stop; the top of the stack is the result

		PushNumber,  // operand: index into `numbers`
		PushText,    // operand: index into `texts`
		PushBool,    // operand: 0 or 1
		LoadSignal,  // operand: index into `signals`; pushes its current value
		StoreSignal, // operand: index into `signals`; pops a value and writes it

		Add, Sub, Mul, Div, Neg,
		Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
		And, Or, Not,

		Select,      // pops b, a, condition; pushes a when the condition holds
		Concat,      // pops b, a; pushes a followed by b, as text
		ToText,      // converts the top of the stack to text

		Pop,         // discards the top; how a handler's statements are separated

		// Appended rather than filed with the other pushes, so every opcode above keeps
		// the number it already had and a blueprint compiled before this existed still
		// decodes to the instructions it was written with.
		PushEvent,   // pushes the value the handler was called with
		// operand: a slot in `signals` -- the pool holds every name a program refers to,
		// and which registry a name belongs to is decided by the opcode that reads it --
		// in the low 24 bits, and the number of arguments it pops in the high 8, laid out
		// as CallScript's are. A blueprint from before arguments has zero there. Pushes
		// whether the command ran, so a statement still leaves one value behind.
		CallCommand,
		// operand: a slot in `signals` holding a marked column name, resolved to a column
		// index when the layout loads. Reads that column of whichever row the template is
		// showing at the moment it is evaluated.
		PushRowField,
		// A function the layout defined, run by the script runtime. The low 24 bits are a
		// slot in `signals` naming it ("$formatSize"), the high 8 the number of arguments
		// it pops; it pushes what the function returned. See docs/architecture/runtime-code.md.
		CallScript,
		// A whole binding that the grammar above does not cover, compiled to JavaScript
		// instead: operand is a slot naming it ("%3", or "%r3" for one that reads the row).
		// Called with the row and the event, if there are any, and pushes its result.
		RunScript,
	};

	inline constexpr uint32_t kScriptSlotMask = 0x00FFFFFFu;
	inline constexpr uint32_t kScriptArgShift = 24;

	struct Instruction {
		Op       op = Op::Halt;
		uint32_t operand = 0;
	};

	// What an expression evaluates to. Deliberately the same three types a signal can
	// hold: the moment a binding needs something richer, the thing it wants is
	// application state rather than a property of a widget.
	struct Value {
		enum class Kind : uint8_t { Number, Bool, Text };

		Kind        kind = Kind::Number;
		double      number = 0.0;
		std::string text;

		static Value fromNumber(double v) { Value x; x.kind = Kind::Number; x.number = v; return x; }
		static Value fromBool(bool v)     { Value x; x.kind = Kind::Bool;   x.number = v ? 1.0 : 0.0; return x; }
		static Value fromText(std::string v) { Value x; x.kind = Kind::Text; x.text = std::move(v); return x; }

		bool        truthy() const;
		double      asNumber() const;
		std::string asText() const;
	};

	// The names a program refers to are pooled once, and resolved to ids when it is
	// loaded. Most are signals; a slot named by CallCommand is a command instead. Sharing
	// one pool is what lets a compiled layout carry commands without the file format
	// growing a section for them.
	struct Program {
		std::vector<Instruction> code;
		std::vector<double>      numbers; // constant pool
		std::vector<std::string> texts;   // constant pool
		// Every signal this program reads or writes, by name. The order is the order
		// LoadSignal and StoreSignal address them by, and it doubles as the dependency
		// list a binding registers with.
		std::vector<std::string> signals;

		bool empty() const { return code.empty(); }
		// Whether running it leaves the VM: a program that does has dependencies nobody
		// could list at build time, and is watched while it runs instead.
		bool usesScript() const {
			for (const Instruction& one : code)
				if (one.op == Op::CallScript || one.op == Op::RunScript) return true;
			return false;
		}
	};

	// Which row a row template's bindings are showing while they are evaluated. Supplied
	// by the list, once per row it rebinds -- which is the whole of what recycling is.
	struct RowRef {
		RDA::Table* table = nullptr;
		size_t      index = 0;
	};

	struct EvalResult {
		bool        ok = false;
		Value       value;
		std::string error; // only when ok is false
		// The failure was a script throwing rather than the program being malformed. A
		// broken program stays broken; a function that threw on one value may well not on
		// the next, so a binding that failed this way is kept and run again.
		bool        scriptError = false;
	};

	// How the VM reaches the script runtime, without depending on it. Set by ScriptHost
	// when it starts; null in a process that has loaded no layout with functions in it,
	// and in the tests that exercise the VM alone. `id` is what the program's slot was
	// resolved to when the layout loaded.
	struct ScriptHooks {
		bool (*call)(uint32_t id, const Value* args, size_t argc, Value& out, std::string& error) = nullptr;
		bool (*run)(uint32_t id, const RowRef* row, const Value* event, Value& out, std::string& error) = nullptr;
	};
	void setScriptHooks(const ScriptHooks* hooks);

	// Runs `program` against `table`. `signalIds` maps the program's signal slots to
	// entries in that table and must be as long as program.signals; resolving names once
	// when a binding is created keeps a lookup out of every evaluation.
	//
	// Failure is a bad program rather than a bad input: a stack underflow or an
	// out-of-range operand means the compiler emitted something wrong, so it reports
	// rather than pretending a value.
	// `event` is the value a handler was called with: the new text of a field, the new
	// state of a checkbox, the new position of a slider. A value binding is called with
	// nothing, and a program that reads an event value when there is none fails rather
	// than quietly reading zero.
	EvalResult evaluate(const Program& program, RDA::Signals& table,
	                    const std::vector<uint32_t>& signalIds,
	                    const Value* event = nullptr, const RowRef* row = nullptr);

	// The program as text, one instruction per line. For diagnostics and for tests that
	// would otherwise assert on opcode numbers nobody can read.
	std::string disassemble(const Program& program);
}
