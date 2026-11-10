// `rda prototype` and `rda shot`: the interface, run with nothing behind it. They are run by
// `rda-preview` (PreviewMain.cpp); `rda` hands them over.
//
// They are one command with two endings: everything up to the window is identical --
// compile the project, define the placeholder state, open the route or the layout -- and
// then one waits for a person while the other takes a picture and stops.
#pragma once

#include "Common.h"
#include "InputScript.h"

namespace Cli {

	// What the command line asked of either of them.
	struct PreviewOptions {
		fs::path    project = ".";
		std::string opening;                 // --layout
		std::string dataFile;                // --data
		std::vector<std::string> sets;       // --set
		std::vector<std::string> rows;       // --rows
		bool        compile = true;          // --no-build turns it off

		// Shot only.
		std::string out = "shot.png";
		unsigned    width = 0, height = 0;
		float       scale = 0.0f;
		int         frames = 1, settleMs = 250, everyMs = 100;
		// A still shot waits for the interface to arrive: transitions ease over a theme's
		// transitionMs, and a window nobody presents runs the loop as fast as it can, so
		// without this a capture catches a hover a millisecond into a 160ms ease and the
		// picture shows a colour one step from where it started. A sequence is about motion
		// by definition, so it does not wait.
		bool        still = true;
		bool        stillSet = false;
		Pointer     pointer;                 // --hover, --press
		// --hover or --press given a widget's id instead of a point; resolved once the layout
		// is built, and then it is `pointer` like any other.
		std::string pointerTarget;
		bool        pointerTargetPressed = false;
		float       scrolling = 0.0f;        // --scroll
		// What to do before the picture, in order: --key, --type, --drag, --click,
		// --compose, --files.
		InputScript script;
		std::string a11yOut;                 // --a11y: where to write the accessibility tree
		// --edit: draws the editor, as `prototype` does, and lets a script drive it. Off for a
		// shot otherwise, because a picture of an interface should be of the interface.
		bool        edit = false;
	};

	// False, with the reason printed, for an option it does not know or cannot read.
	// `shooting` is what lets `shot` take the options `prototype` has no use for.
	bool parsePreviewOptions(const Args& args, bool shooting, PreviewOptions& options);

	int cmdPrototype(const Args& args);
	int cmdShot(const Args& args);
}
