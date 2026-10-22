#include <GraphicalObjects/ViewportEvents.h>
#include <chrono>

namespace RDA {

	void ViewportEvent::merge(const ViewportEvent& later) {
		if (later.hasPointer) {
			x = later.x;
			y = later.y;
			held = later.held;
			inside = later.inside;
			mods = later.mods;
			hasPointer = true;
		}
		dx += later.dx;
		dy += later.dy;
		wheel += later.wheel;
		pressed |= later.pressed;
		released |= later.released;
		// The count belongs to a press, so only a later press replaces it. A move after a
		// double click must not turn it back into a single one before anybody looked.
		if (later.pressed) clicks = later.clicks;
		if (later.hasSize) {
			width = later.width;
			height = later.height;
			hasSize = true;
		}
		resized = resized || later.resized;
	}

	void ViewportEvent::settle() {
		dx = dy = wheel = 0.0f;
		pressed = released = 0;
		resized = false;
	}

	ViewportEventQueue::Entry* ViewportEventQueue::find(const std::string& name) {
		for (Entry& entry : mEntries) {
			if (entry.name == name) return &entry;
		}
		return nullptr;
	}

	void ViewportEventQueue::report(const std::string& name, const ViewportEvent& event) {
		{
			std::lock_guard<std::mutex> lock(mMutex);
			Entry* entry = find(name);
			if (!entry) {
				mEntries.push_back({ name, ViewportEvent{}, false });
				entry = &mEntries.back();
			}
			// Folded in either way, so a report that speaks only for the size keeps the
			// pointer where the last one put it. Not pending means everything summed was
			// already taken, and starts again from nothing.
			if (!entry->pending) entry->state.settle();
			entry->state.merge(event);
			entry->pending = true;
		}
		mArrived.notify_all();
	}

	bool ViewportEventQueue::take(const std::string& name, ViewportEvent& out) {
		std::lock_guard<std::mutex> lock(mMutex);
		Entry* entry = find(name);
		if (!entry) {
			out = ViewportEvent{};
			return false;
		}
		out = entry->state;
		if (!entry->pending) {
			// Nothing new: where it is, what is held and how big it is, with nothing
			// having happened.
			out.settle();
			return false;
		}
		entry->pending = false;
		return true;
	}

	std::string ViewportEventQueue::wait(int timeoutMs) {
		std::unique_lock<std::mutex> lock(mMutex);
		const uint64_t cleared = mCleared;
		std::string answer;

		auto ready = [&] {
			if (mCleared != cleared) return true;
			const size_t count = mEntries.size();
			for (size_t i = 0; i < count; ++i) {
				const size_t at = (mNextToAnswer + i) % count;
				if (mEntries[at].pending) {
					answer = mEntries[at].name;
					mNextToAnswer = (at + 1) % count;
					return true;
				}
			}
			return false;
		};

		if (timeoutMs < 0) {
			mArrived.wait(lock, ready);
		} else {
			mArrived.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
		}
		return answer;
	}

	void ViewportEventQueue::clear() {
		{
			std::lock_guard<std::mutex> lock(mMutex);
			mEntries.clear();
			mNextToAnswer = 0;
			++mCleared;
		}
		mArrived.notify_all();
	}
}
