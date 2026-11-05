#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cstdint>

// Gui::button. It is the one immediate widget whose shape can be an asset, which is most of
// what is in it.
namespace RDA {

	bool Gui::button(const char* id, const char* text, const Rect& rect, Variant variant,
	                 TextAlign align, float alignNudge, bool selected) {
		const ButtonStyle& s = mTheme.button(variant);
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mInteraction.hot = wid;
		if (mInteraction.hot == wid && mInput.lPressed) { mInteraction.active = wid; mInteraction.clickPress = wid; }

		bool clicked = false;
		if (mInteraction.active == wid && mInput.lReleased) {
			if (mInteraction.hot == wid) clicked = true;
			mInteraction.active = 0;
		}
		// Clicking one points the keyboard at it, the way clicking a field does.
		if (mInteraction.hot == wid && mInput.lPressed) setFocus(wid);
		const bool hasFocus = focusable(wid, rect);
		if (hasFocus && focusActivated(wid)) clicked = true;

		uint32_t color = s.normal;
		// Chosen, and staying chosen. Ahead of hover on purpose: a tab that went back to
		// looking unchosen because the pointer arrived on it would be saying the wrong
		// thing about the one place it matters most.
		if (selected)            color = s.pressed;
		else if (mInteraction.active == wid) color = s.pressed; // pressed
		else if (mInteraction.hot == wid)    color = s.hovered; // hovered
		// Pressed-looking while the key that presses it is what activated it. There is
		// no held state to read for a key, so this is the frame it happens on.
		if (hasFocus && focusActivated(wid)) color = s.pressed;
		// On its way there rather than already there, when the theme asks for it. The
		// three states above are unchanged: what a button *should* look like is still
		// decided by what is happening to it, and only how it gets there is new.
		color = mMotion.colour(wid ^ kMotionFill, color, s.motion.seconds, s.motion.curve);
		// And the corners, for the same reason and over the same time: a button whose
		// variant changed is a different shape as well as a different colour, and only
		// one of the two used to travel.
		const float radius = mMotion.value(wid ^ kMotionRadius, s.radius,
		                                   s.motion.seconds, s.motion.curve);

		// A mesh, if the variant names one. It is the whole of the button's surface --
		// the frame, the corners and the fill -- so the built-in drawing below is what
		// happens when there is no mesh, or when the file behind it would not load.
		bool drawn = false;
		if (!s.mesh.empty()) {
			if (const WidgetMesh* mesh = widgetMeshes().find(s.mesh)) {
				// Names to indices, once. After this the per-frame path is an array
				// lookup, which is the whole point of declaring parameters rather than
				// reading them out of a map every time something is drawn.
				//
				// Keyed on the mesh so a theme swap that points a variant at a different
				// one resolves again rather than keeping the old answer.
				if (s.meshResolvedFor != mesh) {
					s.meshColours.assign(mesh->params().size(), 0u);
					for (size_t i = 0; i < mesh->params().size(); ++i) {
						s.meshColours[i] = mesh->params()[i].colour;   // what it declared
					}
					for (const auto& given : s.meshParams) {
						// "tab.seam" -- the mesh knows itself as "seam".
						const size_t dot = given.first.find('.');
						const std::string bare = dot == std::string::npos
							? given.first : given.first.substr(dot + 1);
						const int at = mesh->paramIndex(bare);
						if (at >= 0) s.meshColours[static_cast<size_t>(at)] = given.second;
					}
					s.meshResolvedFor = mesh;
				}
				// Which state it is in, as a number, so that moving between them is the
				// same eased value every other property here already uses.
				//
				// The states are a ladder, ordered from least engaged to most: at rest,
				// under the pointer, held. Easing through the middle one on the way to
				// the last is not a compromise -- it is what a press looks like.
				//
				// `selected` is the top of that ladder. A mesh with three states
				// therefore draws a chosen widget the way it draws a held one, which is
				// usually right and is what a tab wants; a mesh that wants them to differ
				// writes a fourth state and a press travels through `held` to reach it.
				const int last = static_cast<int>(mesh->stateNames().size()) - 1;
				float target = 0.0f;
				if (selected)         target = static_cast<float>(last);
				else if (mInteraction.active == wid)   target = 2.0f;
				else if (mInteraction.hot == wid) target = 1.0f;
				const float where = std::clamp(
					mMotion.value(wid ^ kMotionShape, target, s.motion.seconds, s.motion.curve),
					0.0f, static_cast<float>(last));
				const int from = static_cast<int>(where);
				const int into = (std::min)(from + 1, last);
				const float blend = where - static_cast<float>(from);

				// The ring, in the shape of the thing it is ringing.
				//
				// end() draws a rectangle around whatever holds the keyboard, which is the
				// right answer for every widget whose shape is one. A mesh-drawn widget is
				// a tab with a flare on it, or a shape with a bite out of one corner, and a
				// box around that announces a rectangle that is not there.
				//
				// So the same mesh is drawn again underneath, grown by the ring's width,
				// with every one of its colours replaced by the ring's. What shows is the
				// part that sticks out: an outline that follows the silhouette exactly,
				// because it *is* the silhouette. One extra resolve, for one widget, on the
				// frames where a ring is showing at all.
				if (mFocus.id == wid) {
					const FocusStyle& ring = mTheme.focus();
					const bool show = ring.width > 0.0f
						&& (ring.show == FocusShow::Always || mFocus.byKeyboard);
					if (show) {
						const float grow = ring.width - ring.inset;
						const Rect around{ rect.x - grow, rect.y - grow,
						                   rect.w + grow * 2.0f, rect.h + grow * 2.0f };
						// Every parameter the mesh has, answered with the ring's colour, so
						// nothing of the widget's own palette leaks into its outline.
						const size_t count = mesh->params().size();
						uint32_t* ringColours = count > 0
							? mFrameArena.allocate<uint32_t>(count) : nullptr;
						for (size_t i = 0; i < count; ++i) ringColours[i] = ring.color;
						mCanvas.drawMesh(*mesh, around, from, into, blend, ringColours, count, ring.color);
						mFocus.shapedRing = wid;
					}
				}

				drawn = mCanvas.drawMesh(*mesh, rect, from, into, blend,
				                 s.meshColours.data(), s.meshColours.size(), color);
			}
		}
		if (!drawn) mCanvas.addFrame(rect, color, s.border, s.borderWidth, radius);

		if (mCanvas.font()) {
			// Inset by the same padding a button measures itself with, so aligned text
			// sits where the edge of a centred button's text would be rather than
			// against the border.
			const float pad = (align == TextAlign::Center) ? 0.0f : kButtonPadX;
			const float room = rect.w - pad * 2.0f;
			const float tx = rect.x + pad + alignOffset(align, room, mCanvas.measureText(text, s.font), alignNudge);
			float baselineY = rect.y + rect.h * 0.5f + mCanvas.baseline(s.font) * 0.35f;
			mCanvas.addText(tx, baselineY, text, s.text, s.font);
		}
		return clicked;
	}
}
