#include <Core/LoopWork.h>

namespace RDA {

	LoopWork& loopWork() {
		static LoopWork work;
		return work;
	}

	void LoopWork::setServiceThread() {
		std::lock_guard<std::mutex> lock(mMutex);
		mServiceThread = std::this_thread::get_id();
		mHasServiceThread = true;
		mStopped = false;
	}

	bool LoopWork::onServiceThread() const {
		std::lock_guard<std::mutex> lock(mMutex);
		return mHasServiceThread && mServiceThread == std::this_thread::get_id();
	}

	bool LoopWork::request(const std::function<void()>& work,
	                       std::chrono::milliseconds timeout) {
		if (!work) return false;

		bool runHere = false;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			// Already there, or nobody is servicing: run it here. Waiting would be waiting
			// for ourselves or waiting for nothing, and both are worse than doing.
			runHere = !mHasServiceThread ||
			          mServiceThread == std::this_thread::get_id();
		}
		if (runHere) {
			// Outside the lock: the work is arbitrary and will come back into the engine,
			// and holding this while it does is how that deadlocks.
			work();
			return true;
		}

		Request request;
		request.work = &work;

		std::unique_lock<std::mutex> lock(mMutex);
		if (mStopped) return false;
		mWaiting.push_back(&request);
		mFinished.wait_for(lock, timeout, [&] { return request.done || mStopped; });

		if (!request.done) {
			// Timed out, or the engine stopped. Take it back out, or the loop would later
			// run work whose caller has given up and whose stack has gone.
			for (size_t i = mWaiting.size(); i-- > 0;) {
				if (mWaiting[i] == &request) {
					mWaiting.erase(mWaiting.begin() + i);
					break;
				}
			}
			return false;
		}
		return true;
	}

	size_t LoopWork::service() {
		std::vector<Request*> batch;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mWaiting.empty()) return 0;
			batch.swap(mWaiting);
		}

		// Run outside the lock: a request that creates a texture will upload it, and that
		// is not something to do while holding a mutex other threads are queueing on.
		for (Request* request : batch) (*request->work)();

		{
			std::lock_guard<std::mutex> lock(mMutex);
			for (Request* request : batch) request->done = true;
		}
		mFinished.notify_all();
		return batch.size();
	}

	void LoopWork::stop() {
		{
			std::lock_guard<std::mutex> lock(mMutex);
			mStopped = true;
			mHasServiceThread = false;
			mWaiting.clear();
		}
		// Everything still waiting is released rather than left to time out, so shutting
		// down does not take five seconds per blocked caller.
		mFinished.notify_all();
	}
}
