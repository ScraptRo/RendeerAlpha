#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalObjects/Window.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Texture.h>
#include <GraphicalObjects/Material.h>
#include <functional>
#include <memory>
#include <vector>

namespace RDA {

	// GUI setup: which font to bake the atlas from, and at which pixel heights.
	struct GuiConfig {
		// Whether this application has a GUI at all. Turning it off is not just "draw
		// nothing": the engine skips baking the font atlas, skips building the GUI
		// pipelines, and drops the GUI pass from every frame, so an application pays
		// nothing for an interface it never shows. ViewportMode::Widget needs a Viewport widget to
		// display the scene, so with the GUI off the scene always goes straight to the
		// window surface.
		bool enabled = true;

		std::string fontPath = "res/fonts/CascadiaMono.ttf";
		float       fontHeight = 18.0f;

		// The other sizes a theme may ask for, baked alongside fontHeight at startup.
		//
		// They are baked rather than scaled, so a heading is as crisp as a caption, and
		// they all share one atlas texture, so text of every size still batches into a
		// single draw. A theme that names a size not listed here is drawn at the
		// nearest one that is -- and says so once, in the log, naming both.
		//
		// The default is a type scale rather than a range: sizes far enough apart to
		// read as different, which is what a scale is for.
		std::vector<float> fontSizes = { 12.0f, 15.0f, 22.0f, 28.0f, 36.0f };

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

		// Keep the engine's own window off the screen.
		//
		// The engine needs a window to pick a device against and to build its pipelines
		// from — a Vulkan surface is what decides the format everything else is compatible
		// with. A program that has no use for that particular window can ask for it to
		// exist without ever being shown; it is never drawn or presented either, so it
		// costs a swapchain and nothing more.
		//
		// What that leaves is a process whose only visible windows are ones it opened
		// itself with rendeerCreateWindow, and a headless run that still has a device.
		bool         hiddenMainWindow = false;
		ThreadMode   threadMode = ThreadMode::Caller;
		ViewportMode viewportMode = ViewportMode::Fullscreen; // scene to surface, or to a Viewport widget
		RedrawMode   redrawMode = RedrawMode::OnDemand;     // every frame, or only on change

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

// Whether the loop is still going. In Caller mode this is only ever asked from inside a
// callback, where the answer is always true; it exists for Owned mode, where the thread
// that started the engine has no other way to find out that every window has closed.
bool rendeerRunning();

// Ask for one more frame to be rendered. Only meaningful in RedrawMode::OnDemand,
// where the engine otherwise skips rendering when nothing it can see has changed —
// call this while the scene is animating, or after changing anything the GUI's own
// geometry does not reflect. Safe to call from any callback or thread; it also wakes
// an idling event pump. A no-op in Continuous mode.
//
// This form wakes *every* window, because it means "something changed that the engine
// cannot see" and the engine cannot know which window was meant.
void rendeerRequestRedraw();

// The same, for one window. Prefer it whenever the caller knows which window changed:
// each window earns its frames separately, so waking all of them makes an application
// that has been idle for minutes redraw because its neighbour moved. On a runtime
// serving several applications that is the difference between one window's worth of
// work per frame and everyone's.
void rendeerRequestRedraw(RDA::Window* window);

// Something rebuilt the retained interface: a layout hot-reloaded, a screen swapped.
//
// The retained cache decides whether to walk the widget tree by looking at input -- the
// pointer moving, a key, a focused caret. Replacing the tree is none of those, so
// without this the next frame presents the geometry it already had and the new screen
// does not appear until something unrelated moves the mouse. Same failure the binding
// path had, and the same fix.
//
// Called for you by LayoutHost and Router. An application only needs it if it rebuilds
// a retained tree by hand.
void rendeerInterfaceChanged();

RDA::Window* getMainWindow();

// Opens another window, drawn by the same renderer as the first: one device, one set of
// pipelines, one font atlas, and only the per-window frame resources duplicated. This is
// what lets the runtime give each application its own window without each application
// bringing its own graphics stack.
//
// Only valid after the engine is up (from onStart or onUpdate), and only from the thread
// running the loop — the windowing system is not thread-safe. Returns nullptr on failure.
RDA::Window* rendeerCreateWindow(uint32_t width, uint32_t height, const char* title,
                                 bool vsync = true);

// Closes a window opened by rendeerCreateWindow and releases what the renderer held for
// it. Waits for the GPU first, since a frame may still be reading its resources. Passing
// the main window is ignored: it belongs to the engine's own lifetime.
void rendeerDestroyWindow(RDA::Window* window);

// Blocks until the GPU has finished everything submitted. Needed before destroying any
// resource a frame in flight might still be reading — a texture a client has just
// stopped using, for instance. Expensive by nature: it stalls the pipeline, so it
// belongs in teardown paths, not in a frame.
void rendeerWaitIdle();

// Releases what the renderer cached for a texture before it is destroyed — its GUI
// descriptor set. Pair it with rendeerWaitIdle(), since a frame may still be sampling
// the texture. Harmless for a texture that was never drawn with.
void rendeerForgetTexture(const RDA::Texture* texture);

// Hands a texture to the engine to destroy at the start of a frame, once the GPU is
// idle and the GUI has forgotten it.
//
// A widget that owns a texture cannot destroy it when it is destroyed. The frame's draw
// list is built before the application's update runs, so a widget torn down there -- a
// screen swapped, a layout reloaded -- leaves this frame's commands pointing at it, and
// they are recorded after the update returns. Destroying it there is a use-after-free
// that a validation layer reports as an invalid image view and a release build does not
// report at all.
//
// So the texture outlives the frame that drew it: ownership moves here, the address
// stays valid until the next frame begins, and the destruction happens where it is safe.
// Taking a unique_ptr is what keeps that address stable -- a texture moved into a queue
// would move out from under the very pointers this exists to protect.
void rendeerRetireTexture(std::unique_ptr<RDA::Texture> texture);

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
	// A sampled texture from tightly packed RGBA8 pixels — for generated data (noise,
	// gradients, lookup tables) that never lives in a file. `srgb` false for anything
	// read as data rather than colour: normal maps, roughness, masks.
	obj_ref<Texture> createTextureFromPixels(const void* rgba, uint32_t width, uint32_t height,
	                                         bool srgb = true);

	// ---- Primitives ----
	// Ready-to-draw procedural meshes, centred on the origin and carrying normals and
	// UVs for the forward path. Same rules as the factories above: call them from the
	// AppConfig callbacks, and the engine owns the result.
	obj_ref<Mesh> createCubeMesh(float size = 1.0f);
	// A ground plane in XZ facing +Y. `subdivisions` splits each side into more quads,
	// which matters once lighting is per-vertex or the surface is displaced. `uvTiles`
	// repeats the texture that many times across the plane, so a material keeps its
	// real-world scale on a large surface instead of being stretched over it.
	obj_ref<Mesh> createPlaneMesh(float size = 1.0f, uint32_t subdivisions = 0, float uvTiles = 1.0f);
	// A UV sphere; `segments` goes around, `rings` from pole to pole.
	obj_ref<Mesh> createSphereMesh(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);

	// A material bound to the engine's forward pipeline. Add meshes to the scene with
	// it; the camera and per-object transform are supplied by the renderer.
	//
	// The textures are baked into a descriptor set when the material is created, so
	// build materials during setup (onStart) rather than mid-frame — changing the maps
	// means creating another material. The scalar MaterialParams stay freely mutable.
	Material createForwardMaterial();
	Material createForwardMaterial(const MaterialTextures& textures);

	// Hands a material's descriptor set back so the next material reuses it. Without this,
	// every material ever created keeps its share of a descriptor pool until shutdown —
	// which matters for anything that rebuilds materials, a scene reload or a model swap.
	//
	// Pair it with rendeerWaitIdle(): a frame in flight may still be bound to the set.
	// A Material is a value and its copies share one set, so release once, when the last
	// of them is finished with. Harmless on a material that was never given a set.
	void releaseForwardMaterial(Material& material);
}
