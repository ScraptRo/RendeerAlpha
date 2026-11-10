#pragma once
#include <RendeerAlpha.h>
#include <GraphicalSrc/Renderer.h>
#include <atomic>
#include <string>
#include <vector>

// What the files of the running engine share, and nothing outside src/Runtime includes it.
//
// This was one file, RendeerAlpha.cpp, of fifteen hundred lines and some seventeen jobs.
// It is split by what each part is for, along the line the rest of the runtime is drawn
// on -- what is settled once, and what changes every frame:
//
//   Startup.cpp        static: bring-up and teardown, a window's interface set up, the
//                      windows an application opens, the handles a viewport is given
//   EventPump.cpp      the OS's events, and frames run from inside its modal loop
//   InputGather.cpp    one window's input, turned into what its GUI is told this frame
//   SyntheticInput.cpp the input nobody gave: what a capture or a test says happened
//   WindowSignals.cpp  state.rda.*, the window as signals that read and write
//   FrameLoop.cpp      the frame, the loop around it, and the public calls that steer it
namespace RDA::Runtime {

	// ---- Startup.cpp ---------------------------------------------------------------------

	// The renderer is built against a surface, so it cannot exist before some window does.
	// An application that opens no window at startup -- a runtime serving other processes,
	// say -- brings it up with its first window instead, which is why the settings it needs
	// are kept here rather than read from a config that has gone.
	struct DeferredRendererSetup {
		std::string  fontPath;
		float        fontHeight = 18.0f;
		std::vector<float> fontSizes;
		ViewportMode viewportMode = ViewportMode::Fullscreen;
		bool         guiEnabled = true;
		// Physical pixels per layout unit, settled once when the first window opens.
		float        unitScale = 1.0f;
		// Nothing goes on the screen this run, so a window opened later is hidden as well.
		bool         offscreen = false;
		// What every window's interface wears: the syntax languages, then the theme.
		std::string  languagesPath;
		std::string  themePath;
	};
	extern DeferredRendererSetup gRendererSetup;
	extern Window*  mainWindow;
	extern Renderer gRenderer;
	// Whether the application's life is tied to having a window. An ordinary app ends when
	// its last window closes; a service that opens windows on behalf of other processes has
	// none most of the time and must not take that for "finished".
	extern bool gWindowDependent;

	// What one layout unit is worth in pixels: what was settled at bring-up, or 1 before
	// anything was. Asked wherever a pixel meets a unit, which is a handful of places, each
	// of which used to spell out the guard against "not settled yet" itself.
	float unitScale();

	void engineBringUp(const AppConfig& config);
	void engineTearDown();

	// ---- EventPump.cpp -------------------------------------------------------------------

	// Pumps OS events into every window. False once no window is open and the application
	// lives only while one is.
	bool pumpEvents(double waitSeconds = 0.0);

	// Keeps the engine running while `window` is dragged or resized.
	void watchForModalLoop(Window* window);

	// ---- InputGather.cpp -----------------------------------------------------------------

	// Gathers one window's input and opens its GUI frame.
	void beginWindowGui(Window& window, GuiInput& gi, float dtSeconds);

	// What was kept about a window's pointer -- its click run, its synthetic press -- let go
	// with the window.
	void forgetWindowInput(const Window* window);

	// ---- SyntheticInput.cpp --------------------------------------------------------------

	// A pointer put somewhere by something other than a hand. See
	// rendeerSetSyntheticPointer: hover and press are read from where the pointer is, so
	// capturing a hovered widget means being able to say where it is.
	extern bool      gSyntheticPointer;
	extern float     gSyntheticScroll;
	extern glm::vec2 gSyntheticAt;
	extern bool      gSyntheticDown;
	extern bool      gSyntheticCtrl;
	extern bool      gSyntheticShift;
	// Chords nobody typed, delivered on the next frame and then forgotten. See
	// rendeerSyntheticChord: a shortcut is matched against what went down, so testing one
	// without a hand on the keyboard means saying what went down.
	extern std::vector<RDA::Chord> gSyntheticChords;
	// Characters nobody typed, the same way: delivered once, as this frame's text.
	extern std::string gSyntheticText;
	// Files nobody dragged in, let go of wherever the pointer is.
	extern std::vector<std::string> gSyntheticFiles;
	// A composition nobody is typing, held until it is cleared -- what a field looks like
	// while an input method is part way through a word.
	extern std::string gSyntheticPreedit;
	extern int         gSyntheticPreeditCaret;

	// ---- WindowSignals.cpp ---------------------------------------------------------------

	void publishWindowSignals();
	void forgetWindowSignals();

	// ---- FrameLoop.cpp -------------------------------------------------------------------

	// Loop control. gRunning is cleared by rendeerStop().
	extern std::atomic<bool> gRunning;
	// True for exactly as long as a frame is being built, whichever of the two callers
	// started it. Read by the modal pump, which must not start a second one on top.
	extern bool gInFrame;
	// The configuration the running loop was started with, so a GLFW callback can reach it.
	// Null whenever no loop is running.
	extern const AppConfig* gLoopConfig;

	float advanceClock();
	bool  runFrame(const AppConfig& config, float dtSeconds);
}
