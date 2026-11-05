#pragma once
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

// A bounded queue that forgets the oldest, safe from any thread.
//
// What the C ABI hands a backend that polls -- commands that fired, files dropped on the
// window -- is produced on the loop thread and collected on whichever thread the backend
// polls from, on its own schedule. That needs a lock of its own (a poll must not cost a
// frame, which a hop to the loop would), and it needs a bound, because a backend that stops
// polling must not turn a held-down button into unbounded memory. Dropping the oldest keeps
// the most recent intent, which is the half worth keeping.
//
// Each of those used to be a mutex, a vector, a constant and an erase from the front,
// written out beside each other. This is that, once.
//
// It does not warn. What is worth saying about a full queue is the owner's to say, in its
// own words and once, so push() reports the drop and leaves the rest to the caller.
namespace RDA {

	template <class T>
	class Mailbox {
	public:
		// At most `bound` items are kept. A bound of zero would make every push a drop of
		// nothing, so it is one.
		explicit Mailbox(size_t bound) : mBound(bound ? bound : 1) {}

		Mailbox(const Mailbox&) = delete;
		Mailbox& operator=(const Mailbox&) = delete;

		// Adds `value` at the back. When the box was full the oldest item is discarded to
		// make room, and this returns true so the caller can say so.
		bool push(T value) {
			std::lock_guard<std::mutex> held(mLock);
			bool dropped = false;
			if (mItems.size() >= mBound) {
				mItems.pop_front();
				dropped = true;
			}
			mItems.push_back(std::move(value));
			return dropped;
		}

		// Takes the oldest item into `out`. False, and `out` untouched, when there is none.
		bool pop(T& out) {
			std::lock_guard<std::mutex> held(mLock);
			if (mItems.empty()) return false;
			out = std::move(mItems.front());
			mItems.pop_front();
			return true;
		}

		size_t size() const {
			std::lock_guard<std::mutex> held(mLock);
			return mItems.size();
		}

		void clear() {
			std::lock_guard<std::mutex> held(mLock);
			mItems.clear();
		}

	private:
		mutable std::mutex mLock;
		std::deque<T>      mItems;
		size_t             mBound;
	};
}
