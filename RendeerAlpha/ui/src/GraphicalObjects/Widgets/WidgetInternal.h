#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

// What the widget sources share and nothing outside them should see: private to
// src/GraphicalObjects/Widgets/, which is why it is not under include/.
namespace RDA::detail {
	float mainAxisRequest(const Widget& child, Gui& gui, glm::vec2 available, bool vertical);
	float crossAxisRequest(const Widget& child, Gui& gui, glm::vec2 available, bool vertical);
	bool readNumber(const std::string& text, double& out);
	const std::string& either(const std::string& first, const std::string& second);
}
