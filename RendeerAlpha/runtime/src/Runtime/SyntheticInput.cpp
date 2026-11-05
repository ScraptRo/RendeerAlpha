#include "Engine.h"
#include <Logger/Logger.h>

// Input nobody gave: a pointer placed, a key pressed, text typed, files dropped and a word
// part way through an input method, all said by a caller -- a capture of a hovered button,
// a test of a shortcut. Read by InputGather.cpp as though it had come from a hand. What is
// said once (chords, text, files) is delivered on the next frame and forgotten; what is
// held (the pointer, the modifiers, a composition) stays until it is cleared.
namespace RDA::Runtime {

	bool      gSyntheticPointer = false;
	float     gSyntheticScroll = 0.0f;
	glm::vec2 gSyntheticAt{ 0.0f };
	bool      gSyntheticDown = false;
	bool      gSyntheticCtrl = false;
	bool      gSyntheticShift = false;
	std::vector<RDA::Chord> gSyntheticChords;
	std::string gSyntheticText;
	std::vector<std::string> gSyntheticFiles;
	std::string gSyntheticPreedit;
	int         gSyntheticPreeditCaret = 0;
}

void rendeerSetSyntheticPointer(float x, float y, bool down) {
	RDA::Runtime::gSyntheticPointer = true;
	RDA::Runtime::gSyntheticAt = { x, y };
	RDA::Runtime::gSyntheticDown = down;
	// The GUI keeps last frame's geometry when nothing it can see has changed, and a
	// pointer that moved is one of the things it looks at -- but only once it has been
	// through an input gather. Asking for a frame is what makes that happen.
	rendeerRequestRedraw();
}

bool rendeerSyntheticChord(const char* chord) {
	RDA::Chord parsed;
	std::string error;
	if (!chord || !RDA::parseChord(chord, parsed, &error)) {
		RDA_LOG_ERROR("synthetic chord '" << (chord ? chord : "") << "' " << error);
		return false;
	}
	RDA::Runtime::gSyntheticChords.push_back(parsed);
	rendeerRequestRedraw();
	return true;
}

void rendeerSyntheticText(const char* utf8) {
	if (!utf8 || !*utf8) return;
	RDA::Runtime::gSyntheticText += utf8;
	rendeerRequestRedraw();
}

void rendeerSyntheticDrop(const std::vector<std::string>& paths) {
	RDA::Runtime::gSyntheticFiles.insert(RDA::Runtime::gSyntheticFiles.end(), paths.begin(), paths.end());
	rendeerRequestRedraw();
}

void rendeerSyntheticComposition(const char* utf8, int caret) {
	RDA::Runtime::gSyntheticPreedit = utf8 ? utf8 : "";
	RDA::Runtime::gSyntheticPreeditCaret = caret;
	rendeerRequestRedraw();
}

void rendeerSetSyntheticModifiers(bool ctrl, bool shift) {
	RDA::Runtime::gSyntheticCtrl = ctrl;
	RDA::Runtime::gSyntheticShift = shift;
	rendeerRequestRedraw();
}

void rendeerClearSyntheticPointer() {
	RDA::Runtime::gSyntheticPointer = false;
	RDA::Runtime::gSyntheticDown = false;
	RDA::Runtime::gSyntheticCtrl = false;
	RDA::Runtime::gSyntheticShift = false;
	RDA::Runtime::gSyntheticScroll = 0.0f;
	rendeerRequestRedraw();
}

void rendeerSetSyntheticScroll(float notches) {
	RDA::Runtime::gSyntheticScroll = notches;
	rendeerRequestRedraw();
}
