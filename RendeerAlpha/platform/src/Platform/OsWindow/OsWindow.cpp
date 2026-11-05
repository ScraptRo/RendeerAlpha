#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Platform/OsWindow.h>
#include <Platform/WindowFrame.h>
#include <GraphicalObjects/Input.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

// The window as the OS has it: opened, fed events, read back, and closed. What the OS does with
// it is OsWindowState.cpp; a window with no frame of its own is OsWindowFrameless.cpp; its icon
// is OsWindowIcon.cpp.
namespace RDA {

	namespace {
		// What GLFW's user pointer points at: the two things a callback needs, and nothing it
		// could keep alive by accident. Owned by the window, so it lives exactly as long.
		struct Callbacks {
			OsWindow* window = nullptr;
			Input*    input = nullptr;
			void    (*pump)() = nullptr;
		};
	}

	static void framebufferResizeCallback(GLFWwindow* window, int, int) {
		auto self = reinterpret_cast<Callbacks*>(glfwGetWindowUserPointer(window));
		if (self && self->window) self->window->markResized();
	}

	// GLFW hands input to these free functions with the window's user pointer; each one
	// recovers the window's Input (two pointer loads) and forwards into it. GLFW's action
	// codes line up with InputAction (RELEASE/PRESS/REPEAT = 0/1/2).
	static Input* inputOf(GLFWwindow* window) {
		auto self = reinterpret_cast<Callbacks*>(glfwGetWindowUserPointer(window));
		return self ? self->input : nullptr;
	}

	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		if (Input* in = inputOf(window)) {
			in->onKey(key, scancode, static_cast<InputAction>(action), mods);
		}
	}
	static void charCallback(GLFWwindow* window, unsigned int codepoint) {
		if (Input* in = inputOf(window)) in->onChar(codepoint);
	}
	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		if (Input* in = inputOf(window)) {
			in->onMouseButton(button, static_cast<InputAction>(action), mods);
		}
	}
	static void cursorPosCallback(GLFWwindow* window, double x, double y) {
		if (Input* in = inputOf(window)) in->onCursorPos(x, y);
	}
	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		if (Input* in = inputOf(window)) in->onScroll(xoffset, yoffset);
	}
	static void dropCallback(GLFWwindow* window, int count, const char** paths) {
		Input* in = inputOf(window);
		if (!in || count <= 0 || !paths) return;
		// Copied out of GLFW's array, which is only valid for the length of this call.
		std::vector<std::string> dropped;
		dropped.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; ++i) {
			if (paths[i]) dropped.emplace_back(paths[i]);
		}
		in->onFilesDropped(dropped);
	}

	// While the OS holds the event loop -- a window being dragged or resized -- these are what
	// still arrive, so the frame is driven off them. See Runtime/EventPump.cpp.
	static void modalCallback(GLFWwindow* window) {
		auto self = reinterpret_cast<Callbacks*>(glfwGetWindowUserPointer(window));
		if (self && self->pump) self->pump();
	}

	// The callbacks' user data, one per window, kept beside the window it belongs to.
	static Callbacks& callbacksOf(GLFWwindow* window, OsWindow* owner) {
		auto self = reinterpret_cast<Callbacks*>(glfwGetWindowUserPointer(window));
		if (!self) {
			self = new Callbacks;
			self->window = owner;
			glfwSetWindowUserPointer(window, self);
		}
		return *self;
	}

	OsWindow::OsWindow(WindowInfo& info) : mInfo(info) {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		// Everything the platform decides once, at creation. GLFW keeps these until they
		// are changed, so they are all stated rather than only the ones being turned off.
		glfwWindowHint(GLFW_DECORATED, mInfo.style.decorated ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, mInfo.style.resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, mInfo.style.maximized ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_FLOATING, mInfo.style.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, mInfo.style.transparent ? GLFW_TRUE : GLFW_FALSE);
		// Not GLFW_SCALE_TO_MONITOR. A size asked for here is in layout units and has
		// already been multiplied by the scale the *engine* settled on -- which is the
		// display's unless the application said otherwise. Letting GLFW scale it as well
		// would multiply twice, and would ignore an application that asked for physical
		// pixels by setting GuiConfig::uiScale to 1.
		// Placed after it is up rather than by a hint, so it is one code path with
		// setPosition -- and so a window that says where it goes does not flash at the
		// platform's choice first. GLFW_VISIBLE is what makes that possible.
		const bool placed = mInfo.style.x != kWindowUnplaced || mInfo.style.y != kWindowUnplaced;
		if (placed) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		// Fullscreen is a monitor rather than a hint. Opening straight onto one avoids
		// creating a swapchain at the windowed size and immediately throwing it away.
		GLFWmonitor* monitor = nullptr;
		if (mInfo.style.fullscreen) {
			monitor = glfwGetPrimaryMonitor();
			if (monitor) {
				if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
					mInfo.Width = static_cast<unsigned>(mode->width);
					mInfo.Height = static_cast<unsigned>(mode->height);
				}
			}
		}
		mWindow = glfwCreateWindow(mInfo.Width, mInfo.Height, mInfo.name.c_str(), monitor, nullptr);
		if (!mWindow) {
			RDA_RUNTIME_ERROR("Failed to create GLFW window");
		}
		if (placed) {
			setPosition(mInfo.style.x, mInfo.style.y);
			glfwShowWindow(mWindow);
		}
		// Before anything that converts between units and pixels -- the size limits on the
		// very next line do -- and before the engine asks what scale to bake a font at.
		sync();
		setSizeLimits(mInfo.style.minWidth, mInfo.style.minHeight, mInfo.style.maxWidth, mInfo.style.maxHeight);
		if (mInfo.style.opacity < 1.0f) setOpacity(mInfo.style.opacity);
		if (!mInfo.style.icon.empty()) {
			// Kept in mInfo either way: a failed icon is a warning and a default-looking
			// window, not a reason to refuse to open one.
			const std::string wanted = mInfo.style.icon;
			setIcon(wanted);
			mInfo.style.icon = wanted;
		}
		callbacksOf(mWindow, this);
		glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
	}

	OsWindow::~OsWindow() {
		destroy();
	}

	void OsWindow::attachInput(Input& input) {
		if (!mWindow) return;
		mInput = &input;
		callbacksOf(mWindow, this).input = &input;
		glfwSetKeyCallback(mWindow, keyCallback);
		glfwSetCharCallback(mWindow, charCallback);
		glfwSetMouseButtonCallback(mWindow, mouseButtonCallback);
		glfwSetCursorPosCallback(mWindow, cursorPosCallback);
		glfwSetScrollCallback(mWindow, scrollCallback);
		glfwSetDropCallback(mWindow, dropCallback);
	}

	void OsWindow::attachFrame(Input& input) {
		if (!mWindow) return;
		mFrame = WindowFrameHost::attach(mWindow, input);
	}

	void OsWindow::detachFrame() {
		mFrame.reset();
	}

	void OsWindow::setModalPump(void (*pump)()) {
		if (!mWindow) return;
		callbacksOf(mWindow, this).pump = pump;
		// Moving reports position, resizing reports size, and both report paint. All three
		// are listened for rather than the minimum that happens to work: the refresh alone
		// does carry a resize today, but only because the OS invalidates the window while it
		// grows, which is a detail of the platform rather than a promise it makes.
		glfwSetWindowPosCallback(mWindow, [](GLFWwindow* w, int, int) { modalCallback(w); });
		glfwSetWindowSizeCallback(mWindow, [](GLFWwindow* w, int, int) { modalCallback(w); });
		glfwSetWindowRefreshCallback(mWindow, modalCallback);
	}

	void OsWindow::destroy() {
		if (!mWindow) return; // already closed
		mFrame.reset();
		for (GLFWcursor** cursor : { &mCursorH, &mCursorV, &mCursorNWSE, &mCursorNESW }) {
			if (*cursor) glfwDestroyCursor(*cursor);
			*cursor = nullptr;
		}
		mCursorEdge = 0xFF;
		delete reinterpret_cast<Callbacks*>(glfwGetWindowUserPointer(mWindow));
		glfwSetWindowUserPointer(mWindow, nullptr);
		glfwDestroyWindow(mWindow);
		mWindow = nullptr;
		mShouldClose = true;
	}

	bool OsWindow::closeRequestedNow() const {
		return mWindow && glfwWindowShouldClose(mWindow);
	}

	void OsWindow::sync() {
		if (!mWindow) return;
		int width = 0, height = 0;
		glfwGetFramebufferSize(mWindow, &width, &height);
		mCachedWidth = static_cast<uint32_t>(width);
		mCachedHeight = static_cast<uint32_t>(height);
		int winW = 0, winH = 0;
		glfwGetWindowSize(mWindow, &winW, &winH);
		mCachedWinWidth = static_cast<uint32_t>(winW);
		mCachedWinHeight = static_cast<uint32_t>(winH);
		// What the display thinks a unit is worth. Read every sync rather than once, so
		// moving a window between monitors is something the engine can notice -- what it
		// does about it is the engine's business, not this class's.
		float scaleX = 1.0f, scaleY = 1.0f;
		glfwGetWindowContentScale(mWindow, &scaleX, &scaleY);
		// One number: an interface scaled differently on the two axes would need two of
		// everything, and no display anybody uses asks for that.
		mCachedContentScale = (scaleX > 0.0f && scaleY > 0.0f) ? (std::min)(scaleX, scaleY) : 1.0f;
		if (glfwWindowShouldClose(mWindow)) {
			mShouldClose = true;
		}
	}
}
