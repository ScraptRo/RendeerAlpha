#include "Engine.h"
#include <Layout/Bindings.h>
#include <Layout/LayoutLoader.h>
#include <Layout/ScriptHost.h>
#include <Layout/StyleWatch.h>
#include <Core/LoopWork.h>
#include <Core/Wake.h>
#include <GraphicalObjects/Viewports.h>
#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Effects.h>
#include <GraphicalObjects/Texture.h>
#include <Logger/Logger.h>
#include <chrono>
#include <cmath>
// std::exception, for the one catch in engineMain. Named rather than inherited: MSVC
// supplies it through other headers here and GCC has no reason to.
#include <exception>
#include <thread>

// The frame, the loop that runs it, and the public calls that start, stop and steer it.
namespace RDA::Runtime {

	std::atomic<bool> gRunning{ false };
	// Only exists in Owned mode, where rendeerWait() joins it.
	// A pointer that is never deleted rather than a std::thread with static storage.
	// If something calls exit() from the loop thread itself -- Xlib's default error
	// handler does exactly that, after printing one line -- static destruction runs on
	// that thread, and destroying a joinable std::thread is std::terminate: the one
	// readable line is followed by "terminate called without an active exception" and
	// a core dump. Leaked, the process ends the way exit() meant it to.
	std::thread*      gLoopThread = nullptr;

	// Whether a frame was asked for, and whether the idle wait should end, live in
	// Core/Wake: anything may ask, from any thread, without reaching into this file.
	// Loop iterations in which anything was drawn, and in which nothing was.
	static uint64_t gFramesRendered = 0, gFramesSkipped = 0; // loop-thread only
	// Individual window draws. With per-window gating these are the numbers that show the
	// saving: on a runtime serving several applications, one busy window no longer drags
	// the idle ones through a frame, and only this pair can tell you so.
	static uint64_t gWindowFramesDrawn = 0, gWindowFramesSkipped = 0;

	// Textures whose owner is gone wait in Texture.h's retire queue (retireTexture);
	// when to free them is the loop's call. Drained at the top of a frame: by now the
	// frame that referenced them has been recorded and submitted, and waiting makes sure
	// it has also finished.
	static void drainRetiredTextures() {
		std::vector<std::unique_ptr<Texture>> retired = takeRetiredTextures();
		if (retired.empty()) return;
		gRenderer.waitIdle();
		for (auto& texture : retired) {
			if (!texture) continue;
			gRenderer.forgetTexture(texture.get());
			texture->destroy();
		}
	}

	// The loop itself: bring up, run the user's callbacks + render each frame, tear
	// down. Runs on the caller's thread (Caller) or the spawned thread (Owned).
	// The loop's own clock, at file scope because the frame can also be driven from a GLFW
	// callback (see serviceWhileModal) and the two must not each keep their own idea of
	// when the last frame was.
	static std::chrono::steady_clock::time_point gLastFrame = std::chrono::steady_clock::now();

	float advanceClock() {
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - gLastFrame).count();
		gLastFrame = now;
		return dt;
	}

	bool gInFrame = false;

	// One iteration of the engine's work, with the event queue left alone.
	//
	// Split out of the loop because it has to run from two places: the loop calls it after
	// pumping events, and serviceWhileModal() calls it from inside an OS modal loop where
	// the pump cannot return. Returns whether anything was drawn.
	bool runFrame(const AppConfig& config, float dtSeconds) {
		// Set here rather than at the two call sites, because a guard somebody has to
		// remember at every call site is a guard that will be missing from the third one.
		struct InFrame {
			InFrame()  { gInFrame = true; }
			~InFrame() { gInFrame = false; }
		} marker;
		// Before anything is walked or drawn, so nothing this frame can still be pointing
		// at what is about to be destroyed.
		drainRetiredTextures();

		// Before the walk, so a binding that reads the window's size is evaluated
		// against this frame's rather than the last one's.
		publishWindowSignals();

		// A window dragged onto a monitor at a different scale. The interface keeps the one
		// it started with, because the font atlas is baked at it -- so the window is
		// correct and slightly the wrong size rather than correct and blurry, which is the
		// better of the two wrongs. Said once, because somebody looking at an interface
		// that is suddenly too small deserves to know why.
		if (mainWindow && config.gui.enabled) {
			static bool toldAboutScale = false;
			const float now = mainWindow->contentScale();
			const float baked = unitScale();
			if (!toldAboutScale && config.gui.uiScale <= 0.0f && std::fabs(now - baked) > 0.01f) {
				toldAboutScale = true;
				RDA_LOG_INFO("This display is at " << now << "x and the interface was built for "
				             << baked << "x, so it is drawn at the size it opened with. Set "
				             << "GuiConfig::uiScale to pick one, or reopen the window there.");
			}
		}

		// An edited theme or mesh, before anything is resolved against the old one. It
		// throttles itself, and in a build without hot reload it is not even a branch.
		//
		// Here rather than in the application's update for the reason the layout reload is
		// *not* here: swapping a tree tears down the walk a widget callback is in, and the
		// application is the only one that knows it is not in one. A style swap replaces no
		// widgets -- the look is resolved by variant as each one is drawn -- so between two
		// frames is simply the right place for it.
		if (Layout::styleWatch().reloadIfChanged()) rendeerRequestRedraw();

		// Reused across frames: its `typed` string and `editKeys` vector keep their
		// capacity, so a frame with keyboard activity does not allocate. Function-local
		// rather than a loop local because the frame is now called from two places.
		static GuiInput gi;

		// Run-time code's own clock: the timers that are due go off, and state.ts's computed
		// values that read a table are asked whether it moved -- a table has no observers.
		// Ahead of the bindings, which may read what either wrote, and outside the GUI,
		// because an application with no window still has its state.
		Layout::scriptHost().tick();

		// Every window feeds its own input into its own GUI.
		if (config.gui.enabled) {
			// Which viewport holds the GPU is settled again every frame, because the
			// interface may have changed between them -- a route that swapped one
			// viewport for another has to be able to hand the target over.
			viewports().beginFrame();

			// A C++ callback needs somewhere to record into, and in Fullscreen mode the
			// scene goes straight to the window instead. Said once: it is a one-line
			// config change, and the symptom otherwise is a viewport that stays empty.
			if (config.viewportMode != ViewportMode::Widget && !viewports().empty()) {
				static bool warnedAboutMode = false;
				if (!warnedAboutMode && viewports().drawnByCode(viewports().gpuViewport())) {
					warnedAboutMode = true;
					RDA_LOG_WARNING("viewport: a draw callback is registered but viewportMode is "
					                "Fullscreen, so there is no target to record into. Set "
					                "config.viewportMode = ViewportMode::Widget.");
				}
			}

			// Widget viewport: size the offscreen scene target to the Viewport widget's
			// rect from last frame. Only the main window shows the scene, so only it asks.
			if (mainWindow && config.viewportMode == ViewportMode::Widget) {
				Rect vr = mainWindow->gui().viewportRect();
				if (vr.w >= 1.0f && vr.h >= 1.0f) {
					// Pixels, not units: this is a texture the scene is drawn into, and at
					// 150% a 600-unit viewport wants 900 pixels of it or the 3D view is the
					// one soft thing on the screen.
					const float k = unitScale();
					gRenderer.ensureSceneTarget({ static_cast<uint32_t>(vr.w * k),
					                              static_cast<uint32_t>(vr.h * k) });
				}
			}
			if (mainWindow) mainWindow->gui().setSceneTexture(gRenderer.sceneTexture());

			// Bindings whose signals moved since the last frame, applied before the
			// interface is walked so the walk sees the new values rather than last
			// frame's. Nothing is compared and no tree is searched: the signal graph
			// already knows exactly which bindings a change reached, and on almost every
			// frame that list is empty and this costs one check.
			//
			// The redraw request is what makes the change visible in an on-demand window,
			// which would otherwise have no reason to look again.
			if (Layout::bindings().applyDirty() > 0) {
				// The tree has to be walked again, not just presented again.
				//
				// The retained cache decides whether to walk by looking at input -- the
				// pointer moving, a button, a key, a focused caret. A binding writing a
				// widget property is none of those, so without this the frame renders the
				// geometry it already had and the change does not appear until something
				// unrelated happens to move the mouse. That is what "the interface takes
				// a second or two to catch up" was.
				for (auto& node : windowList) {
					Window* window = Window::getRefFromNode(node);
					if (window && window->windowIsUp()) window->gui().markDirty();
				}
				rendeerRequestRedraw();
			}

			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (!window || !window->windowIsUp()) continue;
				if (config.hiddenMainWindow && window == mainWindow) continue;
				beginWindowGui(*window, gi, dtSeconds);
			}
			gSyntheticChords.clear();
			gSyntheticText.clear();
			gSyntheticFiles.clear();
		}

		// What a stream's "is anybody looking" ages against. Counted here rather than in
		// the GUI, because a window with no GUI at all still turns the loop.
		streams().tick();

		if (config.onUpdate) config.onUpdate(dtSeconds);

		if (config.gui.enabled) {
			for (auto& node : windowList) {
				Window* window = Window::getRefFromNode(node);
				if (!window || !window->windowIsUp()) continue;
				if (config.hiddenMainWindow && window == mainWindow) continue;
				window->gui().end();
				window->accessibilityFrameDone();
				window->textInputFrameDone();
				// After the walk, because whether a title bar is being held is something
				// the walk works out -- and before the frame is drawn, so the window and
				// what is in it move together rather than a frame apart.
				window->followGrip(window->gui().windowGrabbed());
				window->frameDone();
				// A double click on the bar maximises or puts back. Through setMaximized,
				// as the OS button would, so `state.rda.maximized` hears of it the same way.
				if (window->gui().windowToggleAsked() && !window->isFullscreen()) {
					window->setMaximized(!window->isMaximized());
				}
			}
		}

		// Every window's frame is now whole: the walk is done and so is everything
		// Gui::end() adds after it. This is the only point where that is true and the
		// frame has not been drawn yet, which is what a capture wants.
		if (config.onFrameEnd) config.onFrameEnd();

		// Whether this frame is worth rendering is decided per window, not once for
		// all of them.
		//
		// In Continuous mode every window draws. In OnDemand a window earns a frame
		// only when its own interface changed, it needs a new swapchain, or the
		// application asked for one. Skipping means no submit and no present, so the
		// GPU does nothing and that window keeps showing what was presented last.
		//
		// Deciding once for the whole loop is what the runtime could least afford: its
		// windows belong to different processes, so ten connected applications meant
		// one of them animating dragged the other nine through a redraw they had no
		// reason to want. Now an idle application costs an idle window.
		//
		// A request through rendeerRequestRedraw() still applies to every window. It
		// means "something changed that the engine cannot see", and the engine has no
		// way to know which window the caller meant.
		const bool requested = (config.redrawMode == RedrawMode::OnDemand)
			? Wake::takeFrameRequest() // consumed either way, never carried over
			: false;

		bool renderedAny = false;
		for (auto& node : windowList) {
			Window* window = Window::getRefFromNode(node);
			if (!window || !window->windowIsUp()) continue;
			if (config.hiddenMainWindow && window == mainWindow) continue; // anchor only
			// Built, not shown. Presenting to a window nothing composites is work for
			// nobody, and a capture renders into a target of its own anyway.
			if (config.offscreen && window == mainWindow) continue;

			// Consumed every iteration whatever else is true, so a request for this
			// window is honoured exactly once and never lingers into a later frame.
			const bool windowRequested = window->takeRedrawRequest();

			bool draw = true;
			if (config.redrawMode == RedrawMode::OnDemand) {
				draw = requested || windowRequested || window->wasResized();
				// With no GUI there is nothing for the engine to detect a change in, so
				// an on-demand app drives its own frames with rendeerRequestRedraw().
				//
				// A window with something still moving earns frames until it has arrived,
				// and then goes quiet again. That is the whole cost of animation in an
				// on-demand loop: frames while something is happening, none while not.
				if (!draw && config.gui.enabled) {
					draw = window->gui().drawChanged() || window->gui().animating();
				}
			}
			if (!draw) { ++gWindowFramesSkipped; continue; }

			// One renderer serves them all: they share the device, the pipelines and
			// the atlas, and differ only in the per-window resources it keeps for each.
			gRenderer.drawWindow(*window);
			// A stop rather than a throw from the renderer: nothing around this loop catches,
			// and this may be running inside a GLFW callback. Stopping takes the ordinary way
			// out, so an application still gets its onShutdown.
			if (gRenderer.submitFailed()) {
				rendeerStop();
				break;
			}
			++gWindowFramesDrawn;
			renderedAny = true;
		}

		// The event pump idles only when nothing at all was drawn.
		if (renderedAny) ++gFramesRendered; else ++gFramesSkipped;
		return renderedAny;
	}

	const AppConfig* gLoopConfig = nullptr;

	static void engineMain(AppConfig config) {
		try {
			engineBringUp(config);
		} catch (const std::exception& failed) {
			// Bring-up is the one place a throw has nowhere to go. rendeerRun is the entry
			// point, so there is no caller inside the engine to catch it, and letting it
			// reach the C runtime is worse than it sounds: on Windows that puts up a modal
			// box and waits for somebody to press a button, so a tool that could not bake
			// a font atlas did not fail -- it hung, with no window and no message, until
			// it was killed. Measured with `rda shot --scale 3`, which asks for a font
			// atlas larger than the one the engine will build.
			//
			// So it is reported here and the loop never starts. rendeerRun returns, the
			// application finds out the ordinary way, and nothing is torn down that was
			// never built.
			RDA_LOG_ERROR("Start-up failed, so there is no loop to run: " << failed.what());
			gRunning.store(false);
			return;
		}
		publishWindowSignals();

		// Before onStart, not after: an application that spawns a thread there can ask for
		// a resource before the loop has run a single frame, and until this is set such a
		// request runs wherever it was made — which for anything touching the device is
		// exactly the thread it must not be on.
		loopWork().setServiceThread();

		// Set before onStart, because onStart is allowed to change its mind. A headless
		// check that finishes its work there, or a startup that finds a bad argument,
		// calls rendeerStop() and must actually stop -- raising the flag afterwards would
		// overwrite that decision and run the loop forever with nothing left to do.
		gRunning.store(true);

		if (config.onStart) config.onStart();


		// In OnDemand, an idle iteration blocks in the event pump for at most this long
		// rather than spinning. Short enough that time-based UI (the caret blink) still
		// ticks, long enough that an idle app costs almost no CPU.
		constexpr double kIdleWaitSeconds = 0.05;
		bool renderedLastFrame = true;

		// Reachable from the GLFW callbacks that drive a frame during a drag.
		gLoopConfig = &config;

		while (gRunning.load()) {
			// Only idle the pump once a frame has been skipped: while frames are being
			// produced the loop stays uncapped here and is paced by vsync at present.
			double wait = (config.redrawMode == RedrawMode::OnDemand && !renderedLastFrame)
				? kIdleWaitSeconds : 0.0;
			// No longer than the next timer, so one goes off when it is due rather than up to
			// an idle period late. A timer due now makes this a poll, not a wait.
			if (wait > 0.0) {
				const double timer = Layout::scriptHost().secondsToNextTimer();
				if (timer >= 0.0 && timer < wait) wait = timer;
			}
			if (!pumpEvents(wait)) break; // every window closed

			// Anything another thread has asked the loop to make, before the frame that
			// might use it. Costs one empty check on the overwhelming majority of frames.
			loopWork().service();

			renderedLastFrame = runFrame(config, advanceClock());
		}
		gRunning.store(false);
		gLoopConfig = nullptr; // no callback may run a frame after this
		// Releases anything still blocked on a resource rather than leaving it to time
		// out, so shutting down does not take five seconds per waiting caller.
		loopWork().stop();
		// One line at shutdown each, so both savings are observable without a profiler.
		if (config.redrawMode == RedrawMode::OnDemand) {
			RDA_LOG_INFO("OnDemand redraw - frames rendered: " << gFramesRendered
			             << ", skipped: " << gFramesSkipped
			             << " | window draws: " << gWindowFramesDrawn
			             << ", skipped: " << gWindowFramesSkipped);
		}
		if (mainWindow && config.gui.enabled) {
			const Gui::CacheStats& cache = mainWindow->gui().cacheStats();
			RDA_LOG_INFO("GUI retained cache - tree walked: " << cache.walked
			             << ", reused: " << cache.reused);
		}

		// The GPU is finished before the application is asked to clean up.
		//
		// An application that recorded into a <viewport> owns Vulkan objects -- a
		// pipeline, a buffer, a descriptor set -- and onShutdown is where it destroys
		// them. Destroying one a frame in flight is still using is a validation error and
		// a real hazard, and every such application would have to know to wait first. It
		// is one call here and a whole class of mistake nobody has to hear about.
		gRenderer.waitIdle();
		// Before onShutdown, so a screen that saves on close saves through a backend that is
		// still there. However a screen ended, its onClose runs.
		Layout::closeOpenLayouts();
		if (config.onShutdown) config.onShutdown();

		// The application just let go of its widget trees, so anything they owned is in
		// the queue with no frame left to drain it. Emptied here, while there is still a
		// device and an allocator to free it with -- left to static teardown, VMA asserts
		// that allocations outlived the block they came from, which is exactly true.
		drainRetiredTextures();

		forgetWindowSignals();
		viewports().forget();
		// Registered pictures, released while there is still a device to release them
		// with. Left to static teardown, VMA asserts -- correctly -- that an allocation
		// outlived the block it came from.
		images().clear();
		streams().clear();
		effects().clear();
		engineTearDown();
	}
}

void rendeerRun(const RDA::AppConfig& config) {
	if (config.threadMode == RDA::ThreadMode::Caller) {
		RDA::Runtime::engineMain(config);
	} else {
		delete RDA::Runtime::gLoopThread; // a previous run, already joined by rendeerWait()
		RDA::Runtime::gLoopThread = new std::thread(RDA::Runtime::engineMain, config);
	}
}

void rendeerStop() {
	RDA::Runtime::gRunning.store(false);
	// Wake an OnDemand loop that is blocked in the event pump, so it exits now rather
	// than after the idle timeout.
	RDA::Wake::wake();
}

bool rendeerCaptureFrame(std::vector<unsigned char>& rgba,
                         unsigned& width, unsigned& height) {
	rgba.clear();
	width = height = 0;
	if (!RDA::Runtime::mainWindow) {
		RDA_LOG_WARNING("capture: there is no window to capture");
		return false;
	}
	uint32_t w = 0, h = 0;
	if (!RDA::Runtime::gRenderer.captureGui(*RDA::Runtime::mainWindow, rgba, w, h)) return false;
	width = w;
	height = h;
	return true;
}

void rendeerRequestRedraw() {
	RDA::Wake::requestFrame();
}

void rendeerInterfaceChanged() {
	RDA::Wake::interfaceChanged();
}

void rendeerRequestRedraw(RDA::Window* window) {
	if (!window) { rendeerRequestRedraw(); return; } // null means "all of them"
	window->requestRedraw();
	RDA::Wake::wake();
}

void rendeerWait() {
	if (RDA::Runtime::gLoopThread && RDA::Runtime::gLoopThread->joinable()) {
		RDA::Runtime::gLoopThread->join();
	}
}

bool rendeerRunning() { return RDA::Runtime::gRunning.load(); }

void rendeerWaitIdle() {
	RDA::Runtime::gRenderer.waitIdle();
}

void rendeerForgetTexture(const RDA::Texture* texture) {
	if (texture) RDA::Runtime::gRenderer.forgetTexture(texture);
}

void rendeerRetireTexture(std::unique_ptr<RDA::Texture> texture) {
	RDA::retireTexture(std::move(texture));
}
