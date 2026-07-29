#pragma once
#include <Core/Datatypes.h>
#include <GraphicalObjects/Window.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Texture.h>
#include <GraphicalObjects/Material.h>
#include <functional>

namespace RDA {

	// GUI setup: which font to bake the atlas from, and at what pixel height.
	struct GuiConfig {
		std::string fontPath = "res/fonts/CascadiaMono.ttf";
		float       fontHeight = 18.0f;

		// Optional XML theme loaded at startup into the main window's Gui::theme().
		// Empty = the built-in look only. Variants can also be added later from code
		// (gui().theme().defineButton(...) / loadFromFile(...)).
		std::string themePath;

		// Optional XML syntax-highlighting languages, loaded into Gui::syntax(). The
		// "python" language is always available; this file adds or overrides others.
		std::string languagesPath;
	};

	// Where the engine's event + render loop runs.
	enum class ThreadMode {
		// The loop runs on the thread that calls rendeerRun(); the call blocks until
		// every window is closed (or rendeerStop() is called). GLFW is initialized on
		// the calling thread.
		Caller,

		// The engine spawns its own thread and runs the loop there; rendeerRun()
		// returns immediately. GLFW is initialized on that spawned thread. Use
		// rendeerStop()/rendeerWait() to end and join it.
		Owned,
	};

	// Everything the engine needs to run. The application only ever touches engine
	// state through these callbacks: they all run on the loop thread (see ThreadMode),
	// which is why creating meshes, materials and scene items from inside them needs
	// no synchronization of its own.
	struct AppConfig {
		AppInfo      app;                                // name, version, windowDependent
		ThreadMode   threadMode = ThreadMode::Caller;
		ViewportMode viewportMode = ViewportMode::Fullscreen; // scene to surface, or to a Viewport widget
		RedrawMode   redrawMode = RedrawMode::Continuous;     // every frame, or only on change

		// ViewportMode::Widget only. With an animating scene the engine must produce a
		// frame every tick, and that re-rasterises the whole GUI — every panel, glyph and
		// border — even though only the viewport's contents changed. With this on, the
		// GUI is drawn once into a cached layer and re-drawn only when it actually
		// changes; each frame then composites that layer over the live scene (two quads).
		//
		// Off by default: it is a different rendering path, and worth eyeballing on your
		// setup before relying on it.
		bool         cacheGuiLayer = false;

		// Cap the render loop to the display's refresh rate (FIFO present). On by
		// default: leaving it off makes the GPU render frames it never shows, which on
		// a laptop just wastes power and heat. Set false for uncapped rendering
		// (Mailbox present) when benchmarking or measuring raw frame times.
		bool vsync = true;

		std::function<void()>              onStart;      // once, after the window + renderer are up
		std::function<void(float dtSeconds)> onUpdate;   // once per frame, before that frame is drawn
		std::function<void()>              onShutdown;   // once, after the loop ends, before teardown

		// Optional discrete-input callbacks, dispatched on the main window during the
		// event poll. Continuous input (movement, held keys) is better read from
		// getMainWindow()->input() inside onUpdate.
		InputCallbacks input;

		GuiConfig gui;
	};
}

// Start the engine. In Caller mode this blocks until the loop ends; in Owned mode
// it returns immediately and the loop runs on the engine's own thread.
void rendeerRun(const RDA::AppConfig& config);

// Ask the loop to exit after the current frame. Safe to call from any callback, or
// from another thread when the engine is running in Owned mode.
void rendeerStop();

// Block until an Owned-mode loop has fully stopped and been torn down. No-op in
// Caller mode. Call this before the process exits when using Owned mode.
void rendeerWait();

// Ask for one more frame to be rendered. Only meaningful in RedrawMode::OnDemand,
// where the engine otherwise skips rendering when nothing it can see has changed —
// call this while the scene is animating, or after changing anything the GUI's own
// geometry does not reflect. Safe to call from any callback or thread; it also wakes
// an idling event pump. A no-op in Continuous mode.
void rendeerRequestRedraw();

RDA::Window* getMainWindow();

namespace RDA {
	// The scene the renderer draws each frame (also RDA::getScene(), from Scene.h).

	// ---- Resources ----
	// Call these from the AppConfig callbacks (they run on the loop thread). Handles
	// come from engine-owned pools and are released in teardown, so a handle that
	// outlives the loop is harmless; reference counting frees a resource early once
	// nothing holds it. MeshKind is stored on the mesh for a future render path to
	// dispatch on; the current forward path draws every mesh the same way.
	obj_ref<Mesh>    createMesh(MeshKind kind = MeshKind::Opaque);
	obj_ref<Texture> createTexture();
	obj_ref<Texture> loadTexture(const std::string& path, bool srgb = true);

	// A material bound to the engine's forward pipeline. Add meshes to the scene with
	// it; the camera and per-object transform are supplied by the renderer.
	Material createForwardMaterial();
}
