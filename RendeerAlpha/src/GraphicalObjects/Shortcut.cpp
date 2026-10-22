#include <GraphicalObjects/Shortcut.h>
#include <cctype>
#include <iterator>

namespace RDA {

	namespace {
		struct NamedKey { const char* name; int key; };

		// The keys with a name rather than a character. The first spelling of each is the
		// one chordName writes back; any later one is only accepted.
		constexpr NamedKey kNamed[] = {
			{ "Enter",     Key::Enter },     { "Return",   Key::Enter },
			{ "Escape",    Key::Escape },    { "Esc",      Key::Escape },
			{ "Space",     Key::Space },
			{ "Tab",       Key::Tab },
			{ "Backspace", Key::Backspace },
			{ "Delete",    Key::Delete },    { "Del",      Key::Delete },
			{ "Insert",    Key::Insert },    { "Ins",      Key::Insert },
			{ "Home",      Key::Home },
			{ "End",       Key::End },
			{ "PageUp",    Key::PageUp },
			{ "PageDown",  Key::PageDown },
			{ "Up",        Key::Up },
			{ "Down",      Key::Down },
			{ "Left",      Key::Left },
			{ "Right",     Key::Right },
			// '+' cannot be written as itself, because it is what joins the parts. On a US
			// keyboard it is Shift+= and "Ctrl+Plus" means the = key, which is what every
			// zoom-in shortcut is actually bound to.
			{ "Plus",      Key::Equal },
		};

		struct CharKey { char c; int key; };
		constexpr CharKey kPunctuation[] = {
			{ '-', Key::Minus },      { '=', Key::Equal },
			{ ',', Key::Comma },      { '.', Key::Period },
			{ '/', Key::Slash },      { ';', Key::Semicolon },
			{ '\'', Key::Apostrophe }, { '`', Key::Grave },
			{ '[', Key::LeftBracket }, { ']', Key::RightBracket },
			{ '\\', Key::Backslash },
		};

		bool sameWord(std::string_view a, std::string_view b) {
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a[i]))
				    != std::tolower(static_cast<unsigned char>(b[i]))) return false;
			}
			return true;
		}

		int keyFrom(std::string_view word) {
			if (word.size() == 1) {
				const unsigned char c = static_cast<unsigned char>(word[0]);
				if (std::isalpha(c)) return Key::A + (std::toupper(c) - 'A');
				if (std::isdigit(c)) return Key::Digit0 + (c - '0');
				for (const CharKey& p : kPunctuation) if (p.c == word[0]) return p.key;
				return 0;
			}
			// F1..F24. GLFW numbers them consecutively, which is the only reason this is
			// arithmetic rather than another twenty-four rows.
			if ((word[0] == 'F' || word[0] == 'f') && word.size() <= 3) {
				int n = 0;
				for (size_t i = 1; i < word.size(); ++i) {
					if (!std::isdigit(static_cast<unsigned char>(word[i]))) return 0;
					n = n * 10 + (word[i] - '0');
				}
				if (n >= 1 && n <= 24) return Key::F1 + (n - 1);
				return 0;
			}
			for (const NamedKey& k : kNamed) if (sameWord(word, k.name)) return k.key;
			return 0;
		}

		void fail(std::string* error, std::string message) {
			if (error) *error = std::move(message);
		}
	}

	bool parseChord(std::string_view text, Chord& out, std::string* error) {
		out = {};
		// Surrounding space is forgiven; inner space is not, because "Ctrl + S" is the same
		// chord and refusing it would be pedantry, while "Ctrl S" is a typo.
		auto trim = [](std::string_view s) {
			while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
			while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
			return s;
		};
		text = trim(text);
		if (text.empty()) { fail(error, "is empty"); return false; }

		Chord chord;
		size_t start = 0;
		while (true) {
			const size_t plus = text.find('+', start);
			const std::string_view part = trim(text.substr(start, plus == std::string_view::npos
			                                                      ? std::string_view::npos
			                                                      : plus - start));
			const bool last = (plus == std::string_view::npos);
			if (part.empty()) {
				fail(error, "has an empty part; write the + key as Plus");
				return false;
			}
			if (!last) {
				uint8_t bit = 0;
				if (sameWord(part, "Ctrl") || sameWord(part, "Control") || sameWord(part, "Mod"))
					bit = Chord::Ctrl;
				else if (sameWord(part, "Shift"))  bit = Chord::Shift;
				else if (sameWord(part, "Alt") || sameWord(part, "Option")) bit = Chord::Alt;
				else if (sameWord(part, "Super") || sameWord(part, "Win") || sameWord(part, "Cmd"))
					bit = Chord::Super;
				if (!bit) {
					fail(error, "has '" + std::string(part) + "' where a modifier goes; "
					            "the modifiers are Ctrl, Alt, Shift and Super, and Mod for Ctrl");
					return false;
				}
				if (chord.mods & bit) {
					fail(error, "names " + std::string(part) + " twice");
					return false;
				}
				chord.mods |= bit;
				start = plus + 1;
				continue;
			}
			chord.key = keyFrom(part);
			if (!chord.key) {
				fail(error, "ends in '" + std::string(part) + "', which is not a key this "
				            "knows; a letter, a digit, F1-F24, punctuation, or a named key "
				            "like Enter, Delete or PageDown");
				return false;
			}
			break;
		}

		out = chord;
		return true;
	}

	const char* shortcutRefusal(const Chord& chord) {
		// Tab and Shift+Tab are how the keyboard reaches every widget there is. A layout
		// that took them would strand anyone who cannot use a pointer.
		if (chord.key == Key::Tab && (chord.mods & ~Chord::Shift) == 0) {
			return "is Tab, which moves between widgets and cannot be a shortcut; Ctrl+Tab can";
		}
		return nullptr;
	}

	std::string chordName(const Chord& chord) {
		if (!chord.valid()) return {};
		std::string out;
		if (chord.mods & Chord::Ctrl)  out += "Ctrl+";
		if (chord.mods & Chord::Alt)   out += "Alt+";
		if (chord.mods & Chord::Shift) out += "Shift+";
		if (chord.mods & Chord::Super) out += "Super+";

		const int key = chord.key;
		if (key >= Key::A && key <= Key::Z) out += static_cast<char>('A' + (key - Key::A));
		else if (key >= Key::Digit0 && key <= Key::Digit9) out += static_cast<char>('0' + (key - Key::Digit0));
		else if (key >= Key::F1 && key <= Key::F24) out += "F" + std::to_string(key - Key::F1 + 1);
		else {
			// Punctuation first, so = reads as "=" rather than as the Plus it also is.
			for (const CharKey& p : kPunctuation) {
				if (p.key == key) { out += p.c; return out; }
			}
			for (const NamedKey& k : kNamed) {
				if (k.key == key) { out += k.name; return out; }
			}
			out += "?";
		}
		return out;
	}

	bool textFieldKeeps(const Chord& chord) {
		const bool function = chord.key >= Key::F1 && chord.key <= Key::F25;
		if ((chord.mods & (Chord::Ctrl | Chord::Alt | Chord::Super)) == 0) return !function;
		if ((chord.mods & ~Chord::Shift) != Chord::Ctrl) return false;
		switch (chord.key) {
		case Key::C: case Key::X: case Key::V: case Key::A:
		case Key::Z: case Key::Y:
		case Key::Left: case Key::Right: case Key::Up: case Key::Down:
		case Key::Home: case Key::End:
		case Key::Backspace: case Key::Delete: case Key::Enter:
			return true;
		default:
			return false;
		}
	}

	bool focusedWidgetKeeps(const Chord& chord) {
		if (chord.mods != 0) return false;
		switch (chord.key) {
		case Key::Space: case Key::Enter: case Key::Escape:
		case Key::Left: case Key::Right: case Key::Up: case Key::Down:
			return true;
		default:
			return false;
		}
	}
}
