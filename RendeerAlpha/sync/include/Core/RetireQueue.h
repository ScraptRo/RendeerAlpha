#pragma once
#include <mutex>
#include <utility>
#include <vector>

// Things whose owner is gone, waiting for a moment it is safe to destroy them. Safe from any
// thread.
//
// A GPU resource cannot be destroyed the instant nothing owns it: a frame in flight may still
// be sampling it, and a frame being recorded may already name it. So the owner hands it here
// instead, and whoever knows the GPU has caught up -- the frame loop, after waiting for it --
// takes everything and frees it.
//
// That is the whole of it, and it is deliberately only that. When to take, what to wait for
// first and what "free" means differ between the loop's per-frame drain and teardown's last
// sweep, and those are decisions about the frame, not about the list. The queue holds items
// and gives them back, in the order they were retired; it never destroys one itself, so the
// destructor of whatever it holds runs where the caller chose and nowhere else.
//
// The lock is for the retiring side. A widget is torn down on whichever thread runs the
// layout, a backend may hand a picture over from its own, and the loop takes. Taking swaps
// the list out under the lock and returns it, so nothing is destroyed while the lock is held.
namespace RDA {

	template <class T>
	class RetireQueue {
	public:
		RetireQueue() = default;
		RetireQueue(const RetireQueue&) = delete;
		RetireQueue& operator=(const RetireQueue&) = delete;

		// Adds `item` after everything retired so far.
		void retire(T item) {
			std::lock_guard<std::mutex> held(mLock);
			mItems.push_back(std::move(item));
		}

		// Everything retired so far, oldest first, leaving the queue empty. Items retired
		// from another thread while the caller works through the result are not lost: they
		// are in the queue for the next take.
		std::vector<T> take() {
			std::vector<T> out;
			std::lock_guard<std::mutex> held(mLock);
			out.swap(mItems);
			return out;
		}

	private:
		std::mutex     mLock;
		std::vector<T> mItems;
	};
}
