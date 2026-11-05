#include "InputScript.h"

#include <GraphicalObjects/Shortcut.h>
#include <RendeerAlpha.h>

#include "Common.h"

#include <cstdio>

namespace Cli {

	namespace {

		// A coordinate as the script used to carry it: written out with six decimals and read
		// back. A point a hair short of a whole number -- 66.99999 for 67, which is what a
		// drag's steps come to in floating point -- has always reached the engine as 67, and
		// a rectangle's edge is exactly where that decides whether the pointer is inside it.
		// So it is read back the same way rather than passed on as it came.
		float written(float value) {
			char text[64];
			std::snprintf(text, sizeof(text), "%f", value);
			float back = 0.0f;
			std::sscanf(text, "%f", &back);
			return back;
		}

		PointerStep pointerAt(float x, float y, bool down) {
			return PointerStep{ written(x), written(y), down };
		}

		// What a step does when it is sent. The pointer is the shot's, and moves with the
		// steps that move it.
		struct Sender {
			Pointer& pointer;
			bool     holdDrag;

			void operator()(const ChordStep& step) const {
				rendeerSyntheticChord(step.chord.c_str());
			}
			void operator()(const TextStep& step) const {
				rendeerSyntheticText(step.text.c_str());
			}
			void operator()(const PointerStep& step) const {
				// Kept in the pointer the frames after it, so the pointer stays where the
				// drag left it.
				pointer.placed = true;
				pointer.x = step.x;
				pointer.y = step.y;
				pointer.pressed = step.down;
				rendeerSetSyntheticPointer(pointer.x, pointer.y, pointer.pressed);
			}
			void operator()(const ReleaseStep&) const {
				pointer.pressed = holdDrag;
				rendeerSetSyntheticPointer(pointer.x, pointer.y, pointer.pressed);
			}
			void operator()(const DropStep& step) const {
				rendeerSyntheticDrop(step.paths);
			}
			void operator()(const ModifiersStep& step) const {
				rendeerSetSyntheticModifiers(step.ctrl, step.shift);
			}
			void operator()(const CompositionStep& step) const {
				rendeerSyntheticComposition(step.text.c_str(), step.caret);
			}
		};
	}

	std::string InputScript::addChord(const std::string& chord) {
		// Checked now rather than when it is sent, so a typo is a refusal before a window
		// opens instead of a picture of nothing having happened.
		RDA::Chord parsed;
		std::string error;
		if (!RDA::parseChord(chord, parsed, &error)) {
			return "--key '" + chord + "' " + error;
		}
		mSteps.push_back(ChordStep{ chord });
		return {};
	}

	// Text rather than a chord: what a field or a dropdown reads as typing. In order with
	// --key, so "--key Tab --type abc" is the keyboard reaching something and then typing
	// into it.
	void InputScript::addText(const std::string& text) {
		mSteps.push_back(TextStep{ text });
	}

	// A drag from one point to another: down at the first, carried across in steps -- a
	// pick-up waits for the pointer to have moved, and a target is found by where the
	// pointer is on each frame -- and let go at the second, unless --hold says to take the
	// picture while it is still being carried. In order with --key and --type, so a drag
	// can be followed by Escape.
	std::string InputScript::addDrag(const std::string& spec) {
		float at[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		size_t from = 0;
		bool ok = true;
		for (int k = 0; k < 4 && ok; ++k) {
			const size_t comma = spec.find(',', from);
			const std::string part = spec.substr(from, comma == std::string::npos
			                                           ? std::string::npos : comma - from);
			ok = number(part, at[k]) && (k == 3 || comma != std::string::npos);
			from = (comma == std::string::npos) ? spec.size() : comma + 1;
		}
		if (!ok) return "--drag wants X1,Y1,X2,Y2 in layout units, like 40,60,300,60";

		mSteps.push_back(pointerAt(at[0], at[1], false));
		mSteps.push_back(pointerAt(at[0], at[1], true));
		constexpr int kSteps = 10;
		for (int k = 1; k <= kSteps; ++k) {
			const float t = static_cast<float>(k) / kSteps;
			mSteps.push_back(pointerAt(at[0] + (at[2] - at[0]) * t, at[1] + (at[3] - at[1]) * t, true));
		}
		mSteps.push_back(ReleaseStep{});   // let go, unless --hold
		return {};
	}

	// A click with Ctrl or Shift held through it, which a chord cannot do: a chord holds its
	// modifiers for one frame, and a click is a press and a release. In order with --key
	// and --drag, so a selection can be built a click at a time.
	std::string InputScript::addClick(const std::string& text) {
		std::string spec = text;
		bool ctrl = false, shift = false;
		for (;;) {
			if (spec.rfind("Ctrl+", 0) == 0)       { ctrl = true;  spec.erase(0, 5); }
			else if (spec.rfind("Shift+", 0) == 0) { shift = true; spec.erase(0, 6); }
			else break;
		}
		float x = 0.0f, y = 0.0f;
		if (!coordinates(spec, x, y)) {
			return "--click wants X,Y in layout units, Ctrl+ and Shift+ in front "
			       "if they are held, like Ctrl+120,340";
		}
		mSteps.push_back(ModifiersStep{ ctrl, shift });
		mSteps.push_back(pointerAt(x, y, true));
		mSteps.push_back(pointerAt(x, y, false));
		mSteps.push_back(ModifiersStep{ false, false });
		return {};
	}

	// An input method part way through a word, in order with --key and --type, so
	// "--key Tab --compose nihao" reaches a field and starts composing in it.
	void InputScript::addComposition(const std::string& text) {
		// The method's cursor at the end of what it is composing, in characters.
		int characters = 0;
		for (const char c : text)
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++characters;
		mSteps.push_back(CompositionStep{ text, characters });
	}

	// Files let go of where the pointer is -- put it there with --hover first. Separated by
	// ';', which is rarer in a path than a comma is.
	void InputScript::addFiles(const std::string& spec) {
		DropStep drop;
		size_t from = 0;
		while (from <= spec.size()) {
			const size_t semi = spec.find(';', from);
			const std::string one = spec.substr(from, semi == std::string::npos
			                                          ? std::string::npos : semi - from);
			if (!one.empty()) drop.paths.push_back(one);
			if (semi == std::string::npos) break;
			from = semi + 1;
		}
		mSteps.push_back(std::move(drop));
	}

	void InputScript::sendNext(Pointer& pointer) {
		std::visit(Sender{ pointer, mHoldDrag }, mSteps[mSent++]);
	}
}
