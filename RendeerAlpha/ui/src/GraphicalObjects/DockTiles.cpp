#include <GraphicalObjects/Docking.h>
#include <GraphicalObjects/Gui.h>
#include "DockChrome.h"

// The Tiles arrangement: a column grid of rectangles that push each other out of the way.
// It shares the container list and the theme with Panes and nothing else, which is why it
// has a frame of its own rather than a branch inside the other one.
namespace RDA {

	// ---- the frame -----------------------------------------------------------------
	// One tile: the same chrome a floating panel wears, because it is the same thing --
	// a rectangle with a bar you pick it up by. What differs is where the rectangle comes
	// from, and that is decided before this is called.
	void DockSpace::drawTile(Gui& gui, DockContainer* c, const Rect& r, bool active,
	                         float arrive) {
		const DockStyle& s = gui.theme().dock(c->variant);
		const GuiInput& in = gui.input();

		// A tile is always the only panel where it is, so a header of its own always
		// wins here -- the same rule a floating panel follows, for the same reason.
		const DockChrome::Framed framed =
			DockChrome::drawPanel(gui, *c, r, arrive, active, s.tabHeight, mTileDrag || mTileResize);
		if (framed.grabbed) {
			mTileDrag = c;
			mTileGrab = in.pointer - glm::vec2(framed.shown.x, framed.shown.y);
			mTileGhost = framed.shown;
		}
	}

	void DockSpace::updateTiles(Gui& gui, const Rect& area) {
		flushPendingSpawns();
		placeTiles();
		advanceTileDrag(gui, area);
		advanceTileResize(gui, area);
		pressTiles(gui, area);
		drawTileLanding(gui, area);
		drawTiles(gui, area);

		// Nothing is the centre in a grid: no tile is the one the rest are arranged
		// around, which is exactly what makes it a grid.
		mCenterBody = area;
		applyPendingRemovals();
	}

	// Every panel that is showing has a place. A panel that said where it goes gets
	// that; one that did not gets the first hole. Placing happens once -- after that
	// the grid owns it, so a panel somebody dragged is not pulled back next frame.
	void DockSpace::placeTiles() {
		for (auto& cp : mContainers) {
			DockContainer* c = cp.get();
			if (!c->visible) { c->fresh = true; continue; }
			if (mGrid.find(c->id())) continue;
			if (c->tileCol >= 0 && c->tileRow >= 0) {
				mGrid.placeAt(c->id(), c->tileCol, c->tileRow, c->tileCols, c->tileRows);
			} else {
				mGrid.place(c->id(), c->tileCols, c->tileRows);
			}
			structuralChange();
		}
	}

	// --- advance a drag --------------------------------------------------------
	//
	// Two positions at once, which is the whole feel of this arrangement: the tile
	// follows the pointer exactly, and the *grid* follows in whole cells behind it,
	// pushing its neighbours as it goes. What you carry is smooth; what it does to
	// the others snaps.
	void DockSpace::advanceTileDrag(Gui& gui, const Rect& area) {
		const GuiInput& in = gui.input();
		if (mTileDrag) {
			Tile* tile = mGrid.find(mTileDrag->id());
			if (!tile) {
				mTileDrag = nullptr;
			} else {
				const Rect landing = mGrid.rectFor(*tile, area);
				mTileGhost = Rect{ in.pointer.x - mTileGrab.x, in.pointer.y - mTileGrab.y,
				                   landing.w, landing.h };
				int col = 0, row = 0;
				mGrid.cellAt(area, { mTileGhost.x, mTileGhost.y }, col, row);
				mGrid.moveTo(mTileDrag->id(), col, row, tile->cols, tile->rows,
				             mTileDrag->id());
				if (in.lReleased) {
					// Let go: the tile stops being the one thing that may not move, so
					// it settles up into the grid with everything else.
					Tile* dropped = mGrid.find(mTileDrag->id());
					if (dropped) {
						mGrid.moveTo(mTileDrag->id(), dropped->col, dropped->row,
						             dropped->cols, dropped->rows);
					}
					mTileDrag = nullptr;
					structuralChange();
				}
			}
		}
	}

	// --- advance a resize ------------------------------------------------------
	void DockSpace::advanceTileResize(Gui& gui, const Rect& area) {
		const GuiInput& in = gui.input();
		if (mTileResize) {
			Tile* tile = mGrid.find(mTileResize->id());
			if (!tile) {
				mTileResize = nullptr;
			} else {
				// The far corner the pointer is on, turned into a cell, is the far cell.
				int col = 0, row = 0;
				mGrid.cellAt(area, in.pointer, col, row);
				mGrid.moveTo(mTileResize->id(), tile->col, tile->row,
				             col - tile->col, row - tile->row, mTileResize->id());
				if (in.lReleased) {
					Tile* sized = mGrid.find(mTileResize->id());
					if (sized) {
						mGrid.moveTo(mTileResize->id(), sized->col, sized->row,
						             sized->cols, sized->rows);
					}
					mTileResize = nullptr;
					structuralChange();
				}
			}
		}
	}

	// --- input: the topmost tile under the pointer gets it ----------------------
	void DockSpace::pressTiles(Gui& gui, const Rect& area) {
		const GuiInput& in = gui.input();
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		const bool busy = mTileDrag || mTileResize;
		if (!busy && in.lPressed) {
			for (auto it = mContainers.rbegin(); it != mContainers.rend(); ++it) {
				DockContainer* c = it->get();
				if (!c->visible) continue;
				const Tile* tile = mGrid.find(c->id());
				if (!tile) continue;
				const Rect r = (c->computedRect.w > 0.0f) ? c->computedRect
				                                          : mGrid.rectFor(*tile, area);
				if (!r.contains(in.pointer)) continue;

				if (DockChrome::gripRect(r).contains(in.pointer)) { mTileResize = c; break; }
				if (c->closable && !c->header()) {
					const Rect xr = DockChrome::closeRect(r, chrome.tabHeight);
					if (xr.contains(in.pointer)) { remove(c); break; }
				}
				// A panel wearing its own header takes that press while the header draws,
				// because only then is it known whether a button on the bar took it
				// first. This is the engine's own bar.
				if (c->header()) break;
				const Rect bar{ r.x, r.y, r.w, chrome.tabHeight };
				if (bar.contains(in.pointer)) {
					mTileDrag = c;
					mTileGrab = in.pointer - glm::vec2(r.x, r.y);
					mTileGhost = r;
					break;
				}
				// A press in the body belongs to whatever is drawn there, so nothing is
				// claimed and the walk below hands it on.
				break;
			}
		}
	}

	// --- where a dragged tile will land ----------------------------------------
	//
	// Drawn under everything, so a tile carried over it covers it rather than the
	// other way round. It is the one piece of guidance this arrangement needs: there
	// is no edge to dock against and no pane to share, only a rectangle of cells.
	void DockSpace::drawTileLanding(Gui& gui, const Rect& area) {
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		if (mTileDrag) {
			if (const Tile* tile = mGrid.find(mTileDrag->id())) {
				const Rect landing = mGrid.rectFor(*tile, area);
				if (chrome.radius > 0.0f) {
					gui.drawRectRounded(landing, chrome.dropPreview, chrome.radius);
				} else {
					gui.drawRect(landing, chrome.dropPreview);
				}
			}
		}
	}

	// --- draw ------------------------------------------------------------------
	void DockSpace::drawTiles(Gui& gui, const Rect& area) {
		for (auto& cp : mContainers) {
			DockContainer* c = cp.get();
			if (!c->visible) continue;
			const Tile* tile = mGrid.find(c->id());
			if (!tile) continue;
			if (c == mTileDrag) continue;   // carried, so drawn last and on top

			const DockStyle& s = gui.theme().dock(c->variant);
			const float seconds = s.motion.seconds;
			Motion& motion = gui.motion();

			// A tile eases to where the grid put it. This is what makes a push read as
			// the others getting out of the way rather than as the grid flickering into
			// a new shape: the cells snap, and the rectangles travel.
			Rect r = mGrid.rectFor(*tile, area);
			if (seconds > 0.0f) {
				const uint32_t key = gui.motionKey(c->id().c_str(), kMotionPlace);
				r.x = motion.value(key + 0u, r.x, seconds, s.motion.curve);
				r.y = motion.value(key + 1u, r.y, seconds, s.motion.curve);
				if (c == mTileResize) {
					motion.reset(key + 2u, r.w);
					motion.reset(key + 3u, r.h);
				} else {
					r.w = motion.value(key + 2u, r.w, seconds, s.motion.curve);
					r.h = motion.value(key + 3u, r.h, seconds, s.motion.curve);
				}
			}

			const float arrive = DockChrome::arrive(gui, *c, s);
			drawTile(gui, c, r, false, arrive);
		}

		// The one being carried, over everything else, exactly where the hand is.
		if (mTileDrag) {
			mTileDrag->fresh = false;
			const uint32_t key = gui.motionKey(mTileDrag->id().c_str(), kMotionPlace);
			gui.motion().reset(key + 0u, mTileGhost.x);
			gui.motion().reset(key + 1u, mTileGhost.y);
			gui.motion().reset(key + 2u, mTileGhost.w);
			gui.motion().reset(key + 3u, mTileGhost.h);
			drawTile(gui, mTileDrag, mTileGhost, true, 1.0f);
		}
	}
}
