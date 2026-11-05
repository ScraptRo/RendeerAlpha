#include <GraphicalObjects/Widgets/Media/Viewport.h>
#include <GraphicalObjects/Viewports.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

	namespace {
		// Replays a backend's drawing inside the viewport.
		//
		// Coordinates are the viewport's own, so a drawing never has to know where on
		// screen it ended up -- the same list is correct in a docked pane, a dialog, or
		// full width. The clip is what makes that safe: a drawing that overruns is
		// cropped at the edge rather than loose in somebody else's interface.
		void paintCommands(Gui& gui, const Rect& area, const std::vector<DrawCommand>& commands) {
			gui.pushClipRect(area);
			for (const DrawCommand& c : commands) {
				switch (c.op) {
				case DrawOp::Clear:
					gui.drawRect(area, c.color);
					break;
				case DrawOp::Rect:
					gui.drawRectRounded({ area.x + c.a, area.y + c.b, c.c, c.d }, c.color, c.e);
					break;
				case DrawOp::Line:
					gui.drawLine({ area.x + c.a, area.y + c.b }, { area.x + c.c, area.y + c.d },
					             c.color, c.e > 0.0f ? c.e : 1.0f);
					break;
				case DrawOp::Text: {
					TextStyle style;
					style.size = c.e; // 0 means the interface's own size, which is the default
					gui.drawText(c.text.c_str(), { area.x + c.a, area.y + c.b }, c.color, style);
					break;
				}
				}
			}
			gui.popClipRect();
		}
	}

	void Viewport::trackPointer(Gui& gui, const Rect& area) {
		const GuiInput& in = gui.input();

		// Only what can be seen takes the pointer. Inside a scroll view the rest of the
		// viewport is clipped away, and a press on the bar or a sibling drawn over that
		// part must not land here.
		const Rect clip = gui.currentClipRect();
		const float x0 = (std::max)(area.x, clip.x), y0 = (std::max)(area.y, clip.y);
		const float x1 = (std::min)(area.x + area.w, clip.x + clip.w);
		const float y1 = (std::min)(area.y + area.h, clip.y + clip.h);
		const Rect visible{ x0, y0, (std::max)(0.0f, x1 - x0), (std::max)(0.0f, y1 - y0) };
		const Gui::PointerArea hit = gui.pointerArea(mId.c_str(), visible);

		auto bits = [](bool left, bool right, bool middle) -> uint32_t {
			return (left ? 1u : 0u) | (right ? 2u : 0u) | (middle ? 4u : 0u);
		};
		const uint32_t down = bits(in.lDown, in.rDown, in.mDown);

		ViewportEvent event;
		event.hasPointer = true;
		event.x = in.pointer.x - area.x;
		event.y = in.pointer.y - area.y;
		event.hasSize = true;
		event.width = area.w;
		event.height = area.h;
		event.inside = hit.hot;
		event.mods = (in.shift ? 1u : 0u) | (in.ctrl ? 2u : 0u) | (in.alt ? 4u : 0u);
		if (hit.hot) {
			event.pressed = bits(in.lPressed, in.rPressed, in.mPressed);
			if (event.pressed) event.clicks = in.clickCount;
			// The wheel is the viewport's while somebody listens, ahead of a scroll view
			// around it that would otherwise take it first -- a graph that zooms must not
			// also scroll the page it is on.
			if (in.scroll != 0.0f && !gui.scrollConsumed()) {
				event.wheel = in.scroll;
				gui.consumeScroll();
			}
		}

		// A button belongs to this viewport from the press over it until it comes up,
		// wherever the pointer is by then. A press and its release can land in one frame;
		// that is a button that is tracked and is not down, which is a release.
		const uint32_t tracked = mButtons | event.pressed;
		event.released = tracked & ~down;
		if (mButtons) {
			event.dx = in.pointer.x - mLastPointer.x;
			event.dy = in.pointer.y - mLastPointer.y;
		}
		mButtons = tracked & down;
		event.held = mButtons;

		const bool moved = in.pointer != mLastPointer;
		const bool worthSaying = event.pressed || event.released || event.wheel != 0.0f ||
		                         event.dx != 0.0f || event.dy != 0.0f ||
		                         event.inside != mWasInside || (event.inside && moved);
		mLastPointer = in.pointer;
		mWasInside = event.inside;
		if (worthSaying) viewports().events().report(name, event);
	}

	void Viewport::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect).
		const bool legacyFill = !mArrangedValid && rect.w <= 0.0f && rect.h <= 0.0f &&
		                        anchor == Anchor::TopLeft;
		Rect area = legacyFill ? gui.currentClipRect() : placement(gui, origin);

		Viewports& all = viewports();
		all.reportPlacement(name, area); // the size a backend lays its drawing out to
		if (all.listening(name)) trackPointer(gui, area);

		if (const std::vector<DrawCommand>* commands = all.commands(name)) {
			paintCommands(gui, area, *commands);
			return;
		}

		// Not a 2D one, so it is the GPU's -- if this is the viewport that got it. The
		// rect goes back to the engine, which sizes the offscreen target to match and
		// leaves this area out of the composited GUI so the target shows through.
		if (all.gpuViewport() == name) {
			gui.setViewportRect(area);
			const Texture* scene = gui.sceneTexture();
			if (scene) { gui.image(area, scene); return; }
			// Fullscreen mode: the scene goes straight to the window and there is no
			// texture to show. A C++ callback needs ViewportMode::Widget for the same
			// reason -- without an offscreen target there is nothing to record into.
			gui.drawRect(area, rgba(18, 20, 26));
			return;
		}

		// A second viewport, with nothing of its own to draw.
		gui.drawRect(area, rgba(18, 20, 26));
	}
}
