#include <Layout/Router.h>
#include <Core/Commands.h>
#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <RendeerAlpha.h>

namespace RDA::Layout {

	namespace {
		// One screen's tree, faded and slid by however far through its transition it is.
		//
		// A route swap used to destroy the outgoing tree and build the incoming one in its
		// place, which left no moment where both existed -- and a cross-fade is exactly a
		// moment where both exist. So each screen gets one of these to live under, and the
		// old one is kept, painted and faded until it is done rather than taken down at
		// once. Inserting a container changes no widget's id: the compiler baked the full
		// paths into the blueprint, so `root/nav/home` is `root/nav/home` however deep the
		// tree it hangs from is.
		//
		// It works out its own progress rather than being told, because it is the thing
		// with a Gui in its hand -- the router only asks, once a frame, whether it is done.
		class ScreenFader : public Container {
		public:
			// `seconds` is how long this one's arrival takes. A departure is given its
			// duration when it is asked to leave, because the router's setting can have
			// changed since -- and a screen created before setTransitionMs() was called
			// would otherwise have nothing to leave over and vanish on the spot.
			//
			// `intro` is whether it fades in. The first screen an application opens has
			// nothing to fade in from, and fading up from an empty window on launch is a
			// splash screen nobody asked for.
			// `direction` is which way the pair of screens moves: 1 forward, -1 back. Both
			// of them are told the same one, so the screen arriving comes from the side
			// the screen leaving is going to. Given them separately they slid past each
			// other in opposite directions, which reads as two things happening rather
			// than as one screen replacing another.
			ScreenFader(std::string id, float seconds, bool intro, float direction)
				: Container(std::move(id)), mSeconds(seconds), mDirection(direction),
				  mState(intro && seconds > 0.0f ? State::Arriving : State::Idle) {}

			// Turns a screen into a leaving one. `direction` is which way it slides out:
			// -1 for back, 1 for forward.
			void leave(float direction, float seconds) {
				mDirection = direction;
				mResuming = (mState == State::Arriving); // caught on its way in
				mState = State::Leaving;
				mSeconds = seconds;
				mStarted = false; // a transition of its own; where it starts is below
				mFinished = seconds <= 0.0f;
			}
			bool finished() const { return mFinished; }

			void paint(Gui& gui, glm::vec2 origin) override {
				if (mState == State::Idle) { Container::paint(gui, origin); return; }
				const bool leaving = mState == State::Leaving;

				const uint32_t key = gui.motionKey(id().c_str(), kMotionOpen);
				if (!mStarted) {
					// A screen told to leave before it had finished arriving leaves from
					// the opacity it had reached, so it takes the rest of the way out
					// rather than the whole way. Starting a departure at solid is what
					// made navigating twice quickly flash the screen in the middle: it
					// jumped to full and only then began to go.
					const float reached = mResuming
						? gui.motion().value(key, 1.0f, mSeconds, Easing::InOut)
						: 1.0f;
					gui.motion().reset(key, leaving ? 1.0f - reached : 0.0f);
					mStarted = true;
					mResuming = false;
				}
				const float progress = gui.motion().value(key, 1.0f, mSeconds, Easing::InOut);
				if (progress >= 0.999f) {
					if (leaving) mFinished = true;
					else mState = State::Idle; // arrived; nothing more to do than exist
				}

				const Rect abs = placement(gui, origin);
				const float travel = abs.w * 0.04f; // a hint of movement, not a journey
				const float alpha = leaving ? 1.0f - progress : progress;
				const float dx = leaving ? -mDirection * travel * progress
				                         :  mDirection * travel * (1.0f - progress);
				// A screen still invisible on its way out has nothing left to contribute
				// and takes no input either. One arriving is walked from its first frame
				// even so, because that is the tree a click during a transition belongs
				// to and it has to be there to receive one.
				if (alpha <= 0.004f && leaving) return;

				// Clipped to itself, so a screen on its way in or out draws nothing past
				// the edge of the window it is sliding across.
				gui.pushClipRect(abs);
				gui.pushOpacity(alpha);
				// A screen on its way out takes no input. Both trees are under the pointer
				// during a transition and both are walked, so without this a click aimed
				// at the arriving screen could land on the one being left -- on a widget
				// that is not there any more as far as anyone looking is concerned.
				//
				// The one arriving takes input where it is drawn, slide and all. Testing
				// it where it will land instead was tried and is worse: the pointer is
				// then over one button and pressing another. Sliding out from under a
				// press cancels it, the same as moving the pointer off a button does --
				// a click that does nothing, rather than a click that does the wrong
				// thing.
				if (leaving) gui.pushInert();
				paintChildren(gui, glm::vec2(abs.x + dx, abs.y));
				if (leaving) gui.popInert();
				gui.popOpacity();
				gui.popClipRect();
			}

		private:
			enum class State : uint8_t { Idle, Arriving, Leaving };

			float mSeconds = 0.0f;
			float mDirection = 1.0f;
			State mState = State::Idle;
			bool  mStarted = false;
			bool  mFinished = false;
			bool  mResuming = false; // asked to leave while it was still arriving
		};
	}

	bool Router::open(RDA::Widget& parent, std::vector<Route> routes,
	                  std::string layoutDir, std::string sourceDir) {
		if (routes.empty()) {
			mLastError = "no routes were declared";
			RDA_LOG_ERROR("Router: " << mLastError);
			return false;
		}
		mParent = &parent;
		mRoutes = std::move(routes);
		mLayoutDir = std::move(layoutDir);
		mSourceDir = std::move(sourceDir);

		mRouteSignal = signals().find("route");
		if (mRouteSignal == kNoSignal) {
			mLastError = "no `route` signal; declaring routes is what creates it, so this "
			             "application's state was generated without any";
			RDA_LOG_ERROR("Router: " << mLastError);
			return false;
		}

		// back() and forward() are commands like any other, so a layout gets a back button
		// without the application writing one -- and an application that wants different
		// behaviour can bind over them, because nothing here is privileged.
		if (const uint32_t back = commands().find("back"); back != kNoCommand) {
			commands().bind(back, [this] { this->back(); });
		}
		if (const uint32_t forward = commands().find("forward"); forward != kNoCommand) {
			commands().bind(forward, [this] { this->forward(); });
		}

		// Whatever the signal already says -- the first declared route, unless something
		// set it first. That is how an application opens on a screen a command line chose.
		const std::string wanted(signals().text(mRouteSignal));
		const Route* route = find(wanted);
		if (!route) {
			RDA_LOG_WARNING("Router: nothing routes to '" << wanted << "'; opening '"
			                << mRoutes.front().name << "'");
			route = &mRoutes.front();
		}
		if (!show(*route)) return false;

		mHistory.clear();
		mHistory.push_back(snapshot(*route));
		mPosition = 0;
		return true;
	}

	bool Router::update() {
		if (!mParent) return false;

		// Screens that have finished fading out. Swept here rather than in paint, where
		// taking a widget out of the tree being walked is exactly what is not allowed.
		if (retireFinished()) rendeerInterfaceChanged();

		// A history move asked for by a button, applied here rather than where it was
		// asked for. restore() puts the route signal back itself, so the check below sees
		// no change and this is one navigation rather than two.
		if (mHasPending) {
			mHasPending = false;
			mPosition = mPending;
			restore(mHistory[mPosition]);
			return true;
		}

		// One string compare a frame. Cheaper than observing the signal, and it puts the
		// swap in the update phase where restructuring the tree is safe -- a binding
		// writing `state.route` inside a click handler must not tear down the tree that
		// handler is being walked in.
		const std::string wanted(signals().text(mRouteSignal));
		if (wanted != mCurrent) {
			const Route* route = find(wanted);
			if (!route) {
				// The signal is typed as a union in TypeScript, so this is either C++
				// writing it or a layout compiled past the check. Either way the screen
				// stays put rather than going blank.
				mLastError = "nothing routes to '" + wanted + "'";
				RDA_LOG_WARNING("Router: " << mLastError << "; staying on '" << mCurrent << "'");
				signals().set(mRouteSignal, std::string_view(mCurrent));
				return false;
			}
			if (show(*route)) {
				// Anything gone forward from is gone: history is where you have been, and
				// leaving from the middle of it makes the rest another timeline.
				mHistory.resize(mPosition + 1);
				mHistory.push_back(snapshot(*route));
				mPosition = mHistory.size() - 1;
				return true;
			}
			return false;
		}
		return mHost.reloadIfChanged();
	}

	bool Router::navigate(std::string_view name) {
		if (!find(name)) {
			mLastError = "nothing routes to '" + std::string(name) + "'";
			RDA_LOG_WARNING("Router: " << mLastError);
			return false;
		}
		// Through the signal rather than around it, so C++ navigating and the interface
		// navigating are the same event -- one path, and anything bound to `state.route`
		// sees both.
		signals().set(mRouteSignal, name);
		return true;
	}

	bool Router::back() {
		if (!canGoBack()) return false;
		mPending = mPosition - 1;
		mHasPending = true;
		// Going back slides back, whatever order the routes happen to be declared in.
		// Where you have been is a better answer than where a route sits in a table, and
		// it is the only one available when both screens are the same route.
		mNextDirection = -1.0f;
		rendeerRequestRedraw(); // so an idle window comes back for the frame that swaps
		return true;
	}

	bool Router::forward() {
		if (!canGoForward()) return false;
		mPending = mPosition + 1;
		mHasPending = true;
		mNextDirection = 1.0f;
		rendeerRequestRedraw();
		return true;
	}

	const Router::Route* Router::find(std::string_view name) const {
		for (const Route& route : mRoutes) {
			if (route.name == name) return &route;
		}
		return nullptr;
	}

	bool Router::show(const Route& route) {
		// Read before anything can fail: a navigation that does not happen leaves no hint
		// behind for the next one to pick up.
		const float asked = mNextDirection;
		mNextDirection = 0.0f;

		const std::string blueprint = mLayoutDir + "/" + route.layout + ".rdab";
		const std::string source = mSourceDir.empty()
			? std::string() : mSourceDir + "/" + route.layout + ".tsx";

		// Each screen gets a container of its own to live under, so two of them can exist
		// at once while one fades into the other. Built before the old one is touched: a
		// screen that will not load must not take the running one down with it, which is
		// the same rule reload follows.
		const bool crossFade = mTransitionMs > 0.0f && mScreen != nullptr;
		// Worked out before either screen is built, because both of them need it: a
		// history move knows which way it went, and otherwise going backwards through the
		// declared routes slides the other way, so the movement agrees with the order the
		// screens are written in.
		const float direction = asked != 0.0f
			? asked
			: (wentBackward(route.name) ? -1.0f : 1.0f);
		auto fader = std::make_unique<ScreenFader>(
			"#screen" + std::to_string(++mScreenSerial), mTransitionMs / 1000.0f,
			crossFade, direction);
		Widget* arriving = mParent->addChild(std::move(fader));

		LayoutHost host;
		if (!host.open(*arriving, blueprint, source)) {
			mLastError = "route '" + route.name + "' could not load " + blueprint;
			RDA_LOG_ERROR("Router: " << mLastError);
			mParent->remove(arriving->id());
			return false;
		}

		// Only now is the old one let go of. With no transition asked for it goes at once,
		// exactly as it did before any of this; with one, it joins the list of screens
		// still leaving and update() takes it down when its fader says it is done.
		if (crossFade) {
			static_cast<ScreenFader*>(mScreen)->leave(direction, mTransitionMs / 1000.0f);
			Leaving going;
			going.screen = mScreen;
			going.id = mScreen->id();
			going.host = std::move(mHost);
			mLeaving.push_back(std::move(going));
		} else if (mScreen) {
			retireAll(); // transitions are off; nothing gets to linger
			mHost = LayoutHost{};
			mParent->remove(mScreen->id());
		}

		mScreen = arriving;
		mHost = std::move(host);
		mCurrent = route.name;
		mLastError.clear();

		// The signal is the record of what is showing, so it is put right here too -- this
		// is the path back() takes, and it arrives with the signal still on the old name.
		if (std::string(signals().text(mRouteSignal)) != mCurrent) {
			signals().set(mRouteSignal, std::string_view(mCurrent));
		}
		RDA_LOG_INFO("Router: " << route.name << " (" << blueprint << ")");
		return true;
	}

	// Whether moving to `name` goes back through the declared order rather than forward.
	// Only used to decide which way a transition slides.
	bool Router::wentBackward(const std::string& name) const {
		size_t from = 0, to = 0;
		for (size_t i = 0; i < mRoutes.size(); ++i) {
			if (mRoutes[i].name == mCurrent) from = i;
			if (mRoutes[i].name == name) to = i;
		}
		return to < from;
	}

	// Takes down every screen that has finished fading out. A host's bindings go first,
	// while the signal table they registered with is still the one they know.
	bool Router::retireFinished() {
		bool any = false;
		for (size_t i = 0; i < mLeaving.size();) {
			if (!static_cast<ScreenFader*>(mLeaving[i].screen)->finished()) { ++i; continue; }
			mLeaving[i].host = LayoutHost{};
			mParent->remove(mLeaving[i].id);
			mLeaving.erase(mLeaving.begin() + static_cast<ptrdiff_t>(i));
			any = true;
		}
		return any;
	}

	void Router::retireAll() {
		for (Leaving& going : mLeaving) {
			going.host = LayoutHost{};
			mParent->remove(going.id);
		}
		mLeaving.clear();
	}

	Router::Entry Router::snapshot(const Route& route) const {
		Entry entry;
		entry.route = route.name;
		for (const std::string& param : route.params) {
			const uint32_t id = signals().find(param);
			if (id == kNoSignal) continue; // the schema checked this; a stale table has not
			Snapshot one;
			one.signal = id;
			one.type = signals().type(id);
			switch (one.type) {
			case SignalType::Text: one.text = std::string(signals().text(id)); break;
			case SignalType::Bool: one.number = signals().boolean(id) ? 1.0 : 0.0; break;
			default:               one.number = signals().number(id); break;
			}
			entry.params.push_back(std::move(one));
		}
		return entry;
	}

	void Router::restore(const Entry& entry) {
		// The params first, then the route: the screen is built after they are in place,
		// so the layout it builds reads the values it was left with rather than the ones
		// the screen being left had.
		for (const Snapshot& one : entry.params) {
			switch (one.type) {
			case SignalType::Text: signals().set(one.signal, std::string_view(one.text)); break;
			case SignalType::Bool: signals().set(one.signal, one.number != 0.0); break;
			default:               signals().set(one.signal, one.number); break;
			}
		}
		if (const Route* route = find(entry.route)) show(*route);
	}
}
