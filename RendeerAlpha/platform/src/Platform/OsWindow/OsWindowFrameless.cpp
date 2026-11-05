#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Platform/OsWindow.h>
#include <GraphicalObjects/WindowEdges.h>
#include <Platform/WindowFrame.h>
#include <algorithm>
#include <cmath>
#include <vector>

// A window with no frame of its own, moved and resized by the interface drawn in it: the
// title bar a layout draws, the edges that resize it, and what the platform's own frame is
// told about both.
namespace RDA {

	void OsWindow::followGrip(bool held) {
		// The platform has the bar: presses on it go to the OS's own caption (see
		// Platform/WindowFrame), which moves, snaps, restores and maximises by itself.
		if (mFrame && mFrame->native()) {
			// Only a press the hit test answered too early arrives here; it is still a press
			// on the bar, so it goes where the others went.
			if (held && !mGripping) mFrame->captionPressed();
			mGripping = held;
			return;
		}
		if (!mWindow || !held || isFullscreen()) {
			mGripping = false;
			mGripFromMaximized = false;
			return;
		}
		double cursorX = 0.0, cursorY = 0.0;
		glfwGetCursorPos(mWindow, &cursorX, &cursorY);
		if (!mGripping) {
			// The first frame only takes hold. Moving on it would jump the window by
			// wherever in the bar the pointer happened to land.
			mGripping = true;
			mGripX = cursorX;
			mGripY = cursorY;
			mGripFromMaximized = isMaximized();
			return;
		}


		if (isMaximized()) {
			// Taken hold of while maximised, the way every platform's own title bar
			// behaves: nothing happens until the pointer has gone somewhere, and then
			// the window restores *under the pointer* -- at the same fraction of its
			// width, so the bar stays where the hand is -- and the drag carries on.
			// Moving a maximised window instead would leave it maximised and somewhere
			// else, which is why this used to do nothing at all.
			if (!mGripFromMaximized) return;   // maximised while held, by a double click
			const double slop = 6.0 * (mCachedContentScale > 0.0f ? mCachedContentScale : 1.0f);
			if (std::abs(cursorX - mGripX) < slop && std::abs(cursorY - mGripY) < slop) return;
			int maxX = 0, maxY = 0, maxW = 0, maxH = 0;
			glfwGetWindowPos(mWindow, &maxX, &maxY);
			glfwGetWindowSize(mWindow, &maxW, &maxH);
			const double fraction = maxW > 0 ? mGripX / maxW : 0.5;
			const double screenX = maxX + cursorX, screenY = maxY + cursorY;
			setMaximized(false);
			int w = 0, h = 0;
			glfwGetWindowSize(mWindow, &w, &h);
			mGripX = fraction * w;
			// Kept at its depth into the bar, which a restored window has too.
			mGripY = std::min(mGripY, static_cast<double>(h) * 0.5);
			glfwSetWindowPos(mWindow, static_cast<int>(screenX - mGripX),
			                 static_cast<int>(screenY - mGripY));
			mGripFromMaximized = false;
			return;
		}
		int windowX = 0, windowY = 0;
		glfwGetWindowPos(mWindow, &windowX, &windowY);
		// Moving the window moves the cursor's window-relative position back to where it
		// took hold, so the offset stays the offset and this does not drift.
		glfwSetWindowPos(mWindow,
		                 windowX + static_cast<int>(cursorX - mGripX),
		                 windowY + static_cast<int>(cursorY - mGripY));
	}

	void OsWindow::showEdgeCursor(uint8_t edge) {
		if (edge == mCursorEdge) return;
		mCursorEdge = edge;
		GLFWcursor* cursor = nullptr;
		const bool horizontal = (edge & (EdgeLeft | EdgeRight)) != 0;
		const bool vertical = (edge & (EdgeTop | EdgeBottom)) != 0;
		if (horizontal && vertical) {
			// GLFW 3.3 has no diagonal resize cursor, so they are drawn: once, at a size
			// that reads at the display's scale.
			const bool nwse = ((edge & EdgeLeft) && (edge & EdgeTop)) ||
			                  ((edge & EdgeRight) && (edge & EdgeBottom));
			GLFWcursor*& slot = nwse ? mCursorNWSE : mCursorNESW;
			if (!slot) {
				const float k = mCachedContentScale > 0.0f ? mCachedContentScale : 1.0f;
				const int size = std::max(16, static_cast<int>(std::lround(20.0f * k)));
				std::vector<uint8_t> pixels = diagonalResizeCursor(size, nwse);
				GLFWimage image{ size, size, pixels.data() };
				slot = glfwCreateCursor(&image, size / 2, size / 2);
			}
			cursor = slot;
		} else if (horizontal) {
			if (!mCursorH) mCursorH = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
			cursor = mCursorH;
		} else if (vertical) {
			if (!mCursorV) mCursorV = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
			cursor = mCursorV;
		}
		// Null is the platform's arrow, which is what everywhere else in the window shows.
		glfwSetCursor(mWindow, cursor);
	}

	bool OsWindow::followEdges(bool pressed, bool down, double pressX, double pressY) {
		if (!mWindow) return false;
		// The platform has the edges: Windows hit-tests them itself, with its own cursors
		// and with snapping while they are dragged.
		if (mFrame && mFrame->native()) return false;
		const bool able = !mInfo.style.decorated && mInfo.style.resizable &&
		                  !isMaximized() && !isFullscreen() && !isMinimized();
		if (!able) {
			mResizeEdge = 0;
			if (mCursorEdge != 0 && mCursorEdge != 0xFF) showEdgeCursor(0);
			return false;
		}

		double cursorX = 0.0, cursorY = 0.0;
		glfwGetCursorPos(mWindow, &cursorX, &cursorY);
		int winX = 0, winY = 0, winW = 0, winH = 0;
		glfwGetWindowPos(mWindow, &winX, &winY);
		glfwGetWindowSize(mWindow, &winW, &winH);

		if (mResizeEdge) {
			// In screen coordinates, because the window moves under the pointer while a
			// left or top edge is dragged and anything relative to it would chase itself.
			const double dx = (winX + cursorX) - mResizeFromX;
			const double dy = (winY + cursorY) - mResizeFromY;
			const float k = mCachedContentScale > 0.0f ? mCachedContentScale : 1.0f;
			const auto bound = [k](unsigned value, int fallback) {
				return value ? static_cast<int>(std::lround(value * k)) : fallback;
			};
			// Some floor even when the application named none: a window dragged down to
			// nothing cannot be taken hold of again.
			const int minW = bound(mInfo.style.minWidth, static_cast<int>(std::lround(160 * k)));
			const int minH = bound(mInfo.style.minHeight, static_cast<int>(std::lround(100 * k)));
			const int maxW = bound(mInfo.style.maxWidth, 0);
			const int maxH = bound(mInfo.style.maxHeight, 0);
			const WindowRect start{ mResizeStartX, mResizeStartY, mResizeStartW, mResizeStartH };
			const WindowRect next = resizeFromEdge(start, mResizeEdge, dx, dy, minW, minH, maxW, maxH);
			if (next.x != winX || next.y != winY) glfwSetWindowPos(mWindow, next.x, next.y);
			if (next.w != winW || next.h != winH) glfwSetWindowSize(mWindow, next.w, next.h);
			// Let go: after the last move rather than instead of it, which arrives in the
			// same frame as the release more often than not.
			if (!down) {
				mResizeEdge = 0;
				// The pointer may have come to rest anywhere; the next frame asks again.
				showEdgeCursor(0);
			}
			return true;   // the release was the window's too
		}

		// Not while a button is already held by something in the interface: a drag that
		// wanders over the edge stays the drag it was.
		if (down && !pressed) {
			if (mCursorEdge != 0 && mCursorEdge != 0xFF) showEdgeCursor(0);
			return false;
		}

		// Five layout units, in the screen coordinates the pointer arrives in. A press is
		// judged where it happened; hovering, where the pointer is now.
		const double band = 5.0 * (mCachedContentScale > 0.0f ? mCachedContentScale : 1.0f);
		const double atX = pressed ? pressX : cursorX;
		const double atY = pressed ? pressY : cursorY;
		const uint8_t edge = windowEdgeAt(atX, atY, winW, winH, band);
		showEdgeCursor(edge);
		if (edge && pressed) {
			mResizeEdge = edge;
			mResizeStartX = winX; mResizeStartY = winY;
			mResizeStartW = winW; mResizeStartH = winH;
			mResizeFromX = winX + pressX;
			mResizeFromY = winY + pressY;
			// Already moved since the press, the window catches up now rather than on
			// the next frame.
			followEdges(false, down, pressX, pressY);
			return true;
		}
		return edge != 0;
	}

	void OsWindow::updateNativeFrame(const Rect& maximizeButton, bool captionUnderPointer) {
		if (!mFrame || !mFrame->native()) return;
		mFrame->update(maximizeButton, captionUnderPointer,
		               5.0 * (mCachedContentScale > 0.0f ? mCachedContentScale : 1.0f));
	}
}
