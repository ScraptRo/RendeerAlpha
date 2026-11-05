#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

namespace RDA {

	// A hole in the interface for something else to fill.
	//
	// The widget decides where it is and how big, and stops there. What appears inside is
	// the application's: a C++ program records Vulkan into it, every other language sends
	// a list of 2D commands the engine draws, and an application that does neither gets
	// the engine's own 3D scene, which is all this element used to be.
	class Viewport : public Widget {
	public:
		explicit Viewport(std::string id) : Widget(std::move(id)) {}

		// How a backend addresses this one. Two viewports with the same name are the same
		// surface drawn twice, which is occasionally what somebody wants.
		std::string name = "main";

		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		// Only once a backend listens to `name`; see Viewports::listen.
		void trackPointer(Gui& gui, const Rect& area);

		glm::vec2 mLastPointer{ 0.0f };
		uint32_t  mButtons = 0;      // down now, having gone down over this
		bool      mWasInside = false;
	};
}
