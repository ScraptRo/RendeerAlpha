#pragma once

// The text of each prelude below is a .js file in js/, embedded at configure time by
// rda_embed_js (RendeerAlpha/CMakeLists.txt) -- which is why a target that includes this
// has the generated directory on its include path.
#include <BuildTimerPreludeSource.h>
#include <FormatPreludeSource.h>
#include <TimerPreludeSource.h>

// JavaScript the engine supplies to code it did not write: `format`, and the four timer
// functions. Internal to the layout tools and the script host, like ModuleTransform.h.
//
// Each is compiled into a run-time module only when the module's code names it, the way
// the colour helpers are, so a layout that formats nothing carries none of it. And the
// same `format` is defined while a layout or state.ts is built, so a top-level
// `format.bytes(2048)` gives at build what it would give later -- the code is one text.
//
// JavaScript rather than C++ for that reason, and because what it does is string work
// QuickJS already does well. QuickJS has no Intl, which is why any of this exists: an
// interface wants grouped digits, sizes, dates and "3 minutes ago", and toLocaleString
// gives none of them.
namespace RDA::Layout {

	// js/format.js.
	inline const char* formatPrelude() {
		return reinterpret_cast<const char*>(kFormatPreludeSource);
	}

	// The timers, as a run-time module gets them: each asks the engine, naming the module
	// it belongs to, so a module's timers go with it when it is unloaded -- a reloaded
	// layout does not keep running the old one's. js/timers.js.
	inline const char* timerPrelude() {
		return reinterpret_cast<const char*>(kTimerPreludeSource);
	}

	// The same names while a layout or state.ts is built, where nothing runs later: the
	// top level runs once, and a timer started there would have nowhere to go off.
	// js/build-timers.js.
	inline const char* buildTimerPrelude() {
		return reinterpret_cast<const char*>(kBuildTimerPreludeSource);
	}
}
