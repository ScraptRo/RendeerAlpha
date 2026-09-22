#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

// Pictures a backend made, rather than files on disk.
//
// `<image src="res/logo.png">` is the ordinary case and needs none of this. This is the
// other one: a vision model hands back bytes, a plot is rendered into a buffer, a frame
// arrives from a camera -- and the only way to show any of it used to be to write a file
// and point at it, which means a temp directory, a name nobody wanted and a round trip
// through the disk for something already in memory.
//
// A picture registered here has a name, and a layout asks for it with "mem:<name>". The
// prefix is explicit rather than clever: a bare name that also happened to be a file on
// disk would be an ambiguity resolved silently, and this way what a layout means is
// visible in the layout.
//
// The registry owns the texture, not the widget. Two `<image>`s showing one name share it,
// and redefining the name replaces what both of them draw -- which is what makes it the
// right home for a preview that updates.
namespace RDA {

	class Texture;

	class Images {
	public:
		~Images();

		// An encoded picture -- PNG, JPEG, whatever stb_image reads. Returns false and
		// says why in the log if it will not decode.
		//
		// Must run on the loop thread: it creates a device texture. Everything reaching
		// this from the C ABI already hops, and C++ callers are on it by construction.
		bool define(const std::string& name, const void* bytes, size_t size);

		// Raw pixels, four bytes each, rows tightly packed, no header. For a buffer an
		// application already has in the right shape -- encoding it to PNG so the engine
		// could decode it again would be a strange way to spend a millisecond.
		bool definePixels(const std::string& name, const void* rgba,
		                  uint32_t width, uint32_t height);

		// Drops it. An `<image>` still pointing at the name draws nothing and says so
		// once, the same as a file that is not there.
		void forget(const std::string& name);

		// The texture, or null. Borrowed: the registry owns it, and a redefine replaces
		// it, so nothing should keep this across frames without checking revision().
		const Texture* find(const std::string& name) const;

		// Bumped by every define, redefine and forget.
		//
		// The retained GUI cache decides whether to walk the tree by looking at input, and
		// a picture being replaced is not input -- without this, a preview updated while
		// nobody touched the mouse would sit there showing the old one.
		uint64_t revision() const { return mRevision; }

		// Every picture released, while there is still a device to release them with.
		// Called at shutdown, before the device goes.
		void clear();

	private:
		std::unordered_map<std::string, std::unique_ptr<Texture>> mByName;
		uint64_t mRevision = 1;
	};

	Images& images();

	// The prefix a layout writes in front of a registered name. Exposed because both the
	// widget and the documentation would otherwise spell it separately.
	inline constexpr const char* kMemoryImagePrefix = "mem:";
}
