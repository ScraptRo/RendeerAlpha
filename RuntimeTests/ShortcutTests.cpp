#include "TestHarness.h"

#include <GraphicalObjects/Shortcut.h>

#include <string>

// Key chords: what a layout writes, what the keyboard sends, and who hears it first.
//
// The parser is the half a person types into, so it is tested for the spellings people
// actually use and for saying *why* when it refuses one -- a shortcut that silently did
// not parse is a menu entry whose keys do nothing, with no clue as to which part was wrong.
namespace {

	using namespace RDA;

	Chord parsed(const char* text) {
		Chord chord;
		parseChord(text, chord);
		return chord;
	}

	Chord make(int key, uint8_t mods = 0) {
		Chord chord;
		chord.key = key;
		chord.mods = mods;
		return chord;
	}
}

TEST(a_chord_is_read_in_any_order_and_any_case) {
	const Chord want = make(Key::S, Chord::Ctrl | Chord::Shift);
	CHECK(parsed("Ctrl+Shift+S") == want);
	CHECK(parsed("shift+ctrl+s") == want);
	CHECK(parsed("  Ctrl + Shift + S ") == want);
	CHECK(parsed("Control+Shift+S") == want);
}

TEST(mod_is_ctrl) {
	CHECK(parsed("Mod+K") == parsed("Ctrl+K"));
}

TEST(named_and_function_keys_are_keys) {
	CHECK(parsed("F5") == make(Key::F1 + 4));
	CHECK(parsed("F24") == make(Key::F24));
	CHECK(parsed("Delete") == make(Key::Delete));
	CHECK(parsed("Del") == make(Key::Delete));
	CHECK(parsed("Alt+PageDown") == make(Key::PageDown, Chord::Alt));
	CHECK(parsed("Escape") == make(Key::Escape));
	CHECK(parsed("Ctrl+9") == make(Key::Digit9, Chord::Ctrl));
}

TEST(punctuation_is_written_as_itself_and_plus_by_name) {
	CHECK(parsed("Ctrl+/") == make(Key::Slash, Chord::Ctrl));
	CHECK(parsed("Ctrl+[") == make(Key::LeftBracket, Chord::Ctrl));
	CHECK(parsed("Ctrl+\\") == make(Key::Backslash, Chord::Ctrl));
	// The = key, which is where + is on the keyboard every zoom-in was bound to.
	CHECK(parsed("Ctrl+Plus") == make(Key::Equal, Chord::Ctrl));
	CHECK(parsed("Ctrl+-") == make(Key::Minus, Chord::Ctrl));
}

TEST(a_chord_that_does_not_parse_says_which_part) {
	Chord chord;
	std::string error;
	CHECK(!parseChord("Ctrl+Shfit+P", chord, &error));
	CHECK(error.find("Shfit") != std::string::npos);
	CHECK(!chord.valid());

	CHECK(!parseChord("Ctrl+Blah", chord, &error));
	CHECK(error.find("Blah") != std::string::npos);

	CHECK(!parseChord("Ctrl++", chord, &error));
	CHECK(error.find("Plus") != std::string::npos);

	CHECK(!parseChord("Ctrl+Ctrl+S", chord, &error));
	CHECK(!parseChord("", chord, &error));
	CHECK(!parseChord("F25", chord, &error));
	CHECK(!parseChord("Ctrl", chord, &error));
}

TEST(tab_belongs_to_the_focus_walk) {
	// A key like any other to press -- a capture has to be able to move the focus -- and
	// not one a shortcut may take.
	CHECK(parsed("Tab") == make(Key::Tab));
	CHECK(shortcutRefusal(parsed("Tab")) != nullptr);
	CHECK(shortcutRefusal(parsed("Shift+Tab")) != nullptr);
	CHECK(std::string(shortcutRefusal(parsed("Tab"))).find("Ctrl+Tab") != std::string::npos);
	CHECK(shortcutRefusal(parsed("Ctrl+Tab")) == nullptr);
	CHECK(shortcutRefusal(parsed("Ctrl+S")) == nullptr);
}

TEST(a_chord_is_written_back_the_way_it_reads) {
	CHECK_STR(chordName(parsed("shift+ctrl+p")), "Ctrl+Shift+P");
	CHECK_STR(chordName(parsed("Mod+Alt+Super+F12")), "Ctrl+Alt+Super+F12");
	CHECK_STR(chordName(parsed("Ctrl+Plus")), "Ctrl+=");
	CHECK_STR(chordName(parsed("pagedown")), "PageDown");
	CHECK_STR(chordName(parsed("Ctrl+'")), "Ctrl+'");
	// And what it writes, it reads.
	const char* chords[] = { "Ctrl+S", "Alt+Shift+Left", "F1", "Ctrl+\\", "Super+Space", "Ctrl+`" };
	for (const char* text : chords) {
		CHECK(parsed(chordName(parsed(text)).c_str()) == parsed(text));
	}
}

TEST(a_field_keeps_what_it_types_and_edits_with) {
	CHECK(textFieldKeeps(make(Key::A)));                       // a letter is a letter
	CHECK(textFieldKeeps(make(Key::A, Chord::Shift)));         // and so is a capital
	CHECK(textFieldKeeps(make(Key::Delete)));
	CHECK(textFieldKeeps(make(Key::Escape)));                   // how you leave the field
	CHECK(textFieldKeeps(make(Key::C, Chord::Ctrl)));           // copy
	CHECK(textFieldKeeps(make(Key::Z, Chord::Ctrl | Chord::Shift))); // redo
	CHECK(textFieldKeeps(make(Key::Left, Chord::Ctrl | Chord::Shift))); // select a word
	CHECK(textFieldKeeps(make(Key::Enter, Chord::Ctrl)));       // send

	CHECK(!textFieldKeeps(make(Key::S, Chord::Ctrl)));          // save is the application's
	CHECK(!textFieldKeeps(make(Key::F1 + 4)));                  // F5 types nothing
	CHECK(!textFieldKeeps(make(Key::C, Chord::Ctrl | Chord::Alt)));
	CHECK(!textFieldKeeps(make(Key::A, Chord::Alt)));
}

TEST(a_focused_widget_keeps_only_the_keys_it_acts_on) {
	CHECK(focusedWidgetKeeps(make(Key::Space)));
	CHECK(focusedWidgetKeeps(make(Key::Enter)));
	CHECK(focusedWidgetKeeps(make(Key::Escape)));
	CHECK(focusedWidgetKeeps(make(Key::Down)));

	CHECK(!focusedWidgetKeeps(make(Key::Delete)));
	CHECK(!focusedWidgetKeeps(make(Key::A)));
	CHECK(!focusedWidgetKeeps(make(Key::Enter, Chord::Ctrl)));
}
