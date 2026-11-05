#include "DockChrome.h"
#include <GraphicalObjects/Gui.h>

namespace RDA {
	namespace DockChrome {

		// A panel starts arriving at this share of its size and grows to all of it.
		static constexpr float kArriveFrom = 0.94f;
		static constexpr float kArriveSpan = 0.06f;
		// The close box: how far its left edge is from the panel's right edge, and how wide
		// it is. The "x" is drawn at an offset into it.
		static constexpr float kCloseInset = 20.0f;
		static constexpr float kCloseWidth = 18.0f;
		static constexpr float kCloseMarkX = 6.0f;
		static constexpr float kCloseMarkY = 3.0f;
		// The resize grip's square, in the bottom-right corner.
		static constexpr float kGripSize = 16.0f;
		// Where the grip's marks sit, and the gap between one and the next.
		static constexpr float kGripFirst = 4.0f;
		static constexpr float kGripInset = 6.0f;
		static constexpr float kGripStep = 4.0f;
		static constexpr float kGripMark = 2.0f;

		float arrive(Gui& gui, DockContainer& c, const DockStyle& s) {
			const float seconds = s.motion.seconds;
			float arrival = 1.0f;
			if (seconds > 0.0f) {
				Motion& motion = gui.motion();
				const uint32_t openKey = gui.motionKey(c.id().c_str(), kMotionOpen);
				if (c.fresh) motion.reset(openKey, 0.0f); // begins at its beginning
				arrival = motion.value(openKey, 1.0f, seconds, s.motion.curve);
			}
			c.fresh = false;
			return arrival;
		}

		Rect scaledAbout(const Rect& r, float scale) {
			return Rect{ r.x + r.w * (1.0f - scale) * 0.5f,
			             r.y + r.h * (1.0f - scale) * 0.5f,
			             r.w * scale, r.h * scale };
		}

		Rect closeRect(const Rect& panel, float barHeight) {
			return Rect{ panel.x + panel.w - kCloseInset, panel.y, kCloseWidth, barHeight };
		}

		Rect gripRect(const Rect& panel) {
			return Rect{ panel.x + panel.w - kGripSize, panel.y + panel.h - kGripSize,
			             kGripSize, kGripSize };
		}

		bool drawClose(Gui& gui, const DockContainer& c, const DockStyle& s, const Rect& box) {
			const bool hot = box.contains(gui.input().pointer);
			gui.drawText("x", { box.x + kCloseMarkX, box.y + kCloseMarkY },
			             gui.motion().colour(gui.motionKey(c.id().c_str(), kMotionMark),
			                                 hot ? s.closeHover : s.close,
			                                 s.motion.seconds, s.motion.curve));
			return hot;
		}

		void drawGripMarks(Gui& gui, glm::vec2 corner, float first, float inset,
		                   uint32_t colour) {
			for (int g = 0; g < 3; ++g) {
				const float o = first + static_cast<float>(g) * kGripStep;
				gui.drawRect({ corner.x - o - kGripMark, corner.y - inset, kGripMark, kGripMark }, colour);
				gui.drawRect({ corner.x - inset, corner.y - o - kGripMark, kGripMark, kGripMark }, colour);
			}
		}

		bool drawHeaderBand(Gui& gui, DockContainer* c, const Rect& band, bool busy) {
			DockHeader* bar = c->header();
			if (!bar) return false;

			const GuiInput& in = gui.input();
			const bool pressedInside = !busy && in.lPressed && band.contains(in.pointer);
			const uint32_t before = gui.activeWidget();

			gui.pushId(c->id().c_str());   // the same scope its contents get
			gui.pushClipRect(band);
			bar->setArranged(band);
			bar->drawHeader(gui, band);
			gui.popClipRect();
			gui.popId();

			return pressedInside && gui.activeWidget() == before;
		}

		Framed drawPanel(Gui& gui, DockContainer& c, const Rect& wanted, float arrival,
		                 bool active, float closeBarHeight, bool busy) {
			const DockStyle& s = gui.theme().dock(c.variant);
			Motion& motion = gui.motion();
			const float seconds = s.motion.seconds;

			Framed framed;
			Rect r = wanted;
			if (arriving(arrival)) {
				r = scaledAbout(r, kArriveFrom + arrival * kArriveSpan);
				gui.pushOpacity(arrival); // the contents fade with the frame around them
			}
			framed.shown = r;
			c.computedRect = r; // what the next frame's input is tested against

			if (s.radius > 0.0f) gui.drawRectRounded(r, s.pane, s.radius);
			else                 gui.drawRect(r, s.pane);

			// A floating panel or a tile is always the only panel where it is, so a header
			// of its own always wins here.
			const float barH = c.headerHeight(s.tabHeight);
			if (c.header()) {
				const Rect band{ r.x, r.y, r.w, barH };
				framed.grabbed = drawHeaderBand(gui, &c, band, busy);
			} else {
				// The same value its tab uses once it is docked, so a panel dropped into a
				// pane does not also change colour in the moment it changes shape.
				gui.drawRect({ r.x, r.y, r.w, s.tabHeight },
				             motion.colour(gui.motionKey(c.id().c_str(), kMotionFill),
				                           active ? s.titleBarActive : s.titleBar,
				                           seconds, s.motion.curve));
				gui.drawText(c.title.c_str(), { r.x + s.tabPadding - 2.0f, r.y + 3.0f }, s.tabText);
				if (c.closable) drawClose(gui, c, s, closeRect(r, closeBarHeight));
			}

			const Rect body{ r.x, r.y + barH, r.w, r.h - barH };
			gui.pushId(c.id().c_str()); // isolate this instance's widget state
			gui.pushClipRect(body);
			c.drawContents(gui, body);
			gui.popClipRect();
			gui.popId();

			// Resize grip: a small stair of marks in the bottom-right corner.
			drawGripMarks(gui, { r.x + r.w, r.y + r.h }, kGripFirst, kGripInset, s.grip);

			if (arriving(arrival)) gui.popOpacity();
			return framed;
		}
	}
}
