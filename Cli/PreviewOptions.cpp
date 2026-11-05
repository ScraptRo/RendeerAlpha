// What `prototype` and `shot` were asked to do: their command line, read into a
// PreviewOptions.
#include "Preview.h"

#include <cstdio>

namespace Cli {

	namespace {

		// ---- the command line ---------------------------------------------------------------
		//
		// One option, one function that reads it. Each returns what to say about a value it
		// cannot read, or nothing -- so what follows is a table of which flag does what,
		// and the loop that walks the line is the same for all of them.

		using Handler = std::string (*)(PreviewOptions& options, const std::string& flag,
		                                const std::string& value);

		struct Option {
			const char* flag;
			bool        takesValue;
			bool        shotOnly;
			Handler     apply;
		};

		// The plain ones: a value stored, a value collected, a switch turned.
		template <std::string PreviewOptions::* Field>
		std::string storeText(PreviewOptions& options, const std::string&, const std::string& value) {
			options.*Field = value;
			return {};
		}

		template <std::vector<std::string> PreviewOptions::* Field>
		std::string collectText(PreviewOptions& options, const std::string&, const std::string& value) {
			(options.*Field).push_back(value);
			return {};
		}

		std::string takeNoBuild(PreviewOptions& options, const std::string&, const std::string&) {
			options.compile = false;
			return {};
		}

		std::string takeStill(PreviewOptions& options, const std::string&, const std::string&) {
			options.still = true;
			options.stillSet = true;
			return {};
		}

		std::string takeMoving(PreviewOptions& options, const std::string&, const std::string&) {
			options.still = false;
			options.stillSet = true;
			return {};
		}

		// The ones with something to check.

		std::string takeSize(PreviewOptions& options, const std::string&, const std::string& size) {
			const size_t by = size.find_first_of("xX*");
			float w = 0.0f, h = 0.0f;
			if (by == std::string::npos || !number(size.substr(0, by), w)
			    || !number(size.substr(by + 1), h) || w < 1.0f || h < 1.0f) {
				return "--size wants WxH, like 1280x800 (got " + size + ")";
			}
			options.width = static_cast<unsigned>(w);
			options.height = static_cast<unsigned>(h);
			return {};
		}

		std::string takeScale(PreviewOptions& options, const std::string&, const std::string& value) {
			if (!number(value, options.scale) || options.scale <= 0.0f) {
				return "--scale wants a number, like 1 or 1.5";
			}
			return {};
		}

		std::string takeFrames(PreviewOptions& options, const std::string&, const std::string& value) {
			float count = 0.0f;
			if (!number(value, count) || count < 1.0f || count > 240.0f) {
				return "--frames wants 1 to 240";
			}
			options.frames = static_cast<int>(count);
			return {};
		}

		std::string takeEvery(PreviewOptions& options, const std::string&, const std::string& value) {
			float ms = 0.0f;
			if (!number(value, ms) || ms < 0.0f) {
				return "--every wants milliseconds";
			}
			options.everyMs = static_cast<int>(ms);
			return {};
		}

		std::string takeSettle(PreviewOptions& options, const std::string&, const std::string& value) {
			float count = 0.0f;
			if (!number(value, count) || count < 0.0f) {
				return "--settle wants milliseconds";
			}
			options.settleMs = static_cast<int>(count);
			return {};
		}

		std::string takeScroll(PreviewOptions& options, const std::string&, const std::string& value) {
			if (!number(value, options.scrolling)) {
				return "--scroll wants notches, negative for downward";
			}
			return {};
		}

		std::string takePointer(PreviewOptions& options, const std::string& flag, const std::string& value) {
			float x = 0.0f, y = 0.0f;
			// A widget's id in place of a point: the pointer goes to its centre once the layout
			// is built. Pressing is remembered with it, because that is not known until then.
			if (namesAWidget(value)) {
				options.pointerTarget = value;
				options.pointerTargetPressed = (flag == "--press");
				return {};
			}
			if (!coordinates(value, x, y)) {
				return flag + " wants X,Y in layout units, like 120,340, or a widget's id";
			}
			options.pointer.placed = true;
			options.pointer.x = x;
			options.pointer.y = y;
			options.pointer.pressed = (flag == "--press");
			return {};
		}

		// What is done to the interface first goes to the script, in the order it is written.

		std::string takeKey(PreviewOptions& options, const std::string&, const std::string& value) {
			return options.script.addChord(value);
		}

		std::string takeType(PreviewOptions& options, const std::string&, const std::string& value) {
			options.script.addText(value);
			return {};
		}

		std::string takeDrag(PreviewOptions& options, const std::string&, const std::string& value) {
			return options.script.addDrag(value);
		}

		std::string takeHold(PreviewOptions& options, const std::string&, const std::string&) {
			options.script.holdDrag();
			return {};
		}

		std::string takeClick(PreviewOptions& options, const std::string&, const std::string& value) {
			return options.script.addClick(value);
		}

		std::string takeCompose(PreviewOptions& options, const std::string&, const std::string& value) {
			options.script.addComposition(value);
			return {};
		}

		std::string takeFiles(PreviewOptions& options, const std::string&, const std::string& value) {
			options.script.addFiles(value);
			return {};
		}

		const Option kOptions[] = {
			// What a prototype and a shot share.
			{ "--set",      true,  false, collectText<&PreviewOptions::sets> },
			{ "--rows",     true,  false, collectText<&PreviewOptions::rows> },
			{ "--data",     true,  false, storeText<&PreviewOptions::dataFile> },
			{ "--layout",   true,  false, storeText<&PreviewOptions::opening> },
			{ "--no-build", false, false, takeNoBuild },

			// What the picture is, and how long it waits for the interface to arrive.
			{ "--out",      true,  true,  storeText<&PreviewOptions::out> },
			{ "--size",     true,  true,  takeSize },
			{ "--scale",    true,  true,  takeScale },
			{ "--frames",   true,  true,  takeFrames },
			{ "--every",    true,  true,  takeEvery },
			{ "--settle",   true,  true,  takeSettle },
			{ "--still",    false, true,  takeStill },
			{ "--moving",   false, true,  takeMoving },
			{ "--a11y",     true,  true,  storeText<&PreviewOptions::a11yOut> },

			// Where the pointer is, and the wheel.
			{ "--hover",    true,  true,  takePointer },
			{ "--press",    true,  true,  takePointer },
			{ "--scroll",   true,  true,  takeScroll },

			// What is done to the interface first, in the order it is written.
			{ "--key",      true,  true,  takeKey },
			{ "--type",     true,  true,  takeType },
			{ "--drag",     true,  true,  takeDrag },
			{ "--hold",     false, true,  takeHold },
			{ "--click",    true,  true,  takeClick },
			{ "--compose",  true,  true,  takeCompose },
			{ "--files",    true,  true,  takeFiles },
		};

		// A flag with no value after it is not that flag, which is how `--out` at the end of
		// a line comes to be reported as an option nobody knows.
		const Option* findOption(const std::string& arg, bool shooting, bool haveValue) {
			for (const Option& one : kOptions) {
				if (arg != one.flag) continue;
				if (one.shotOnly && !shooting) continue;
				if (one.takesValue && !haveValue) continue;
				return &one;
			}
			return nullptr;
		}
	}

	bool parsePreviewOptions(const Args& args, bool shooting, PreviewOptions& options) {
		for (size_t i = 1; i < args.size(); ++i) {
			const std::string& arg = args[i];
			if (const Option* option = findOption(arg, shooting, i + 1 < args.size())) {
				const std::string value = option->takesValue ? args[++i] : std::string();
				const std::string refusal = option->apply(options, arg, value);
				if (!refusal.empty()) {
					std::printf("rda: %s\n", refusal.c_str());
					return false;
				}
			}
			else if (!arg.empty() && arg[0] == '-') {
				std::printf("rda: unknown option %s\n", arg.c_str());
				return false;
			}
			else options.project = arg;
		}
		if (!options.stillSet) options.still = (options.frames == 1);
		return true;
	}
}
