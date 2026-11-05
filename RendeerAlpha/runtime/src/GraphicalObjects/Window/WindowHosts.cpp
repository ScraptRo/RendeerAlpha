#include <GraphicalObjects/Window.h>
#include <Platform/Accessibility.h>
#include <Platform/TextInput.h>

// What the platform hosts are told after each frame: the screen reader, what changed; the
// input method, whether a field has the keyboard and where its caret is.
namespace RDA {

	void Window::textInputFrameDone() {
		if (!mTextInput) return;
		const Rect caret = mGui.textCaret();
		const float k = mGui.unitScale();
		mTextInput->update(mGui.wantsTextEntry(), Rect{ caret.x * k, caret.y * k, caret.w * k, caret.h * k });
	}

	void Window::accessibilityFrameDone() {
		if (!mA11yHost || !mA11yBridge) return;
		// Read here, on the loop thread, because nowhere else may ask the window system;
		// handed over only when it changed, so an idle frame takes no lock.
		AccessibleWindow facts;
		mOs.getPosition(facts.x, facts.y);
		facts.focused = isFocused();
		facts.title = mInfo.name;
		if (!(facts == mA11yWindowSeen)) {
			mA11yWindowSeen = facts;
			mA11yBridge->setWindow(facts);
		}
		mA11yHost->frameDone();
	}
}
