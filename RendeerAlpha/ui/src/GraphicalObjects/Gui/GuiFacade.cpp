#include <GraphicalObjects/Gui.h>

// Gui's public face. Gui is made of parts -- a canvas it draws on and the classes beside it -- and
// the calls the application and the widgets make on Gui that are really one part's business are
// forwarded from here, grouped by the part they go to. Nothing in this file decides anything.
namespace RDA {

	// ---- the canvas ---------------------------------------------------------------
	bool Gui::drawMesh(const WidgetMesh& mesh, const Rect& rect, int from, int into, float t,
	                   const uint32_t* colours, size_t colourCount, uint32_t base) {
		return mCanvas.drawMesh(mesh, rect, from, into, t, colours, colourCount, base);
	}
	void Gui::drawRect(const Rect& rect, uint32_t color) {
		mCanvas.drawRect(rect, color);
	}
	void Gui::drawRectRounded(const Rect& rect, uint32_t color, float radius) {
		mCanvas.drawRectRounded(rect, color, radius);
	}
	void Gui::drawText(const char* text, glm::vec2 topLeft, uint32_t color, TextStyle font) {
		mCanvas.drawText(text, topLeft, color, font);
	}
	void Gui::image(const Rect& r, const Texture* texture, uint32_t tint) {
		mCanvas.image(r, texture, tint);
	}
	void Gui::drawLine(glm::vec2 from, glm::vec2 to, uint32_t color, float width) {
		mCanvas.drawLine(from, to, color, width);
	}
	void Gui::pushOpacity(float alpha) {
		mCanvas.pushOpacity(alpha);
	}
	void Gui::popOpacity() {
		mCanvas.popOpacity();
	}
	void Gui::pushClipRect(const Rect& rect) {
		mCanvas.pushClipRect(rect);
	}
	void Gui::popClipRect() {
		mCanvas.popClipRect();
	}
	Rect Gui::currentClipRect() const {
		return mCanvas.currentClipRect();
	}
	float Gui::measureText(const char* text, TextStyle font) const {
		return mCanvas.measureText(text, font);
	}
	float Gui::lineHeight(TextStyle font) const {
		return mCanvas.lineHeight(font);
	}
	float Gui::baseline(TextStyle font) const {
		return mCanvas.baseline(font);
	}
	std::vector<std::pair<size_t, size_t>> Gui::wrapText(const std::string& text, float width,
	                                                     TextStyle font) const {
		return mCanvas.wrapText(text, width, font);
	}

	// ---- who hears the pointer ----------------------------------------------------
	void Gui::pushId(const char* id) {
		mInteraction.pushId(id);
	}
	void Gui::popId() {
		mInteraction.popId();
	}
	void Gui::pushInert() {
		mInteraction.pushInert(mInput);
	}
	void Gui::popInert() {
		mInteraction.popInert(mInput);
	}
	void Gui::pushPointerAway() {
		mInteraction.pushPointerAway(mInput);
	}
	void Gui::popPointerAway() {
		mInteraction.popPointerAway(mInput);
	}
	Gui::PointerArea Gui::pointerArea(const char* id, const Rect& rect) {
		return mInteraction.pointerArea(id, rect, mInput);
	}

	// ---- the keyboard focus -------------------------------------------------------
	bool Gui::focusable(uint32_t wid, const Rect& rect, Variant ringVariant) {
		return mFocus.focusable(wid, rect, ringVariant);
	}
	bool Gui::focusActivated(uint32_t wid) const {
		return mFocus.activated(wid, mInput);
	}
	int Gui::focusStep(uint32_t wid) const {
		return mFocus.step(wid, mInput);
	}
	bool Gui::focusEscaped(uint32_t wid) const {
		return mFocus.escaped(wid, mInput);
	}

	// ---- shortcuts ----------------------------------------------------------------
	void Gui::offerShortcut(Widget* widget) {
		mShortcuts.offer(widget, mInput);
	}
	void Gui::offerShortcutsIn(Widget& subtree) {
		mShortcuts.offerIn(subtree, mInput);
	}

	// ---- dragging and dropping ----------------------------------------------------
	void Gui::notePickUp(Widget* widget, const Rect& where) {
		mDragDrop.notePickUp(widget, where);
	}
	void Gui::offerDropTarget(Widget* widget, const Rect& where) {
		mDragDrop.offerTarget(widget, where, mInput, mCanvas.currentClipRect());
	}

	// ---- window chrome ------------------------------------------------------------
	bool Gui::windowGrip(const char* id, const Rect& rect) {
		const bool held = mChrome.grip(mInteraction.scopedId(id), rect, mInput, mInteraction);
		if (held) {
			// The window is about to move under a pointer that has not itself moved, so
			// nothing else would ask for the next frame.
			mAwake = true;
		}
		return held;
	}
}
