#pragma once
#include <string>
#include <vector>

// What the font atlas sources share and nothing outside them should see: private to
// renderer/src/GraphicalSrc/Font/, which is why it is not under include/.
namespace RDA {

	namespace detail {
		// The system's fallback faces that exist on this machine, most likely first. Defined in
		// FontDiscovery.cpp, which says how the order is chosen; asked by FontAtlas::bake.
		std::vector<std::string> systemFallbacks();
	}
}
