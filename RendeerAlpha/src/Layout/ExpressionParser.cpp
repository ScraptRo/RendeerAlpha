#include <Layout/ExpressionParser.h>

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace RDA::Layout {

	namespace {

		// The one identifier a binding may read through. Anything else is refused, which
		// is what keeps "reactive" meaning something: every dependency is visible in the
		// source as `state.<name>`, and the compiler can therefore know all of them.
		constexpr const char* kStateRoot = "state";
		constexpr const char* kCommandRoot = "commands";
		// The one name under `state` that is a group rather than a signal: what the
		// engine knows about itself, `state.rda.width` and so on. Flat everywhere else,
		// because a signal is a name and a value and nesting it would be a second idea
		// -- but the engine's own values are not the application's, and putting them in
		// the same flat pool would mean an application could not call anything `width`.
		//
		// The dot survives into the signal name: `state.rda.width` is the signal
		// literally called "rda.width". Nothing else can produce that name, since an
		// identifier cannot contain a dot, so the two pools cannot collide.
		constexpr const char* kEngineNamespace = "rda";
		// Command names share the pool with signal names, so they carry a mark no
		// identifier can have. Without it `state.save` and `commands.save()` would intern
		// to one slot with two meanings.
		constexpr char kCommandMark = '@';
		// And column names carry their own, for the same reason.
		constexpr char kColumnMark = '#';

		struct Failure {
			std::string message;
			size_t      position;
		};

		class Parser {
		public:
			Parser(const std::string& source, bool isHandler, std::string rowParam)
				: mSource(source), mIsHandler(isHandler), mRowParam(std::move(rowParam)) {}

			Program parse() {
				skipArrowPrefix();
				skipSpace();
				if (peek() == '{') {
					parseBlock();
				} else {
					parseExpression();
				}
				skipSpace();
				if (!atEnd()) fail("unexpected trailing text");
				mProgram.code.push_back({ Op::Halt, 0 });
				return std::move(mProgram);
			}

		private:
			// ---- source ----------------------------------------------------------

			bool atEnd() const { return mAt >= mSource.size(); }
			char peek(size_t ahead = 0) const {
				return (mAt + ahead < mSource.size()) ? mSource[mAt + ahead] : '\0';
			}
			bool startsWith(const char* text) const {
				return mSource.compare(mAt, std::char_traits<char>::length(text), text) == 0;
			}
			void skipSpace() {
				while (!atEnd() && std::isspace(static_cast<unsigned char>(mSource[mAt]))) ++mAt;
			}
			bool take(const char* text) {
				skipSpace();
				if (!startsWith(text)) return false;
				mAt += std::char_traits<char>::length(text);
				return true;
			}
			[[noreturn]] void fail(const std::string& message) const {
				throw Failure{ message, mAt };
			}

			// ---- emitting --------------------------------------------------------

			void emit(Op op, uint32_t operand = 0) { mProgram.code.push_back({ op, operand }); }

			uint32_t numberConstant(double value) {
				for (size_t i = 0; i < mProgram.numbers.size(); ++i) {
					if (mProgram.numbers[i] == value) return static_cast<uint32_t>(i);
				}
				mProgram.numbers.push_back(value);
				return static_cast<uint32_t>(mProgram.numbers.size() - 1);
			}
			uint32_t textConstant(const std::string& value) {
				for (size_t i = 0; i < mProgram.texts.size(); ++i) {
					if (mProgram.texts[i] == value) return static_cast<uint32_t>(i);
				}
				mProgram.texts.push_back(value);
				return static_cast<uint32_t>(mProgram.texts.size() - 1);
			}
			uint32_t signalSlot(const std::string& name) {
				for (size_t i = 0; i < mProgram.signals.size(); ++i) {
					if (mProgram.signals[i] == name) return static_cast<uint32_t>(i);
				}
				mProgram.signals.push_back(name);
				return static_cast<uint32_t>(mProgram.signals.size() - 1);
			}

			// ---- structure -------------------------------------------------------

			// Records the one parameter a handler may name. A value binding is called
			// with nothing, so a parameter there is refused where it is written rather
			// than becoming a parse error further along.
			void takeParameter(const std::string& name, size_t at) {
				if (!mIsHandler) {
					mAt = at;
					fail("a value binding takes no parameters -- it is called with nothing. "
					     "Only an event handler is handed a value");
				}
				mParameter = name;
			}

			// `() => ...`, and for a handler `(value) => ...` or `value => ...`, where the
			// parameter is whatever the widget passes: the new text of a field, the new
			// state of a checkbox, the new position of a slider.
			void skipArrowPrefix() {
				skipSpace();
				const size_t start = mAt;

				// A single parameter needs no parentheses in TypeScript, and esbuild is
				// free to write it either way.
				if (isIdentifierStart(peek())) {
					const size_t nameAt = mAt;
					const std::string word = readIdentifier();
					skipSpace();
					if (mSource.compare(mAt, 2, "=>") == 0) {
						takeParameter(word, nameAt);
						mAt += 2;
						return;
					}
					mAt = start;
					return;
				}

				if (peek() != '(') return;

				size_t scan = mAt + 1;
				while (scan < mSource.size() && mSource[scan] != ')') ++scan;
				if (scan >= mSource.size()) { mAt = start; return; }

				size_t at = mAt + 1;
				while (at < scan && std::isspace(static_cast<unsigned char>(mSource[at]))) ++at;
				if (at < scan) {
					const size_t nameAt = at;
					mAt = at;
					const std::string word = isIdentifierStart(peek()) ? readIdentifier() : std::string();
					skipSpace();
					// One plain name and nothing else: no default, no destructuring, no
					// second parameter, none of which there is anything to fill in from.
					const bool onePlainName = !word.empty() && mAt == scan;
					if (!onePlainName) {
						mAt = nameAt;
						fail("a handler takes at most one parameter, naming the value the "
						     "widget passes it");
					}
					takeParameter(word, nameAt);
				}

				size_t after = scan + 1;
				while (after < mSource.size() &&
				       std::isspace(static_cast<unsigned char>(mSource[after]))) ++after;
				if (mSource.compare(after, 2, "=>") == 0) { mAt = after + 2; return; }
				mAt = start;
			}

			void parseBlock() {
				if (!take("{")) fail("expected a block");
				bool any = false;
				for (;;) {
					skipSpace();
					if (take("}")) break;
					if (atEnd()) fail("unterminated block");
					if (any) emit(Op::Pop); // only the last statement's value survives
					if (take("return")) {
						// `return x` in a handler is the same as `x` here: there is nothing
						// to return to.
					}
					parseExpression();
					any = true;
					skipSpace();
					take(";");
				}
				if (!any) fail("an empty binding has no value");
			}

			// ---- expressions, loosest binding first ------------------------------

			void parseExpression() { parseAssignment(); }

			void parseAssignment() {
				const size_t before = mAt;
				// An assignment target has to be a signal, and the only way to know is to
				// parse one and see what follows.
				skipSpace();
				if (const std::string name = tryStateName(); !name.empty()) {
					skipSpace();
					// Written by the engine every frame, so a write here would be
					// overwritten before anyone saw it. Said plainly rather than
					// allowed and ignored.
					const bool writesToIt =
						(peek() == '=' && peek(1) != '=') ||   // and not ==
						startsWith("+=") || startsWith("-=") ||
						startsWith("*=") || startsWith("/=") ||
						startsWith("++") || startsWith("--");
					if (isEngineName(name) && writesToIt) {
						fail("state." + name + " is the engine's, and read-only: it is "
						     "rewritten every frame from the window itself");
					}
					const char* compound = nullptr;
					if      (startsWith("+=")) compound = "+";
					else if (startsWith("-=")) compound = "-";
					else if (startsWith("*=")) compound = "*";
					else if (startsWith("/=")) compound = "/";

					if (compound) {
						mAt += 2;
						emit(Op::LoadSignal, signalSlot(name));
						parseAssignment();
						emit(compound[0] == '+' ? Op::Add
						   : compound[0] == '-' ? Op::Sub
						   : compound[0] == '*' ? Op::Mul : Op::Div);
						emit(Op::StoreSignal, signalSlot(name));
						return;
					}
					if (peek() == '=' && peek(1) != '=') {
						++mAt;
						parseAssignment();
						emit(Op::StoreSignal, signalSlot(name));
						return;
					}
					if (startsWith("++") || startsWith("--")) {
						const bool increment = peek() == '+';
						mAt += 2;
						emit(Op::LoadSignal, signalSlot(name));
						emit(Op::PushNumber, numberConstant(1.0));
						emit(increment ? Op::Add : Op::Sub);
						emit(Op::StoreSignal, signalSlot(name));
						return;
					}
				}
				mAt = before;
				parseTernary();
			}

			void parseTernary() {
				parseOr();
				skipSpace();
				if (peek() == '?') {
					++mAt;
					parseExpression();
					skipSpace();
					if (!take(":")) fail("expected ':' to finish the conditional");
					parseExpression();
					emit(Op::Select);
				}
			}

			void parseOr() {
				parseAnd();
				while (take("||")) { parseAnd(); emit(Op::Or); }
			}

			void parseAnd() {
				parseEquality();
				while (take("&&")) { parseEquality(); emit(Op::And); }
			}

			void parseEquality() {
				parseRelational();
				for (;;) {
					skipSpace();
					// The strict forms first, so `===` is not read as `==` followed by `=`.
					if (take("===") || take("==")) { parseRelational(); emit(Op::Equal); }
					else if (take("!==") || take("!=")) { parseRelational(); emit(Op::NotEqual); }
					else break;
				}
			}

			void parseRelational() {
				parseAdditive();
				for (;;) {
					skipSpace();
					if (take("<=")) { parseAdditive(); emit(Op::LessEqual); }
					else if (take(">=")) { parseAdditive(); emit(Op::GreaterEqual); }
					else if (peek() == '<') { ++mAt; parseAdditive(); emit(Op::Less); }
					else if (peek() == '>') { ++mAt; parseAdditive(); emit(Op::Greater); }
					else break;
				}
			}

			void parseAdditive() {
				parseMultiplicative();
				for (;;) {
					skipSpace();
					if (peek() == '+' && peek(1) != '+' && peek(1) != '=') {
						++mAt; parseMultiplicative(); emit(Op::Add);
					} else if (peek() == '-' && peek(1) != '-' && peek(1) != '=') {
						++mAt; parseMultiplicative(); emit(Op::Sub);
					} else break;
				}
			}

			void parseMultiplicative() {
				parseUnary();
				for (;;) {
					skipSpace();
					if (peek() == '*' && peek(1) != '=') { ++mAt; parseUnary(); emit(Op::Mul); }
					else if (peek() == '/' && peek(1) != '=') { ++mAt; parseUnary(); emit(Op::Div); }
					else break;
				}
			}

			void parseUnary() {
				skipSpace();
				if (peek() == '!' && peek(1) != '=') { ++mAt; parseUnary(); emit(Op::Not); return; }
				if (peek() == '-' && peek(1) != '-') { ++mAt; parseUnary(); emit(Op::Neg); return; }
				if (peek() == '+' && peek(1) != '+') { ++mAt; parseUnary(); return; }
				parsePrimary();
			}

			// `state.name`, or empty when this is not one. Never reports; the caller
			// decides whether not-a-signal is an error here.
			std::string tryStateName() {
				const size_t start = mAt;
				skipSpace();
				if (!isIdentifierStart(peek())) { mAt = start; return {}; }
				const std::string root = readIdentifier();
				if (root != kStateRoot || peek() != '.') { mAt = start; return {}; }
				++mAt;
				if (!isIdentifierStart(peek())) { mAt = start; return {}; }
				const std::string name = readIdentifier();
				// The engine's own namespace is the one place a second level is read.
				if (name == kEngineNamespace && peek() == '.') {
					++mAt;
					if (!isIdentifierStart(peek())) { mAt = start; return {}; }
					return name + "." + readIdentifier();
				}
				return name;
			}

			static bool isEngineName(const std::string& name) {
				const std::string prefix = std::string(kEngineNamespace) + ".";
				return name.compare(0, prefix.size(), prefix) == 0;
			}

			static bool isIdentifierStart(char c) {
				return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
			}
			static bool isIdentifierPart(char c) {
				return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
			}

			std::string readIdentifier() {
				const size_t start = mAt;
				while (!atEnd() && isIdentifierPart(mSource[mAt])) ++mAt;
				return mSource.substr(start, mAt - start);
			}

			void parsePrimary() {
				skipSpace();
				if (atEnd()) fail("expected a value");

				const char c = peek();

				if (c == '(') {
					++mAt;
					parseExpression();
					if (!take(")")) fail("expected ')'");
					return;
				}
				if (c == '`') { parseTemplate(); return; }
				if (c == '"' || c == '\'') { parseString(c); return; }
				if (std::isdigit(static_cast<unsigned char>(c)) ||
				    (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
					parseNumber();
					return;
				}
				if (isIdentifierStart(c)) {
					const size_t start = mAt;
					const std::string word = readIdentifier();

					if (word == "true")  { emit(Op::PushBool, 1); return; }
					if (word == "false") { emit(Op::PushBool, 0); return; }

					skipSpace();
					if (peek() == '(') {
						mAt = start;
						fail("calls are not compiled yet -- a binding is an expression over "
						     "state, so move the work into the backend and bind to the result");
					}
					// The handler's parameter. Checked before state, so a handler that
					// names its parameter `state` reads the parameter -- which is what the
					// same code would do in TypeScript.
					if (!mParameter.empty() && word == mParameter) {
						emit(Op::PushEvent);
						return;
					}
					// commands.<name>() -- asking the application to do the work. Only in
					// a handler: a value binding is evaluated whenever the interface is
					// drawn, and something that changes the world cannot live there.
					// item.<column> -- the row this template is showing. Resolved to a
					// column index when the layout loads, so nothing is looked up by name
					// while scrolling.
					if (!mRowParam.empty() && word == mRowParam && peek() == '.') {
						++mAt; // the dot
						skipSpace();
						if (!isIdentifierStart(peek())) {
							fail("expected a column name after '" + mRowParam + ".'");
						}
						const std::string column = readIdentifier();
						skipSpace();
						if (peek() == '.' || peek() == '[') {
							fail("a row is one level deep: " + mRowParam + ".<column>");
						}
						emit(Op::PushRowField, signalSlot(kColumnMark + column));
						return;
					}
					if (word == kCommandRoot && peek() == '.') {
						if (!mIsHandler) {
							mAt = start;
							fail("a value binding cannot call a command -- it is evaluated "
							     "every time the interface is drawn, and a command changes "
							     "something. Call it from an event handler instead");
						}
						++mAt; // the dot
						skipSpace();
						if (!isIdentifierStart(peek())) fail("expected a command name after 'commands.'");
						const std::string name = readIdentifier();
						skipSpace();
						if (!take("(")) fail("a command has to be called: write commands." + name + "()");
						skipSpace();
						if (!take(")")) {
							fail("a command takes no arguments -- write what it needs into "
							     "state first, and let it read that");
						}
						emit(Op::CallCommand, signalSlot(kCommandMark + name));
						return;
					}
					if (word == kStateRoot && peek() == '.') {
						mAt = start;
						const std::string name = tryStateName();
						if (name.empty()) fail("expected a name after 'state.'");
						skipSpace();
						if (peek() == '.' || peek() == '[') {
							fail("only a single level of state is readable: state.<name>. "
							     "The exception is state.rda.<name>, which is what the "
							     "engine knows about itself");
						}
						emit(Op::LoadSignal, signalSlot(name));
						return;
					}
					mAt = start;
					if (word == "props") {
						fail("props are resolved when the layout is compiled, so they cannot "
						     "be read from a binding -- use the value directly");
					}
					// Almost always a variable from the surrounding code. A binding is
					// compiled from its own source, so nothing it closed over came with
					// it -- worth saying, because the code looks perfectly reasonable.
					fail("'" + word + "' is not in scope. A binding is compiled from its "
					     "source, so a variable from the code around it does not come "
					     "along; only state.<name> and literals are readable");
				}
				if (c == '[' || c == '{') {
					fail("arrays and objects are not compiled yet");
				}
				fail(std::string("unexpected '") + c + "'");
			}

			void parseNumber() {
				const size_t start = mAt;
				while (!atEnd() && (std::isdigit(static_cast<unsigned char>(mSource[mAt])) ||
				                    mSource[mAt] == '.')) ++mAt;
				// An exponent, if there is one.
				if (!atEnd() && (mSource[mAt] == 'e' || mSource[mAt] == 'E')) {
					size_t scan = mAt + 1;
					if (scan < mSource.size() && (mSource[scan] == '+' || mSource[scan] == '-')) ++scan;
					if (scan < mSource.size() && std::isdigit(static_cast<unsigned char>(mSource[scan]))) {
						mAt = scan;
						while (!atEnd() && std::isdigit(static_cast<unsigned char>(mSource[mAt]))) ++mAt;
					}
				}
				emit(Op::PushNumber, numberConstant(std::strtod(mSource.substr(start, mAt - start).c_str(), nullptr)));
			}

			std::string readEscaped(char terminator) {
				std::string out;
				while (!atEnd() && mSource[mAt] != terminator) {
					char ch = mSource[mAt++];
					if (ch == '\\' && !atEnd()) {
						const char escape = mSource[mAt++];
						switch (escape) {
						case 'n': out.push_back('\n'); break;
						case 't': out.push_back('\t'); break;
						case 'r': out.push_back('\r'); break;
						default:  out.push_back(escape); break;
						}
						continue;
					}
					out.push_back(ch);
				}
				if (atEnd()) fail("unterminated string");
				++mAt; // the closing quote
				return out;
			}

			void parseString(char quote) {
				++mAt;
				emit(Op::PushText, textConstant(readEscaped(quote)));
			}

			// A template becomes a chain of concatenations, left to right. Every piece is
			// converted to text so `${count}` in a string is the number spelled out rather
			// than arithmetic with the surrounding literal.
			void parseTemplate() {
				++mAt; // the opening backtick
				std::string literal;
				bool anything = false;

				auto flushLiteral = [&]() {
					if (literal.empty()) return;
					emit(Op::PushText, textConstant(literal));
					if (anything) emit(Op::Concat);
					anything = true;
					literal.clear();
				};

				while (!atEnd() && mSource[mAt] != '`') {
					if (mSource[mAt] == '\\' && mAt + 1 < mSource.size()) {
						const char escape = mSource[mAt + 1];
						mAt += 2;
						switch (escape) {
						case 'n': literal.push_back('\n'); break;
						case 't': literal.push_back('\t'); break;
						default:  literal.push_back(escape); break;
						}
						continue;
					}
					if (mSource[mAt] == '$' && mAt + 1 < mSource.size() && mSource[mAt + 1] == '{') {
						flushLiteral();
						mAt += 2;
						parseExpression();
						emit(Op::ToText);
						if (anything) emit(Op::Concat);
						anything = true;
						skipSpace();
						if (!take("}")) fail("expected '}' to close the substitution");
						continue;
					}
					literal.push_back(mSource[mAt++]);
				}
				if (atEnd()) fail("unterminated template literal");
				++mAt; // the closing backtick
				flushLiteral();
				if (!anything) emit(Op::PushText, textConstant(""));
			}

			const std::string& mSource;
			size_t             mAt = 0;
			Program            mProgram;
			// A handler is called with the widget's new value; a value binding is not.
			const bool         mIsHandler = false;
			// The name the handler gave that value, empty when it named none. Reading it
			// compiles to PushEvent.
			std::string        mParameter;
			// The row template's parameter, when this is one of its bindings.
			const std::string  mRowParam;
		};
	}

	ParseResult parseBinding(const std::string& source, bool isHandler,
	                         const std::string& rowParam) {
		ParseResult result;
		try {
			Parser parser(source, isHandler, rowParam);
			result.program = parser.parse();
			result.ok = true;
		} catch (const Failure& failure) {
			result.error = failure.message;
			result.position = failure.position;
		}
		return result;
	}

	std::string pointAt(const std::string& source, size_t position) {
		std::ostringstream out;
		out << source << "\n";
		for (size_t i = 0; i < position && i < source.size() + 1; ++i) out << ' ';
		out << "^";
		return out.str();
	}
}
