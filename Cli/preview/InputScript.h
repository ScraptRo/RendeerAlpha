// What `rda shot` does to the interface before it takes the picture: key chords, typed
// text, a drag, a click, files let go of, an input method part way through a word.
//
// A script is a list of steps, in the order the options were written, and the shot sends
// one a frame. A step is a value of its own type rather than a string with a control byte
// in front of it, so the place a step is made from an option and the place it is sent are
// no longer two halves of a text format that have to agree about its punctuation.
#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace Cli {

	// A key chord that is known to parse: "Ctrl+S", "F5". Delivered once, to every window.
	struct ChordStep { std::string chord; };

	// Text as the keyboard typed it, UTF-8: into a focused field, or a dropdown finding a
	// choice by its first letters.
	struct TextStep { std::string text; };

	// The pointer somewhere, with the button down or up. Kept there on the frames after,
	// so it stays where the last step left it.
	struct PointerStep { float x = 0.0f; float y = 0.0f; bool down = false; };

	// The pointer at the centre of a widget somebody named, with the button down or up. Which
	// widget, and so where, is not known until the layout is built, so it is worked out when the
	// step is sent rather than when the option is read.
	struct TargetStep { std::string target; bool down = false; };

	// The end of a drag: the button is let go, unless --hold said to take the picture while
	// it is still being carried.
	struct ReleaseStep {};

	// Files let go of where the pointer is.
	struct DropStep { std::vector<std::string> paths; };

	// Ctrl and Shift held by nobody, until the next one of these says otherwise.
	struct ModifiersStep { bool ctrl = false; bool shift = false; };

	// An input method part way through a word, with its own cursor inside it, in characters.
	struct CompositionStep { std::string text; int caret = 0; };

	using InputStep = std::variant<ChordStep, TextStep, PointerStep, TargetStep, ReleaseStep, DropStep,
	                               ModifiersStep, CompositionStep>;

	// Where a named widget is, once the layout is built. NotYet is a layout that has not been
	// walked (wait a frame); No is a refusal, with `problem` saying what to do about it.
	enum class Found { Yes, NotYet, No };
	using Resolver = std::function<Found(const std::string& target, float& x, float& y,
	                                     std::string& problem)>;

	// A widget named on the command line, not a point: anything that is not X,Y. A comma is the
	// mark of a point, so a typo in one is said to be one and not looked for as a widget.
	inline bool namesAWidget(const std::string& text) {
		return !text.empty() && text.find(',') == std::string::npos;
	}

	// Where the synthetic pointer stands. The shot holds it there frame after frame, and the
	// pointer steps move it, so it is kept by whoever drives rather than by the script.
	struct Pointer {
		bool  placed = false;    // --hover, --press, or a step has put it somewhere
		float x = 0.0f;
		float y = 0.0f;
		bool  pressed = false;
	};

	class InputScript {
	public:
		// Each of these is one option on the command line. The ones that can be mistyped
		// return what to say about it -- empty when the option was fine -- so a typo is a
		// refusal before a window opens instead of a picture of nothing having happened.
		std::string addChord(const std::string& chord);
		void        addText(const std::string& text);
		std::string addDrag(const std::string& spec);
		std::string addClick(const std::string& spec);
		void        addComposition(const std::string& text);
		void        addFiles(const std::string& spec);

		// --hold: picture a drag before it is let go.
		void holdDrag() { mHoldDrag = true; }

		// Whether every step has been sent.
		bool finished() const { return mSent >= mSteps.size(); }

		// The widgets the steps aim at, by the name they were written with.
		std::vector<std::string> targets() const;

		// Sends the next step to the engine, and moves `pointer` if it was a pointer step.
		// Found::Yes when it went; NotYet leaves the step for the next frame, and No leaves
		// `problem` saying why it never will.
		Found sendNext(Pointer& pointer, const Resolver& resolve, std::string& problem);

	private:
		std::vector<InputStep> mSteps;
		size_t                 mSent = 0;
		bool                   mHoldDrag = false;
	};
}
