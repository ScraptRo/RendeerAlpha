#include <GraphicalObjects/Widgets/Controls/ShortcutKey.h>
#include <GraphicalObjects/Gui.h>

namespace RDA {

	void ShortcutKey::paint(Gui& gui, glm::vec2 origin) {
		(void)origin;
		if (shortcut.valid() && gui.wantsShortcuts()) gui.offerShortcut(this);
	}
}
