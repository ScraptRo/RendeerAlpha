#pragma once
#include <functional>
#include <memory>
#include <Core/Signals.h>
#include <Layout/Blueprint.h>
#include <string>
#include <vector>

namespace RDA {
	class Widget;
}

// Turning a compiled blueprint into live widgets.
//
// This is the half of the layout pipeline that ships. It contains no parser, no
// JavaScript engine and no knowledge of TypeScript — it walks a POD array, calls
// constructors, and hooks up the bindings the compiler left behind.
//
// The walk is a single forward loop rather than a recursion, because the blueprint
// stores nodes breadth-first and Blueprint::parse() has already established that every
// parent appears before its children.
namespace RDA::Layout {

	class ScriptModule;

	// What instantiating a layout produced.
	//
	// It exists because bindings outlive the call that made them: they are registered
	// with the signal graph and will be evaluated whenever their signals change. Something
	// has to be able to say "these are finished" — when the widgets go away, and when a
	// layout is reloaded and replaced by a newer one. That something is this.
	//
	// Releasing forgets the bindings; it does not remove the widgets, because the widget
	// tree owns those and knows how.
	class LayoutInstance {
	public:
		LayoutInstance() = default;
		~LayoutInstance() { release(); }

		LayoutInstance(const LayoutInstance&) = delete;
		LayoutInstance& operator=(const LayoutInstance&) = delete;
		LayoutInstance(LayoutInstance&& other) noexcept { *this = std::move(other); }
		LayoutInstance& operator=(LayoutInstance&& other) noexcept;

		RDA::Widget* root() const { return mRoot; }
		bool valid() const { return mRoot != nullptr; }
		size_t bindingCount() const { return mObservers ? mObservers->ids.size() : 0; }

		// Stops every binding this instance created. Safe to call twice.
		void release();

		// A layout's onOpen and onClose. Every open is answered by exactly one close, unless
		// the process dies, so each runs at most once whatever calls it. Separate from
		// building, because a Router builds the next screen before it lets go of the last
		// and the two have to be heard in the other order.
		void announceOpen();
		void announceClose();
		bool announced() const { return mObservers->announced; }
		// Open already, with nothing said: a reload is the same layout, not a new one.
		void markAnnounced();

		// Used by instantiate(); public so the loader is not a friend of everything.
		void adopt(RDA::Widget* root) { mRoot = root; }
		void track(ObserverId observer) { mObservers->ids.push_back(observer); }

		// Every binding this layout made, shared with whatever goes on making them after it
		// loaded -- a list that builds a row when its view grows. Released together, and a
		// sink that has been released takes nothing more.
		struct ObserverSink {
			std::vector<ObserverId> ids;
			bool                    open = true;
			// The layout's functions, when it has any. Held here because everything that
			// can still run one -- a list building a row late -- holds this too.
			std::shared_ptr<ScriptModule> module;
			// Here rather than on a widget, because no widget answers them, and because a
			// host moves and this moves with it.
			std::function<void(const Value*)> onOpen;
			std::function<void(const Value*)> onClose;
			bool                              announced = false;
		};
		std::shared_ptr<ObserverSink> sink() const { return mObservers; }

	private:
		RDA::Widget*                  mRoot = nullptr;
		std::shared_ptr<ObserverSink> mObservers = std::make_shared<ObserverSink>();
	};

	// Runs onClose on every layout still open. Called by the engine on its way out, before
	// the application's onShutdown, and not from a destructor: an application's LayoutHost
	// is often a global, and those go after everything a handler needs.
	void closeOpenLayouts();

	// Builds `blueprint` under `parent`.
	//
	// `idPrefix` is prepended to every id, so instantiating the same layout twice gives
	// two sets of widgets that do not share interaction state.
	//
	// Widgets whose type is not known are skipped along with their children, and the rest
	// of the tree is still built: a layout compiled against a newer engine loads as much
	// of itself as this one understands rather than nothing at all.
	//
	// A binding whose signal has not been declared gets one, as a number, and says so —
	// the alternative is a layout that reads a misspelled name and shows nothing while
	// looking correct.
	LayoutInstance instantiate(const Blueprint& blueprint, RDA::Widget& parent,
	                           const std::string& idPrefix = {});

	// Whether this build can make a widget of that type.
}
