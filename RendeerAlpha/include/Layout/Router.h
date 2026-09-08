#pragma once
#include <cstdint>
#include <Core/Signals.h>
#include <Layout/LayoutHost.h>
#include <string>
#include <vector>

namespace RDA {
	class Widget;
}

// Which screen is showing, and how it changes.
//
// A route is a screen: a name, the layout it shows, and optionally the signals that are
// its arguments -- which product, which page. The name of the current one lives in a
// signal called `route`, so navigating is an ordinary write:
//
//     onClick={() => state.route = "catalogue"}
//
// That is the whole interface to this. It needed no new grammar and no new concept
// because "which screen is showing" is state, and this engine already has one answer for
// state. It is reactive for the same reason: a label bound to `state.route` updates when
// the screen changes, without being told that navigation exists.
//
// Params are ordinary signals too, named by the route rather than passed to it. Writing
// them and the route in one handler is one navigation, because the router looks once per
// frame rather than per write:
//
//     onClick={() => { state.productId = 12; state.route = "product" }}
//
// Naming them is what makes going back correct: history records the route *and* the value
// of every param that route declared, and restores both.
//
// The swap is total -- the widgets are thrown away and the new layout is built -- which
// is only correct because signals live outside the tree being replaced. That is the same
// property that makes hot reload here simpler than Fast Refresh, and it is why a screen
// can be left and returned to without anything being preserved by hand.
namespace RDA::Layout {

	class Router {
	public:
		// One screen, as the generated route table gives it.
		struct Route {
			std::string              name;
			std::string              layout;
			std::vector<std::string> params;
		};

		// `parent` must outlive this. Blueprints are looked for at
		// `<layoutDir>/<layout>.rdab`, and sources -- only in a build that can compile --
		// at `<sourceDir>/<layout>.tsx`.
		//
		// Opens whichever route the `route` signal already holds, which is the first one
		// declared unless something set it first. Setting it before this is how an
		// application opens on a screen chosen by a command line or a saved session.
		bool open(RDA::Widget& parent, std::vector<Route> routes,
		          std::string layoutDir, std::string sourceDir = {});

		// Call once per frame from the application's update, not from a widget callback:
		// a swap restructures the tree, and a callback fires while that tree is walked.
		// Returns true when the screen changed or the current one was reloaded.
		bool update();

		// From C++. Same effect as the interface writing `state.route`.
		bool navigate(std::string_view name);

		// Both take effect on the next update() rather than at once, because both are
		// reachable from a widget callback -- `commands.back()` fires while the tree that
		// button lives in is being walked, and swapping it there destroys the walk from
		// under itself. Deferring is what makes them safe to call from anywhere, which is
		// the only way a back button can be an ordinary button.
		bool back();
		bool forward();
		bool canGoBack() const { return mPosition > 0; }
		bool canGoForward() const { return mPosition + 1 < mHistory.size(); }

		// How long a screen takes to cross-fade into the next, in milliseconds. Zero is
		// instant, which is what a swap was before this and still is unless asked.
		//
		// Both screens exist for the length of one: the outgoing tree is kept, painted and
		// faded out rather than destroyed on the spot. That is the whole cost, and it is
		// why this is a number rather than always on -- a screen holding ten thousand rows
		// is a screen worth taking down promptly.
		void  setTransitionMs(float ms) { mTransitionMs = ms; }
		float transitionMs() const { return mTransitionMs; }

		const std::string& current() const { return mCurrent; }
		LayoutHost&        host() { return mHost; }
		const LayoutHost&  host() const { return mHost; }
		const std::string& lastError() const { return mLastError; }

	private:
		// A signal's value, kept so going back can put it back. Which of the three fields
		// matters is the signal's own type, which does not change once it is declared.
		struct Snapshot {
			uint32_t    signal = kNoSignal;
			SignalType  type = SignalType::Number;
			double      number = 0.0;
			std::string text;
		};
		struct Entry {
			std::string           route;
			std::vector<Snapshot> params;
		};

		const Route* find(std::string_view name) const;
		bool  show(const Route& route);          // swap the layout, no history
		bool  wentBackward(const std::string& name) const;
		bool  retireFinished();                  // true if anything was taken down
		void  retireAll();
		Entry snapshot(const Route& route) const;
		void  restore(const Entry& entry);

		// The container the showing screen's tree lives under. Owned by mParent; the
		// router only holds the pointer and the id it needs to take it away again.
		RDA::Widget* mScreen = nullptr;

		// Screens on their way out, oldest first -- which is also the order they are
		// painted in, so each fades over the one before it.
		//
		// More than one, because navigating again before a transition has finished must
		// not make the screen already leaving vanish on the spot. The list is bounded by
		// how many navigations fit inside one transition, and empties itself.
		struct Leaving {
			RDA::Widget* screen = nullptr;
			std::string  id;
			LayoutHost   host;
		};
		std::vector<Leaving> mLeaving;

		float  mTransitionMs = 0.0f;
		size_t mScreenSerial = 0;
		// Which way the next transition slides, when the answer is not the declared order:
		// going back through history slides back whatever the route table's order says.
		// 0 means nobody asked and the order decides.
		float mNextDirection = 0.0f;

		RDA::Widget*       mParent = nullptr;
		std::vector<Route> mRoutes;
		std::string        mLayoutDir;
		std::string        mSourceDir;
		LayoutHost         mHost;

		uint32_t    mRouteSignal = kNoSignal;
		std::string mCurrent;      // what is showing, and what the signal said last frame
		std::string mLastError;

		std::vector<Entry> mHistory;
		size_t             mPosition = 0;
		// Where back()/forward() asked to go, applied by the next update().
		size_t mPending = 0;
		bool   mHasPending = false;
	};
}
