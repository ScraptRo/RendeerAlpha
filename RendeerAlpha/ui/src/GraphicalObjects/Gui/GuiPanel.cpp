#include <GraphicalObjects/Gui.h>

// Gui, panels: a clipped, id-scoped frame for other widgets to draw into.
namespace RDA {

	void Gui::beginPanel(const char* id, const Rect& rect, Variant variant) {
		const PanelStyle& s = mTheme.panel(variant);
		const uint32_t wid = scopedId(id);
		mInteraction.pushScope(wid);
		mCanvas.pushClip(glm::vec4(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h));
		// A panel has no hover state, so these move only when its variant does -- which
		// is the case they exist for, since a variant may be a binding.
		const uint32_t body = mMotion.colour(wid ^ kMotionFill, s.body,
		                                     s.motion.seconds, s.motion.curve);
		const float radius = mMotion.value(wid ^ kMotionRadius, s.radius,
		                                   s.motion.seconds, s.motion.curve);
		mCanvas.addFrame(rect, body, s.border, s.borderWidth, radius);              // body
		if (s.accentHeight > 0.0f) {
			mCanvas.addRect({ rect.x, rect.y, rect.w, s.accentHeight }, s.accent);  // accent strip
		}
	}

	void Gui::endPanel() {
		mCanvas.popClip();
		mInteraction.popScope();
	}
}
