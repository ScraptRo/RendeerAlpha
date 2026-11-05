#include <Core/Wake.h>

#include <atomic>

namespace RDA::Wake {

	namespace {
		// True to begin with, so the first frame is drawn whatever else happens.
		std::atomic<bool> gFrameRequested{ true };
		std::atomic<bool> gWakeRequested{ false };
		std::atomic<void (*)()> gPoster{ nullptr };
		std::atomic<void (*)()> gInterfaceChanged{ nullptr };

		void post() {
			if (void (*poster)() = gPoster.load()) poster();
		}
	}

	void requestFrame() {
		gFrameRequested.store(true);
		gWakeRequested.store(true);
		post();
	}

	void wake() {
		gWakeRequested.store(true);
		post();
	}

	void interfaceChanged() {
		if (void (*handler)() = gInterfaceChanged.load()) handler();
		requestFrame();
	}

	bool frameRequested() { return gFrameRequested.load(); }
	bool takeFrameRequest() { return gFrameRequested.exchange(false); }
	bool takeWake() { return gWakeRequested.exchange(false); }

	void setPoster(void (*poster)()) { gPoster.store(poster); }
	void setInterfaceChangedHandler(void (*handler)()) { gInterfaceChanged.store(handler); }
}
