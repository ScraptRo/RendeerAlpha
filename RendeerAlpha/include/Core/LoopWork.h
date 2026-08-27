#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace RDA {

	// Work that has to happen on the loop thread, asked for from anywhere.
	//
	// This is the other half of the scripting model, and the half a queue cannot do.
	// ScriptQueue carries *commands*: a script says "move that entity" and never needs an
	// answer, so the change can be recorded and applied later. Creating a resource is not
	// like that. `loadTexture(path)` has to hand back something the next line uses, so the
	// caller cannot be told "later" without changing what the call means.
	//
	// So: commands go one way and are applied at a defined point; resources go there and
	// come back. Both exist so a script can eventually run somewhere other than the loop
	// thread, which is the thing neither direct mutation nor a queue alone can survive.
	//
	// Called from the loop thread itself, request() simply runs the work. That is not a
	// special case bolted on -- it is the whole reason this can be introduced without
	// changing what anything does today, since every script currently runs inline on that
	// thread and gets exactly the behaviour it had before.
	class LoopWork {
	public:
		// Names the calling thread as the one that services requests. The engine calls
		// this as its loop starts.
		void setServiceThread();
		bool onServiceThread() const;

		// Runs `work` on the service thread and returns once it has finished.
		//
		// From the service thread, or before one exists, it runs inline -- there is
		// nothing to wait for and waiting would be waiting for itself. From anywhere else
		// it blocks until the loop picks it up, or until `timeout` passes.
		//
		// False means it never ran: the loop is gone, or too busy to have serviced it
		// inside the timeout. A caller should treat that as the resource not existing
		// rather than assume it does.
		bool request(const std::function<void()>& work,
		             std::chrono::milliseconds timeout = std::chrono::seconds(5));

		// Performs everything waiting. Called once per frame by the loop; returns how many
		// requests were run, which is 0 on the overwhelming majority of frames.
		size_t service();

		// Wakes everything still waiting and refuses further requests, so a script blocked
		// on a resource does not wait out its timeout while the engine shuts down.
		void stop();

	private:
		struct Request {
			const std::function<void()>* work = nullptr;
			bool done = false;
		};

		mutable std::mutex      mMutex;
		std::condition_variable mFinished;
		std::vector<Request*>   mWaiting;
		std::thread::id         mServiceThread;
		bool                    mHasServiceThread = false;
		bool                    mStopped = false;
	};

	LoopWork& loopWork();
}
