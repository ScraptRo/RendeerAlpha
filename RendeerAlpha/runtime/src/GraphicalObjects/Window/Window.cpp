#include <GraphicalObjects/Window.h>
#include <GraphicalSrc/VulkanInstance.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <Platform/Accessibility.h>
#include <Platform/TextInput.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
namespace RDA {
	stack_list windowList;

	Window::Window(WindowInfo& pInfo) : mInfo(pInfo), mOs(mInfo) {
		// Keys, buttons, the pointer and dropped files arrive here, before anything that hooks
		// the window's procedure -- the order every host below was written against.
		mOs.attachInput(mInput);

		// Ready for a screen reader from the start; nothing is described until one asks.
		mA11yBridge = std::make_shared<AccessibleBridge>();
		mGui.setAccessibleBridge(mA11yBridge.get());
		mA11yHost = AccessibilityHost::attach(mOs.handle(), mA11yBridge);
		mTextInput = TextInputHost::attach(mOs.handle(), mInput);
		// Last, so its window procedure is the outermost and answers the hit test before
		// anything else sees it.
		mOs.attachFrame(mInput);

		mOs.sync(); // seed the cached size before anything renders
		switch (mPresent.create(vulkanInstance(), mOs.handle(), cachedExtent(), mInfo.vsync)) {
		case PresentationSurface::Created::Ready:
			break;
		case PresentationSurface::Created::NoSurface:
			RDA_RUNTIME_ERROR("Failed to create window surface");
			break;
		case PresentationSurface::Created::NoFrameBuffer:
			RDA_RUNTIME_ERROR("Failed to create the window's surface FrameBuffer");
			break;
		case PresentationSurface::Created::Deferred:
			// Created with no area: a session restoring its windows minimised, or a
			// compositor that has not given this one a size yet. Not fatal, and not
			// something to build around -- the first frame that finds it restored
			// makes the swapchain then.
			RDA_LOG_INFO("Window created with a zero-sized surface; its swapchain is "
			             "built when the window is given a size");
			mOs.markResized();
			break;
		}

		windowList.addNode(&mID);
		RDA_LOG_SUCCESS("Window Created Succesfully");
	}

	Window::~Window() {
		closeWindow();
	}

	void Window::closeWindow() {
		if (!mOs.isOpen()) return; // already closed

		mPresent.destroy();
		// Unhooked while the window it hooks into still exists, in the reverse of the order
		// they were hooked in.
		mOs.detachFrame();
		mTextInput.reset();
		mA11yHost.reset();
		mGui.setAccessibleBridge(nullptr);
		mOs.destroy();
	}

	bool Window::windowIsUp() const {
		return mOs.isOpen() && !mOs.closeRequested();
	}

	void Window::syncOSState() {
		mOs.sync();
	}

	bool Window::recreateSwapchain() {
		// Not cleared when it cannot be rebuilt: a minimised window is coming back, and the
		// frame that finds it restored rebuilds the swapchain then.
		if (!mPresent.recreate(cachedExtent())) return false;
		mOs.clearResized();
		return true;
	}

	Window* Window::getRefFromNode(stack_element& node) {
		return node.getBody<Window, &RDA::Window::mID>();
	}

	const GuiDrawData& Window::guiDrawData() const { return mGui.drawData(); }
	uint64_t Window::guiDrawVersion() const { return mGui.drawVersion(); }
	uint32_t Window::guiBackground() const { return mGui.backgroundColor(); }
	Rect Window::guiViewportRect() const { return mGui.viewportRect(); }
}

namespace RDA {
	// After the walk: the layout's maximise button and the bare title bar, in pixels, for the
	// platform's own frame. On Windows that is what offers snap layouts over the button,
	// hands the bar to the OS's caption, and resizes natively.
	void Window::frameDone() {
		const float k = mGui.unitScale();
		const Rect button = mGui.maximizeButton();
		mOs.updateNativeFrame(Rect{ button.x * k, button.y * k, button.w * k, button.h * k },
		                      mGui.captionUnderPointer());
	}
}
