#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace RDA {
	class Signals;
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
	};

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

	struct Program {
		std::vector<Instruction> code;
		std::vector<double>      numbers; // constant pool
		std::vector<std::string> texts;   // constant pool
		// Every signal this program reads or writes, by name. The order is the order
		// LoadSignal and StoreSignal address them by, and it doubles as the dependency
		// list a binding registers with.
		std::vector<std::string> signals;

		bool empty() const { return code.empty(); }
	};

	struct EvalResult {
		bool        ok = false;
		Value       value;
		std::string error; // only when ok is false
	};

	// Runs `program` against `table`. `signalIds` maps the program's signal slots to
	// entries in that table and must be as long as program.signals; resolving names once
	// when a binding is created keeps a lookup out of every evaluation.
	//
	// Failure is a bad program rather than a bad input: a stack underflow or an
	// out-of-range operand means the compiler emitted something wrong, so it reports
	// rather than pretending a value.
	EvalResult evaluate(const Program& program, RDA::Signals& table,
	                    const std::vector<uint32_t>& signalIds);

	// The program as text, one instruction per line. For diagnostics and for tests that
	// would otherwise assert on opcode numbers nobody can read.
	std::string disassemble(const Program& program);
}
