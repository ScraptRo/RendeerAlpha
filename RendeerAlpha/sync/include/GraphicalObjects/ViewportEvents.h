#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace RDA {

	// What happened to a <viewport>, for a backend that draws in one: it was resized, so
	// the drawing has to be made again at the new size; or the pointer did something over
	// it -- a graph to pan, a plot to zoom, a model to orbit.
	//
	// Summed rather than queued. What a pan or an orbit wants is how far the pointer
	// travelled since the drawing was last made, not the forty positions in between, and
	// what a redraw wants is the size now, not every size a dragged splitter passed
	// through. A backend that is busy drawing must not fall further behind for having been
	// busy. So movement and the wheel add up until somebody takes them, a button records
	// that it went down or came up at least once, `resized` that the size changed at least
	// once, and the position and the size are simply the latest.
	struct ViewportEvent {
		float    x = 0.0f, y = 0.0f;   // where the pointer is, in the viewport's own coordinates
		float    dx = 0.0f, dy = 0.0f; // how far it moved with a button held, summed
		float    wheel = 0.0f;         // notches, summed; positive is away from the reader
		uint32_t held = 0;             // buttons down now that went down over it: 1 left, 2 right, 4 middle
		uint32_t pressed = 0;          // went down over it since last taken
		uint32_t released = 0;         // came up since last taken -- only ones that went down over it
		int      clicks = 0;           // of the most recent press: 2 is a double click, 3 a triple
		bool     inside = false;       // the pointer is over it now
		uint32_t mods = 0;             // with the latest pointer event: 1 shift, 2 ctrl, 4 alt
		float    width = 0.0f;         // its size as last laid out, in layout units
		float    height = 0.0f;
		bool     resized = false;      // the size changed since last taken

		// What kind of report this is, so folding one in leaves alone what it does not
		// speak for -- a resize carries no pointer, and must not move the pointer to 0, 0.
		bool     hasPointer = false;
		bool     hasSize = false;

		// Folds `later` onto this, as though both had been read at once.
		void merge(const ViewportEvent& later);
		// Forgets what was summed -- movement, the wheel, presses, releases, the resize --
		// and keeps what is a state: where the pointer is, what is held, how big it is.
		void settle();
	};

	// What every viewport has been through, waiting to be taken.
	//
	// The one part of the viewport machinery that is not for the loop thread only. The
	// widget reports from the frame, and a backend reads from wherever it likes -- which is
	// the point: a backend that asked the loop thread every frame would be asking for a
	// frame every frame, and an idle window would never be idle. Its own lock, the way the
	// command queue and the dropped files have theirs.
	class ViewportEventQueue {
	public:
		// From the loop thread, whenever something happened to `name`.
		void report(const std::string& name, const ViewportEvent& event);

		// Everything since the last take. False when nothing new arrived, and `out` is then
		// still filled with where the pointer is and how big the viewport is -- so asking
		// is never a wasted call.
		bool take(const std::string& name, ViewportEvent& out);

		// Blocks until some viewport has something not yet taken, and names it; empty on a
		// timeout or when the queue is cleared. A negative timeout waits for as long as it
		// takes. Never touches the loop, so a thread can sit in here for the life of the
		// window at no cost to anything.
		std::string wait(int timeoutMs);

		// Drops every entry and lets anyone waiting go, answering empty. For shutdown.
		void clear();

	private:
		struct Entry {
			std::string   name;
			ViewportEvent state;
			bool          pending = false;
		};
		Entry* find(const std::string& name);

		std::mutex              mMutex;
		std::condition_variable mArrived;
		// A handful at most, in the order they were first touched -- which is also the
		// order wait() answers in, so two busy viewports take turns rather than one
		// starving the other.
		std::vector<Entry>      mEntries;
		size_t                  mNextToAnswer = 0;
		uint64_t                mCleared = 0;
	};
}
