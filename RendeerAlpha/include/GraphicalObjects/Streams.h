#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

// Pictures that keep arriving.
//
// A still goes through Images.h: registered once, shown as `<image src="mem:name">`. That
// is the wrong shape for something live. `Images::define` decodes and builds a **new**
// texture every call, so a camera at thirty frames a second would create and retire
// thirty textures a second and leave the old ones to the frames still in flight.
//
// A stream is the same picture surface kept and written into instead. Its size is settled
// by the first frame; every frame after that of the same size is an upload into the
// texture already there, and only a size change builds a new one. That is the whole
// difference, and it is the difference between a feed that runs and one that thrashes.
//
// It also knows whether anyone is looking. A `<stream>` that is on screen marks itself
// each frame it paints, so a producer can ask `wanted()` and stop decoding for a tab
// nobody has open -- which is the half of "pull" that actually saves anything.
namespace RDA {

	class Texture;

	class Streams {
	public:
		~Streams();

		// A frame of raw pixels: four bytes each (R, G, B, A), rows tightly packed,
		// `width * height * 4` bytes in all.
		//
		// Must run on the loop thread -- it touches the device. Everything reaching this
		// from the C ABI hops; C++ callers are on it by construction.
		bool push(const std::string& name, const void* rgba, uint32_t width, uint32_t height);

		// The same, from an encoded frame -- PNG, JPEG, and the rest of what stb_image
		// reads. An MJPEG camera hands over exactly this.
		bool pushEncoded(const std::string& name, const void* bytes, size_t size);

		// A surface for something on the GPU to write into, sized to match, made if it is
		// not there. For an effect's output -- see Effects.h.
		//
		// Different from a pushed frame in two ways it cannot hide. It is UNORM rather
		// than sRGB, because Vulkan has no storage image in an sRGB format, and it holds
		// **linear** values: the GUI's image path samples it and writes to an sRGB
		// surface, which encodes. That is why an effect reads and writes linear light,
		// and it is the correct space to do image arithmetic in anyway.
		//
		// Null, with a line in the log, if it cannot be made.
		Texture* target(const std::string& name, uint32_t width, uint32_t height);

		// Said after something wrote into a target: it counts as a frame, and the cache
		// has to hear about it the same way a push is heard.
		void noteWritten(const std::string& name);

		// The current frame, copied back to ordinary memory as RGBA8, rows tightly packed.
		//
		// **As it looks on screen.** A pushed frame is stored the way it was pushed and
		// comes back that way; a surface an effect wrote is stored in linear light, and is
		// encoded on the way out so that what comes back is the picture rather than the
		// arithmetic behind it. The alternative -- handing back linear bytes and a footnote
		// -- makes saving a filtered picture as a PNG produce something too dark, which is
		// a bug report rather than a lesson.
		//
		// Costs a round trip to the GPU and a wait: a staging buffer, a copy, a queue
		// idle. Fine for saving a picture or handing one to a model, wrong for doing every
		// frame -- which is what a <stream> is for.
		//
		// Must run on the loop thread. False, with a line in the log, if there is nothing
		// to read.
		bool read(const std::string& name, std::vector<unsigned char>& out,
		          uint32_t& width, uint32_t& height) const;

		// Drops it. A <stream> still naming it draws nothing and says so once.
		void close(const std::string& name);

		// The texture to draw, or null before the first frame. Borrowed: the registry
		// owns it, and a size change replaces it, so nothing should keep this across
		// frames without checking revision().
		const Texture* find(const std::string& name) const;

		// Whether a <stream> painted this in the last few frames -- that is, whether
		// anybody is looking. False for a stream on a screen that is not showing, which
		// is when a producer should stop working rather than keep decoding into a
		// texture nothing samples.
		//
		// A few frames rather than exactly the last one: a window that redraws on demand
		// may not have drawn at all since the question was last asked, and a feed that
		// stopped because the interface was idle would never start again.
		//
		// True for a name nothing has drawn *yet*, for the same window of frames. A
		// producer that asked before the interface had painted once would otherwise be
		// told nobody is looking and stop before it began -- which is the deadlock of a
		// widget waiting for a frame and a frame waiting for a widget. After that window
		// it goes false and says so once, naming the stream, because by then the answer
		// really is that nothing shows it.
		//
		// Safe from any thread, and does not cross to the loop: a producer asks this on
		// every turn of its own loop, and a round trip to decide whether to do work would
		// cost more than the work.
		bool wanted(const std::string& name) const;

		// Said by the <stream> widget as it paints. Not public API in spirit, but the
		// widget lives outside this class and there is nothing to gain by hiding it.
		void markSeen(const std::string& name);

		// Frames pushed, and frames that were still current when a widget drew them. The
		// gap is what a producer is wasting: pushing sixty a second into a window drawing
		// thirty means half of them were replaced before anything sampled them.
		void counts(const std::string& name, uint64_t& pushed, uint64_t& shown) const;

		// Bumped by every frame, every size change and every close. Read without the lock
		// on purpose: the retained cache asks it every frame and a stale read costs one
		// frame of latency, never correctness.
		//
		// The retained GUI cache decides whether to walk the tree by looking at input, and
		// a frame arriving is not input. Without this a feed would advance only when the
		// pointer happened to move.
		uint64_t revision() const { return mRevision.load(); }

		// Counted up once per frame by the loop, so `wanted` has something to age
		// against. Called from one place; see beginWindowGui.
		void tick() { ++mFrameNumber; }

		// Every surface released, while there is still a device to release them with.
		void clear();

	private:
		struct Live {
			std::unique_ptr<Texture> texture;
			uint32_t width = 0;
			uint32_t height = 0;
			uint64_t pushed = 0;
			uint64_t shown = 0;
			uint64_t lastSeenFrame = 0;
			bool     fresh = false;   // pushed since a widget last drew it
			// Made as a storage image for something else to write into, rather than from
			// pixels. A name used both ways rebuilds the surface, which is wasteful and
			// correct; nothing stops it because nothing should be doing it.
			bool     writable = false;
			// When this name was first heard of, so a stream nothing ever draws stops
			// being wanted instead of staying wanted forever.
			uint64_t knownFrame = 0;
			bool     warnedUnwatched = false;
		};

		Live& slot(const std::string& name);
		bool  upload(Live& live, const std::string& name, const void* rgba,
		             uint32_t width, uint32_t height);

		// Everything here is reachable from two threads: the loop pushes, draws and
		// clears, while a producer asks `wanted` and `counts` from wherever it lives. One
		// lock over the whole map rather than anything cleverer -- the map is tiny, the
		// calls are a handful per frame, and a reader-writer race on an unordered_map is
		// the kind of bug that corrupts rather than the kind that returns a stale answer.
		mutable std::mutex mLock;
		std::unordered_map<std::string, Live> mByName;
		std::atomic<uint64_t> mRevision{ 1 };
		std::atomic<uint64_t> mFrameNumber{ 0 };
	};

	Streams& streams();
}
