#pragma once
#include <GraphicalObjects/Accessible.h>
#include <memory>

struct GLFWwindow;

// The other half of GraphicalObjects/Accessible.h: speaking the tree to the screen reader
// a particular system has.
//
// One per window, attached when it opens and detached before it closes. On Windows it is
// a UI Automation provider; elsewhere, for now, nothing -- attach() returns a host that
// does nothing, the GUI never builds a tree because nobody ever says they are listening,
// and the cost of that is one check per frame.
namespace RDA {

	class AccessibilityHost {
	public:
		static std::unique_ptr<AccessibilityHost> attach(GLFWwindow* window,
		                                                 std::shared_ptr<AccessibleBridge> bridge);
		virtual ~AccessibilityHost() = default;
		// After every frame, once the GUI has published what it found: tell whoever is
		// listening what moved -- the focus, a checkbox ticked, a value changed, nodes that
		// came and went, the window gaining the keyboard. On the loop thread. Cheap when
		// nothing is listening; each layer checks that first.
		virtual void frameDone() {}
	};
}
