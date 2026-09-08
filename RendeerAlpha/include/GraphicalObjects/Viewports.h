#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <chrono>
#include <Core/Datatypes.h>
#include <GraphicalObjects/GuiTypes.h>

typedef struct VmaAllocator_T* VmaAllocator;

namespace RDA {

	// A <viewport> is a hole in the interface that something else fills.
	//
	// The interface decides where it is and how big; what appears inside it is not the
	// engine's business. That is the same bargain a browser's <canvas> makes, and it is
	// made twice here, because the two halves of this engine are not equally able to
	// hold a GPU:
	//
	//   C++  gets the command buffer. Inside the render pass, at the viewport's own
	//        resolution, with the device, the allocator and the queue to build against.
	//        Whatever Vulkan can do, a viewport can do -- there is no wrapper in the way
	//        and no subset to discover.
	//
	//   Everyone else gets a list of 2D commands the engine draws. Rectangles, lines and
	//        text, in the viewport's coordinates, sent in one call and redrawn until
	//        replaced. It is small on purpose: a plot, a timeline, a seating chart, a
	//        game of life -- the drawings a backend actually wants, without asking Python
	//        to own GPU memory or to be on the render thread at the right microsecond.
	//
	// Both address a viewport by the `name` its element carries, so which half is driving
	// is a property of the application rather than of the layout.

	// ---- the 2D half -------------------------------------------------------------------

	enum class DrawOp : int32_t {
		Clear = 0, // fills the whole viewport; `color` only
		Rect  = 1, // a, b, c, d = x, y, w, h; e = corner radius
		Line  = 2, // a, b -> c, d; e = width
		Text  = 3, // a, b = top-left; e = font size (0 = the interface's own)
	};

	// One drawing instruction, in viewport coordinates: 0,0 is its top-left corner and
	// the units are the same pixels the rest of the interface is laid out in.
	struct DrawCommand {
		DrawOp      op = DrawOp::Rect;
		uint32_t    color = 0xFFFFFFFFu;
		float       a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
		float       e = 0.0f;
		std::string text;
	};

	// ---- the Vulkan half ---------------------------------------------------------------

	// Handed to a C++ draw callback. The render pass has already begun on the viewport's
	// own target and the viewport and scissor cover it, so the shortest useful callback
	// is a bind and a draw.
	struct ViewportFrame {
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		// The pass the recording goes into. Pipelines must be built against this (or one
		// compatible with it); it outlives every resize, so building once is right.
		VkRenderPass    renderPass = VK_NULL_HANDLE;
		VkExtent2D      extent{};      // the target's size in pixels, not the widget's
		uint32_t        frameIndex = 0; // 0 .. framesInFlight-1, for per-frame resources
		float           seconds = 0.0f; // since the engine started
		float           delta = 0.0f;   // since the previous frame
	};

	// Everything needed to make Vulkan objects of your own. Valid from onStart until
	// onShutdown returns, and the GPU is idle by the time onShutdown runs -- so whatever
	// was built here is destroyed there, with nothing still using it.
	struct ViewportGpu {
		VkInstance       instance = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice         device = VK_NULL_HANDLE;
		VkQueue          graphicsQueue = VK_NULL_HANDLE;
		uint32_t         graphicsFamily = 0;
		VmaAllocator     allocator = nullptr;
		// The pass a viewport's recording goes into, and what pipelines must be built
		// against. Available from onStart onwards, and the same one for the life of the
		// application, so building a pipeline there rather than on the first frame is
		// both possible and better. Null unless ViewportMode::Widget is set.
		VkRenderPass     renderPass = VK_NULL_HANDLE;
		// How many frames the engine keeps in flight. A resource written by the CPU each
		// frame needs this many copies; one written once needs one.
		uint32_t         framesInFlight = 1;
		VkFormat         colorFormat = VK_FORMAT_UNDEFINED; // the viewport target's format
		VkFormat         depthFormat = VK_FORMAT_UNDEFINED;
	};

	// The named viewports of this application, and what fills them.
	//
	// Every method is for the loop thread: the C++ half is called from onStart or a
	// callback, and the C ABI marshals its calls there before touching this. That is
	// what keeps a draw list from being replaced halfway through being read.
	class Viewports {
	public:
		// ---- the Vulkan half ----
		// Record into `name` every frame until stopDrawing. Recording is not optional
		// work: it happens inside a frame that is already being built, so it must return.
		void onDraw(const std::string& name, std::function<void(const ViewportFrame&)> draw);
		void stopDrawing(const std::string& name);
		bool drawnByCode(const std::string& name) const;

		// What the engine clears a Vulkan viewport to before the callback runs.
		void setClearColor(const std::string& name, uint32_t rgba);
		uint32_t clearColor(const std::string& name) const;

		// ---- the 2D half ----
		// Replaces everything `name` was drawing. An empty list leaves it blank, which is
		// how a backend takes its drawing back down.
		void setCommands(const std::string& name, std::vector<DrawCommand> commands);
		const std::vector<DrawCommand>* commands(const std::string& name) const;

		// ---- what both halves ask ----
		// The size the named viewport was last laid out at, in interface pixels. Zero
		// until a frame has placed it -- a backend that draws before the first frame is
		// drawing into something whose size nobody has decided yet.
		glm::vec2 size(const std::string& name) const;

		// Reported by the widget as it paints, which is the only place the answer is
		// known. Also settles which viewport holds the GPU, since there is one offscreen
		// target to give.
		void reportPlacement(const std::string& name, const Rect& rect);

		// The viewport the Vulkan target belongs to this frame: the first one painted
		// that is not drawing 2D commands. Empty when the frame drew no viewport, or
		// when every viewport it drew is a 2D one.
		const std::string& gpuViewport() const { return mGpuViewport; }

		// A frame is starting. The answer from last frame is kept until this frame paints
		// a viewport of its own: the GUI reuses its geometry when nothing it can see has
		// changed, so most frames never walk the tree and never report a placement --
		// and on those frames the interface is identical, so last frame's answer is the
		// right one rather than a stale one.
		void beginFrame();

		// Called by the renderer from inside the scene pass. Does nothing unless this
		// frame's GPU viewport has a callback, which is the usual case.
		void record(VkCommandBuffer cmd, VkRenderPass pass, VkExtent2D extent,
		            uint32_t frameIndex);

		// Bumped whenever a drawing changes. The GUI keeps geometry from the last frame
		// when nothing it can see has changed, and a backend replacing a drawing is a
		// change it cannot see: the widget tree is identical and so is the input.
		uint64_t revision() const { return mRevision; }

		// Whether anything at all is registered. Lets the renderer skip the whole
		// question in an application that never mentions a viewport.
		bool empty() const { return mSlots.empty(); }

		// Everything this held, dropped. Called when the engine tears down: a callback
		// registered by the last run closes over Vulkan objects that no longer exist, and
		// the application's next onStart is where it builds new ones and registers again.
		void forget();

	private:
		struct Slot {
			Rect                                     rect{};
			std::function<void(const ViewportFrame&)> draw;
			std::vector<DrawCommand>                 commands;
			uint32_t                                 clearColor = 0xFF080505u; // the engine's slate
			bool                                     hasCommands = false;
		};
		Slot* find(const std::string& name);
		const Slot* find(const std::string& name) const;

		// A handful at most, and looked up by name once per frame each, so a vector of
		// pairs beats a hash map and keeps the order they were first seen in.
		std::vector<std::pair<std::string, Slot>> mSlots;
		std::string mGpuViewport;
		// The clock a recording animates on. Members rather than statics inside record(),
		// so a second run in the same process starts from zero rather than from wherever
		// the first one left off.
		std::chrono::steady_clock::time_point mStarted{};
		std::chrono::steady_clock::time_point mPrevious{};
		bool        mClockStarted = false;
		uint64_t    mRevision = 0;
		bool        mPlacedThisFrame = false;
		bool        mWarnedAboutSharing = false;
	};

	Viewports& viewports();

	// The handles a C++ application needs to build its own Vulkan work. Valid only while
	// the engine is up.
	ViewportGpu viewportGpu();
}
