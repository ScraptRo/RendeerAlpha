#include <Layout/Expression.h>
#include <Core/Commands.h>
#include <Core/Tables.h>
#include <Core/Signals.h>

#include <cmath>
#include <sstream>

namespace RDA::Layout {

	namespace {
		// Numbers print the way a person would write them: no trailing zeros, and no
		// exponent for the sizes an interface deals in. A count rendered as "3.000000"
		// is the kind of detail that makes a framework feel unfinished.
		std::string numberToText(double value) {
			if (std::isnan(value)) return "NaN";
			if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
			if (value == static_cast<long long>(value) && std::abs(value) < 1e15) {
				return std::to_string(static_cast<long long>(value));
			}
			std::ostringstream out;
			out.precision(6);
			out << value;
			return out.str();
		}
	}

	bool Value::truthy() const {
		switch (kind) {
		case Kind::Number: return number != 0.0;
		case Kind::Bool:   return number != 0.0;
		case Kind::Text:   return !text.empty();
		}
		return false;
	}

	double Value::asNumber() const {
		if (kind == Kind::Text) {
			try { return std::stod(text); } catch (...) { return 0.0; }
		}
		return number;
	}

	std::string Value::asText() const {
		switch (kind) {
		case Kind::Number: return numberToText(number);
		case Kind::Bool:   return number != 0.0 ? "true" : "false";
		case Kind::Text:   return text;
		}
		return {};
	}

	EvalResult evaluate(const Program& program, RDA::Signals& table,
	                    const std::vector<uint32_t>& signalIds,
	                    const Value* event, const RowRef* row) {
		EvalResult result;

		if (signalIds.size() != program.signals.size()) {
			result.error = "the program's signals were not resolved";
			return result;
		}

		std::vector<Value> stack;
		stack.reserve(8);

		auto pop = [&stack](Value& out) -> bool {
			if (stack.empty()) return false;
			out = std::move(stack.back());
			stack.pop_back();
			return true;
		};

		for (const Instruction& instruction : program.code) {
			switch (instruction.op) {
			case Op::Halt:
				break;

			case Op::PushNumber:
				if (instruction.operand >= program.numbers.size()) {
					result.error = "number constant out of range";
					return result;
				}
				stack.push_back(Value::fromNumber(program.numbers[instruction.operand]));
				break;

			case Op::PushText:
				if (instruction.operand >= program.texts.size()) {
					result.error = "text constant out of range";
					return result;
				}
				stack.push_back(Value::fromText(program.texts[instruction.operand]));
				break;

			case Op::PushBool:
				stack.push_back(Value::fromBool(instruction.operand != 0));
				break;

			case Op::LoadSignal: {
				if (instruction.operand >= signalIds.size()) {
					result.error = "signal slot out of range";
					return result;
				}
				const uint32_t id = signalIds[instruction.operand];
				switch (table.type(id)) {
				case SignalType::Number: stack.push_back(Value::fromNumber(table.number(id))); break;
				case SignalType::Bool:   stack.push_back(Value::fromBool(table.boolean(id)));  break;
				case SignalType::Text:   stack.push_back(Value::fromText(std::string(table.text(id)))); break;
				}
				break;
			}

			case Op::PushRowField: {
				if (instruction.operand >= signalIds.size()) {
					result.error = "column slot out of range";
					return result;
				}
				if (!row || !row->table) {
					result.error = "this program reads a row's column, but it is not part "
					               "of a row template";
					return result;
				}
				const uint32_t column = signalIds[instruction.operand];
				if (column == kNoColumn) {
					result.error = "the table has no such column";
					return result;
				}
				switch (row->table->columnType(column)) {
				case ColumnType::Number:
					stack.push_back(Value::fromNumber(row->table->number(row->index, column)));
					break;
				case ColumnType::Bool:
					stack.push_back(Value::fromBool(row->table->boolean(row->index, column)));
					break;
				case ColumnType::Text:
					stack.push_back(Value::fromText(std::string(row->table->text(row->index, column))));
					break;
				}
				break;
			}

			case Op::CallCommand: {
				if (instruction.operand >= signalIds.size()) {
					result.error = "command slot out of range";
					return result;
				}
				// The registry is reached directly rather than passed in, because unlike
				// signals there is one of it: a command names work in this application,
				// and a second registry would mean two answers for one name.
				const bool ran = commands().invoke(signalIds[instruction.operand]);
				if (!ran) {
					result.error = "nothing is bound to this command";
					return result;
				}
				stack.push_back(Value::fromBool(true));
				break;
			}

			case Op::PushEvent:
				if (!event) {
					result.error = "this program reads the value it was called with, but "
					               "nothing passed one";
					return result;
				}
				stack.push_back(*event);
				break;

			case Op::StoreSignal: {
				if (instruction.operand >= signalIds.size()) {
					result.error = "signal slot out of range";
					return result;
				}
				Value value;
				if (!pop(value)) { result.error = "nothing to store"; return result; }
				const uint32_t id = signalIds[instruction.operand];
				// Written through the signal's own type rather than the expression's, so
				// `state.count = "3"` lands as the number three rather than changing what
				// the signal is.
				switch (table.type(id)) {
				case SignalType::Number: table.set(id, value.asNumber()); break;
				case SignalType::Bool:   table.set(id, value.truthy()); break;
				case SignalType::Text:   table.set(id, std::string_view(value.asText())); break;
				}
				// A store leaves the value behind, so `state.a = state.b = 1` works and a
				// statement can be discarded with Pop.
				stack.push_back(std::move(value));
				break;
			}

			case Op::Neg: {
				Value a;
				if (!pop(a)) { result.error = "stack underflow at neg"; return result; }
				stack.push_back(Value::fromNumber(-a.asNumber()));
				break;
			}

			case Op::Not: {
				Value a;
				if (!pop(a)) { result.error = "stack underflow at not"; return result; }
				stack.push_back(Value::fromBool(!a.truthy()));
				break;
			}

			case Op::ToText: {
				Value a;
				if (!pop(a)) { result.error = "stack underflow at toText"; return result; }
				stack.push_back(Value::fromText(a.asText()));
				break;
			}

			case Op::Pop: {
				Value discarded;
				if (!pop(discarded)) { result.error = "stack underflow at pop"; return result; }
				break;
			}

			case Op::Select: {
				Value b, a, condition;
				if (!pop(b) || !pop(a) || !pop(condition)) {
					result.error = "stack underflow at select";
					return result;
				}
				stack.push_back(condition.truthy() ? std::move(a) : std::move(b));
				break;
			}

			default: {
				// Everything remaining is binary and pops its operands the same way.
				Value b, a;
				if (!pop(b) || !pop(a)) { result.error = "stack underflow"; return result; }

				switch (instruction.op) {
				case Op::Add:
					// Following JavaScript here on purpose: a layout is written in it, and
					// `"items: " + count` doing arithmetic instead would surprise everyone.
					if (a.kind == Value::Kind::Text || b.kind == Value::Kind::Text) {
						stack.push_back(Value::fromText(a.asText() + b.asText()));
					} else {
						stack.push_back(Value::fromNumber(a.asNumber() + b.asNumber()));
					}
					break;
				case Op::Concat: stack.push_back(Value::fromText(a.asText() + b.asText())); break;
				case Op::Sub:    stack.push_back(Value::fromNumber(a.asNumber() - b.asNumber())); break;
				case Op::Mul:    stack.push_back(Value::fromNumber(a.asNumber() * b.asNumber())); break;
				case Op::Div: {
					const double divisor = b.asNumber();
					// Division by zero yields infinity rather than failing: it is what the
					// language a layout is written in does, and a binding that refuses to
					// evaluate is worse on screen than one showing "Infinity".
					stack.push_back(Value::fromNumber(a.asNumber() / divisor));
					break;
				}
				case Op::Equal:
					if (a.kind == Value::Kind::Text || b.kind == Value::Kind::Text) {
						stack.push_back(Value::fromBool(a.asText() == b.asText()));
					} else {
						stack.push_back(Value::fromBool(a.asNumber() == b.asNumber()));
					}
					break;
				case Op::NotEqual:
					if (a.kind == Value::Kind::Text || b.kind == Value::Kind::Text) {
						stack.push_back(Value::fromBool(a.asText() != b.asText()));
					} else {
						stack.push_back(Value::fromBool(a.asNumber() != b.asNumber()));
					}
					break;
				case Op::Less:         stack.push_back(Value::fromBool(a.asNumber() <  b.asNumber())); break;
				case Op::LessEqual:    stack.push_back(Value::fromBool(a.asNumber() <= b.asNumber())); break;
				case Op::Greater:      stack.push_back(Value::fromBool(a.asNumber() >  b.asNumber())); break;
				case Op::GreaterEqual: stack.push_back(Value::fromBool(a.asNumber() >= b.asNumber())); break;
				case Op::And:          stack.push_back(Value::fromBool(a.truthy() && b.truthy())); break;
				case Op::Or:           stack.push_back(Value::fromBool(a.truthy() || b.truthy())); break;
				default:
					result.error = "unknown instruction";
					return result;
				}
				break;
			}
			}
		}

		if (stack.empty()) {
			result.error = "the program produced no value";
			return result;
		}
		result.value = std::move(stack.back());
		result.ok = true;
		return result;
	}

	std::string disassemble(const Program& program) {
		static const char* kNames[] = {
			"halt", "push.num", "push.text", "push.bool", "load", "store",
			"add", "sub", "mul", "div", "neg",
			"eq", "ne", "lt", "le", "gt", "ge",
			"and", "or", "not",
			"select", "concat", "totext", "pop",
			"push.event", "call", "push.field",
		};

		std::ostringstream out;
		for (size_t i = 0; i < program.code.size(); ++i) {
			const Instruction& instruction = program.code[i];
			const size_t index = static_cast<size_t>(instruction.op);
			out << (index < std::size(kNames) ? kNames[index] : "op?");

			switch (instruction.op) {
			case Op::PushNumber:
				if (instruction.operand < program.numbers.size()) {
					out << " " << numberToText(program.numbers[instruction.operand]);
				}
				break;
			case Op::PushText:
				if (instruction.operand < program.texts.size()) {
					out << " \"" << program.texts[instruction.operand] << "\"";
				}
				break;
			case Op::PushBool:
				out << (instruction.operand ? " true" : " false");
				break;
			case Op::LoadSignal:
			case Op::CallCommand:
			case Op::PushRowField:
			case Op::StoreSignal:
				if (instruction.operand < program.signals.size()) {
					out << " " << program.signals[instruction.operand];
				}
				break;
			default:
				break;
			}
			out << "\n";
		}
		return out.str();
	}
}
