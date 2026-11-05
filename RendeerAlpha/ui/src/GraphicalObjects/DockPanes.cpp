#include <GraphicalObjects/Docking.h>
#include <GraphicalObjects/Gui.h>
#include "DockChrome.h"
#include <algorithm>
#include <cmath>

// The Panes arrangement: a tree of split panes with tab strips, splitters, a corner that
// cuts and joins, and floating panels over the top. update() is the frame, and it is a
// sequence of steps in the order the frame needs them -- what is being held advances
// first, then presses are decided, then everything is drawn from back to front.
namespace RDA {

	static constexpr float kSplit = 6.0f;    // splitter thickness
	static constexpr float kMinDock = 90.0f; // smallest a pane can be squeezed to
	static constexpr float kEdgeBand = 26.0f;// window border band = dock across that edge
	static constexpr float kEdgeShare = 0.25f;// how much of the window a full-edge dock takes

	// ---- drop targets -------------------------------------------------------------
	DockDrop DockSpace::windowEdgeAt(glm::vec2 p, const Rect& area, Rect* previewOut) const {
		if (!area.contains(p)) return DockDrop::None;
		const float right = area.x + area.w, bottom = area.y + area.h;
		DockDrop where = DockDrop::None;
		if      (p.x < area.x + kEdgeBand) where = DockDrop::Left;
		else if (p.x > right - kEdgeBand)  where = DockDrop::Right;
		else if (p.y < area.y + kEdgeBand) where = DockDrop::Top;
		else if (p.y > bottom - kEdgeBand) where = DockDrop::Bottom;
		if (where == DockDrop::None || !previewOut) return where;

		const float ew = area.w * kEdgeShare, eh = area.h * kEdgeShare;
		switch (where) {
		case DockDrop::Left:   *previewOut = { area.x, area.y, ew, area.h }; break;
		case DockDrop::Right:  *previewOut = { right - ew, area.y, ew, area.h }; break;
		case DockDrop::Top:    *previewOut = { area.x, area.y, area.w, eh }; break;
		default:               *previewOut = { area.x, bottom - eh, area.w, eh }; break;
		}
		return where;
	}

	// ---- painting one pane ---------------------------------------------------------

	// How far into a pane a corner drag has to go before it means anything. Below this
	// it is a press that has not decided yet, and guessing would make the corner
	// impossible to click without also splitting something.
	static constexpr float kCornerSlop = 12.0f;
	// The corner's grab square, in the bottom-right of a pane.
	static constexpr float kCornerSize = 14.0f;
	// Where its marks sit: how far in the nearest is, and how far from the edges.
	static constexpr float kCornerMarkFirst = 3.0f;
	static constexpr float kCornerMarkInset = 4.0f;

	// The grip in a pane's bottom-right corner: drag it to cut this pane in two, or over
	// a neighbour to make the two of them one.
	//
	// Three diagonal marks, the same vocabulary the floating panel's resize grip uses,
	// because it is the same idea -- a corner you take hold of. It is drawn only when
	// there is something it could do: a lone pane with a lone panel has nothing to split
	// off and nobody to join.
	void DockSpace::drawPaneCorner(Gui& gui, DockNode* leaf, const Rect& slot, bool& busy) {
		if (!leaf) return;
		const bool couldSplit = leaf->tabs.size() > 1;
		const bool couldJoin = mTree.count() > 1;
		if (!couldSplit && !couldJoin) return;

		const DockStyle& s = gui.theme().dock(kDefaultVariant);
		const GuiInput& in = gui.input();
		const Rect corner{ slot.x + slot.w - kCornerSize, slot.y + slot.h - kCornerSize,
		                   kCornerSize, kCornerSize };
		const bool held = !mCornerId.empty() && mTree.leafFor(mCornerId) == leaf;
		const bool hot = held || (!busy && corner.contains(in.pointer));
		const uint32_t colour = hot ? s.splitterHover : s.splitter;

		DockChrome::drawGripMarks(gui, { corner.x + corner.w, corner.y + corner.h },
		                          kCornerMarkFirst, kCornerMarkInset, colour);

		if (!busy && in.lPressed && corner.contains(in.pointer)) {
			if (const std::string* active = leaf->activeTab()) {
				mCornerId = *active;
				mCornerStart = in.pointer;
				busy = true;
			}
		}
	}

	DockSpace::CornerIntent DockSpace::cornerIntentAt(glm::vec2 pointer) const {
		CornerIntent out;
		if (mCornerId.empty()) return out;
		out.from = mTree.leafFor(mCornerId);
		if (!out.from) return out;

		const glm::vec2 moved = pointer - mCornerStart;
		if (std::abs(moved.x) < kCornerSlop && std::abs(moved.y) < kCornerSlop) return out;

		// Over another pane: join. This is the gesture Blender uses and it means the same
		// thing here -- the two become one, and everything in this pane moves into that
		// one as further tabs.
		DockNode* under = mTree.leafAt(pointer);
		if (under && under != out.from && !under->tabs.empty()) {
			out.into = under;
			out.preview = under->rect;
			out.valid = true;
			return out;
		}

		// Otherwise: split this pane, and the active panel goes into the new half.
		//
		// It *moves* rather than being copied, which is where this parts company with
		// Blender: there an area splits into two views of the same editor, and here a
		// panel is a thing with an identity that can only be in one place. So a pane with
		// one panel in it has nothing to split off, and the corner does nothing.
		if (out.from->tabs.size() < 2) return out;

		const Rect& r = out.from->rect;
		if (std::abs(moved.x) >= std::abs(moved.y)) {
			// A vertical boundary: the new half is on the right, where the corner is.
			const float cut = std::clamp((pointer.x - r.x) / (std::max)(1.0f, r.w), 0.15f, 0.85f);
			out.side = DockDrop::Right;
			// split() takes the newcomer's share, and the newcomer is what lies beyond
			// the cut -- so it is the remainder, not the cut.
			out.ratio = 1.0f - cut;
			out.preview = Rect{ r.x + r.w * cut, r.y, r.w * (1.0f - cut), r.h };
		} else {
			const float cut = std::clamp((pointer.y - r.y) / (std::max)(1.0f, r.h), 0.15f, 0.85f);
			out.side = DockDrop::Bottom;
			out.ratio = 1.0f - cut;
			out.preview = Rect{ r.x, r.y + r.h * cut, r.w, r.h * (1.0f - cut) };
		}
		out.valid = true;
		return out;
	}

	void DockSpace::drawDropGhost(Gui& gui, const Rect& where, const DockStyle& chrome,
	                              const char* title, bool asTab) {
		if (where.w < 4.0f || where.h < 4.0f) return;

		// The room, then the bar across the top of it. A tab-shaped ghost is all bar.
		const float barH = asTab ? where.h : (std::min)(chrome.tabHeight, where.h);
		gui.drawRect(where, chrome.dropPreview);
		gui.drawRect({ where.x, where.y, where.w, barH }, chrome.dropBandHot);

		// And the edges. Two pixels, drawn as four rectangles rather than a frame,
		// because the one thing this must not do is disagree with the boundary the drop
		// will actually create.
		constexpr float kLine = 2.0f;
		gui.drawRect({ where.x, where.y, where.w, kLine }, chrome.dropBandHot);
		gui.drawRect({ where.x, where.y + where.h - kLine, where.w, kLine }, chrome.dropBandHot);
		gui.drawRect({ where.x, where.y, kLine, where.h }, chrome.dropBandHot);
		gui.drawRect({ where.x + where.w - kLine, where.y, kLine, where.h }, chrome.dropBandHot);

		if (title && *title) {
			// Clipped to the ghost: a half-width pane is a half-width bar, and a name
			// running out of it would be the one part of this drawn somewhere the panel
			// is not going.
			gui.pushClipRect(where);
			gui.drawText(title, { where.x + chrome.tabPadding, where.y + 3.0f }, chrome.tabText);
			gui.popClipRect();
		}
	}

	float DockSpace::tabStripEnd(Gui& gui, const DockNode* leaf) const {
		float x = leaf ? leaf->rect.x : 0.0f;
		if (!leaf) return x;
		for (const std::string& id : leaf->tabs) {
			const DockContainer* t = containerFor(id);
			if (!t || !t->visible) continue;
			const DockStyle& s = gui.theme().dock(t->variant);
			x += gui.measureText(t->title.c_str()) + s.tabPadding * 2.0f;
		}
		return x;
	}

	void DockSpace::drawPane(Gui& gui, DockNode* leaf, bool& busy) {
		if (!leaf || leaf->tabs.empty()) return;

		// Where the tree put it, and where it is drawn on its way there.
		//
		// A pane keyed by the panel it holds, so when that panel is docked somewhere else
		// the entry follows it and the pane glides from its old home to its new one --
		// which is the whole of "the panel moved" rather than "one pane vanished and
		// another appeared".
		//
		// Never while something is being dragged. A splitter drag recomputes these rects
		// from where the pointer is every frame, and a panel being carried follows the
		// hand; easing either means the thing you are holding trails you.
		Rect slot = leaf->rect;
		const DockStyle& chromeMotion = gui.theme().dock(kDefaultVariant);
		const float seconds = chromeMotion.motion.seconds;
		if (seconds > 0.0f) {
			const uint32_t key = gui.motionKey(leaf->tabs.front().c_str(), kMotionPlace);
			Motion& motion = gui.motion();
			if (mResizeSplit || mDragging) {
				// Tracked exactly while something is being dragged, not eased -- and
				// tracked rather than simply skipped, because a value nobody asks for is
				// dropped. Skipped, the glide after a drop would start from nothing: the
				// entry would be created fresh at the destination and already be there.
				motion.reset(key + 0u, slot.x);
				motion.reset(key + 1u, slot.y);
				motion.reset(key + 2u, slot.w);
				motion.reset(key + 3u, slot.h);
			} else {
				slot = Rect{
					motion.value(key + 0u, slot.x, seconds, chromeMotion.motion.curve),
					motion.value(key + 1u, slot.y, seconds, chromeMotion.motion.curve),
					motion.value(key + 2u, slot.w, seconds, chromeMotion.motion.curve),
					motion.value(key + 3u, slot.h, seconds, chromeMotion.motion.curve),
				};
			}
		}

		// The pane's own chrome follows the theme's default dock style; each tab is drawn
		// with its own container's variant, so two panels sharing a pane can look
		// different from one another.
		const DockStyle& pane = gui.theme().dock(kDefaultVariant);
		float tabH = pane.tabHeight;

		// One panel in this pane, wearing a header of its own: the header is the bar.
		// With others beside it the strip wins, because a strip belongs to the pane and a
		// header belongs to a panel -- and two panels in one group cannot both dress it.
		DockContainer* only = nullptr;
		int visibleTabs = 0;
		for (const std::string& id : leaf->tabs) {
			DockContainer* t = containerFor(id);
			if (!t || !t->visible) continue;
			++visibleTabs;
			only = t;
		}
		// Nothing showing: no background, no strip, no splitter beside it. The tree has
		// already given it no room, and painting a panel over zero pixels would be the
		// one thing left saying it was here.
		if (visibleTabs == 0) return;
		DockContainer* dressed = (visibleTabs == 1 && only && only->header()) ? only : nullptr;
		if (dressed) tabH = dressed->headerHeight(pane.tabHeight);

		if (slot.w < 8.0f || slot.h < tabH) return; // squeezed to nothing

		const GuiInput& in = gui.input();
		gui.drawRect(slot, pane.pane);

		if (dressed) {
			const Rect band{ slot.x, slot.y, slot.w, tabH };
			if (DockChrome::drawHeaderBand(gui, dressed, band, busy)) {
				leaf->active = leaf->indexOf(dressed->id());
				mPressTab = dressed;
				mPressPos = in.pointer;
				busy = true;
			}
			const Rect body{ slot.x, slot.y + tabH, slot.w, slot.h - tabH };
			dressed->computedRect = slot;
			gui.pushId(dressed->id().c_str());
			gui.pushClipRect(body);
			dressed->drawContents(gui, body);
			gui.popClipRect();
			gui.popId();
			drawPaneCorner(gui, leaf, slot, busy);
			return;
		}

		gui.drawRect({ slot.x, slot.y, slot.w, tabH }, pane.tabStrip);

		float tx = slot.x;
		for (size_t i = 0; i < leaf->tabs.size(); ++i) {
			DockContainer* t = containerFor(leaf->tabs[i]);
			if (!t || !t->visible) continue;
			const DockStyle& s = gui.theme().dock(t->variant);
			const float tw = gui.measureText(t->title.c_str()) + s.tabPadding * 2.0f;
			const Rect tabR{ tx, slot.y, tw, tabH };
			const bool activeTab = (static_cast<int>(i) == leaf->active);
			// Keyed on the panel rather than on its position in the strip, so reordering
			// tabs does not make two of them trade colours on the way past each other.
			const uint32_t tabColour = gui.motion().colour(
				gui.motionKey(leaf->tabs[i].c_str(), kMotionFill),
				activeTab ? s.tabActive : s.tab, s.motion.seconds, s.motion.curve);
			if (s.radius > 0.0f) {
				gui.drawRectRounded(tabR, tabColour, s.radius);
			} else {
				gui.drawRect(tabR, tabColour);
			}
			gui.drawText(t->title.c_str(), { tabR.x + s.tabPadding, tabR.y + 3.0f }, s.tabText);
			if (!busy && in.lPressed&& tabR.contains(in.pointer)) {
				leaf->active = static_cast<int>(i);
				mPressTab = t;
				mPressPos = in.pointer;
				busy = true;
			}
			tx += tw;
		}

		const std::string* activeId = leaf->activeTab();
		DockContainer* act = activeId ? containerFor(*activeId) : nullptr;
		if (!act || !act->visible) return;

		// Close (x) for the active container, at the far right of the tab bar.
		if (act->closable) {
			const DockStyle& s = gui.theme().dock(act->variant);
			const Rect xr{ slot.x + slot.w - s.closeWidth - 2.0f, slot.y, s.closeWidth, tabH };
			const bool hot = DockChrome::drawClose(gui, *act, s, xr);
			if (!busy && in.lPressed && hot) { remove(act); busy = true; }
		}

		const Rect body{ slot.x, slot.y + tabH, slot.w, slot.h - tabH };
		act->computedRect = slot;
		gui.pushId(act->id().c_str()); // isolate this instance's widget state
		gui.pushClipRect(body);
		act->drawContents(gui, body);
		gui.popClipRect();
		gui.popId();

		drawPaneCorner(gui, leaf, slot, busy);
	}

	// ---- the frame -----------------------------------------------------------------

	void DockSpace::update(Gui& gui, const Rect& area) {
		if (arrange == DockArrange::Tiles) {
			updateTiles(gui, area);
			return;
		}

		flushPendingSpawns();
		PaneFrame frame = beginPaneFrame(area);

		// What is already being held moves first, so the rest of the frame sees the tree
		// and the panels where this frame's pointer has put them.
		advanceSplitterDrag(gui, frame);
		advanceFloatResize(gui);
		advanceTabPress(gui, frame);
		advanceCornerDrag(gui, frame);
		advanceFloatDrag(gui, frame);

		frame.busy = mDragging || mResizingFloat || mPressTab || mResizeSplit ||
		             !mCornerId.empty();
		pressFloating(gui, frame);
		frame.underFloating = pointerUnderFloating(gui);

		drawPanes(gui, frame);
		drawFloating(gui, frame);
		drawDropGuidance(gui, frame);
		drawClosing(gui);

		applyPendingRemovals(); // erase any container closed this frame
	}

	DockSpace::PaneFrame DockSpace::beginPaneFrame(const Rect& area) {
		PaneFrame frame;
		frame.area = area;

		// What the tree needs to know about widgets, which is only this: a pane holding
		// nothing that is showing takes no room, so hiding the last panel in one closes
		// the space up instead of leaving a panel-shaped hole.
		frame.showing = [this](const std::string& id) {
			const DockContainer* c = containerFor(id);
			return c && c->visible;
		};

		// Only the size, for the parts that ask how much room there is rather than where
		// it is: seeding a pane's share, and nothing else.
		const glm::vec2 viewport{ area.w, area.h };

		// Laid out once so seeding can compare pane sizes, then again with what it added.
		mTree.layout(area, kSplit, frame.showing);
		seedTree(viewport);
		mTree.layout(area, kSplit, frame.showing);
		return frame;
	}

	// --- advance a splitter drag ---
	void DockSpace::advanceSplitterDrag(Gui& gui, PaneFrame& f) {
		const GuiInput& in = gui.input();
		if (mResizeSplit) {
			mTree.setRatioFromPointer(mResizeSplit, in.pointer, kSplit, kMinDock);
			mTree.layout(f.area, kSplit, f.showing);
			if (in.lReleased) mResizeSplit = nullptr;
		}
	}

	// --- advance a floating-container resize (bottom-right grip) ---
	void DockSpace::advanceFloatResize(Gui& gui) {
		const GuiInput& in = gui.input();
		if (mResizingFloat) {
			Rect& fr = mResizingFloat->floatingRect;
			fr.w = std::max(140.0f, in.pointer.x - fr.x);
			fr.h = std::max(90.0f, in.pointer.y - fr.y);
			if (in.lReleased) mResizingFloat = nullptr;
		}
	}

	// --- a pressed tab may become a detach-drag ---
	void DockSpace::advanceTabPress(Gui& gui, PaneFrame& f) {
		const GuiInput& in = gui.input();
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		if (mPressTab) {
			if (in.lReleased) mPressTab = nullptr;
			else if (glm::length(in.pointer - mPressPos) > 6.0f) {
				DockContainer* c = mPressTab;
				mPressTab = nullptr;
				c->floatingRect = { in.pointer.x - 40.0f, in.pointer.y - chrome.tabHeight * 0.5f, 260.0f, 220.0f };
				c->dock = DockSide::Floating;
				c->fresh = false; // pulled out of a pane, not newly arrived
				mTree.remove(c->id());
				for (std::string& anchor : mSeedAnchor) if (anchor == c->id()) anchor.clear();
				structuralChange();
				mTree.layout(f.area, kSplit, f.showing);
				mDragging = c;
				mDragOffset = { 40.0f, chrome.tabHeight * 0.5f };
				mDragFrom = in.pointer;
				mDragTravelled = true;   // it moved to become a drag at all
			}
		}
	}

	// --- advance a corner drag: split this pane, or join it with a neighbour ---
	void DockSpace::advanceCornerDrag(Gui& gui, PaneFrame& f) {
		const GuiInput& in = gui.input();
		if (!mCornerId.empty()) {
			CornerIntent& corner = f.corner;
			corner = cornerIntentAt(in.pointer);
			if (in.lReleased) {
				if (corner.valid) {
					if (corner.into) {
						// Everything here moves into there. Copied first: removing a tab
						// may collapse this leaf, and both node pointers with it.
						const std::vector<std::string> moving = corner.from->tabs;
						const std::string target = corner.into->tabs.front();
						for (const std::string& id : moving) {
							mTree.remove(id);
							// Re-found every time, because the removal above may have
							// collapsed a split and moved the target's node.
							if (DockNode* into = mTree.leafFor(target)) mTree.addTab(into, id);
						}
					} else if (const std::string* activeTab = corner.from->activeTab()) {
						const std::string moving = *activeTab;
						// Whichever tab is left behind, so the pane can be found again
						// after the removal -- which may restructure the tree.
						std::string staying;
						for (const std::string& id : corner.from->tabs) {
							if (id != moving) { staying = id; break; }
						}
						mTree.remove(moving);
						if (DockNode* pane = mTree.leafFor(staying)) {
							mTree.split(pane, corner.side, moving, corner.ratio);
						}
					}
					structuralChange();
				}
				mCornerId.clear();
				mTree.layout(f.area, kSplit, f.showing);
				corner = CornerIntent{};
			}
		}
	}

	// --- advance a floating drag, working out where it would land ---
	void DockSpace::advanceFloatDrag(Gui& gui, PaneFrame& f) {
		const GuiInput& in = gui.input();
		if (mDragging) {
			mDragging->floatingRect.x = in.pointer.x - mDragOffset.x;
			mDragging->floatingRect.y = in.pointer.y - mDragOffset.y;
			// The same slop a pressed tab has before it tears off: until then a press is a
			// click, and a click on a floating panel's bar is not a request to dock it.
			if (glm::length(in.pointer - mDragFrom) > 6.0f) mDragTravelled = true;

			// The window border takes precedence: that is how you get a strip across the
			// full width or height rather than a share of one pane.
			f.previewZone = mDragTravelled ? windowEdgeAt(in.pointer, f.area, &f.previewRect)
			                               : DockDrop::None;
			f.wholeEdge = (f.previewZone != DockDrop::None);
			if (!f.wholeEdge && mDragTravelled) {
				f.previewPane = mTree.leafAt(in.pointer);
				if (f.previewPane && f.previewPane->tabs.empty()) f.previewPane = nullptr;
				if (f.previewPane) {
					f.previewZone = mTree.dropZoneAt(f.previewPane, in.pointer, &f.previewRect);
				}
			}

			if (in.lReleased) {
				DockContainer* dropped = mDragging;
				mDragging = nullptr;
				if (f.wholeEdge) {
					mTree.splitRoot(f.previewZone, dropped->id(), kEdgeShare);
					structuralChange();
				} else if (f.previewPane && f.previewZone != DockDrop::None) {
					mTree.dropOnto(f.previewPane, f.previewZone, dropped->id());
					structuralChange();
				}
				f.previewPane = nullptr;
				f.previewZone = DockDrop::None;
				mTree.layout(f.area, kSplit, f.showing);
			}
		}
	}

	// --- input: floating containers get first crack (topmost = drawn last) ---
	void DockSpace::pressFloating(Gui& gui, PaneFrame& f) {
		const GuiInput& in = gui.input();
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		if (!f.busy && in.lPressed) {
			DockContainer* pressed = nullptr;
			for (auto it = mContainers.rbegin(); it != mContainers.rend(); ++it) {
				DockContainer* c = it->get();
				if (!c->visible || mTree.contains(c->id())) continue;
				// What was drawn last frame rather than what was asked for: a panel that
				// is still arriving is not at its rect yet, and grabbing one has to agree
				// with where it looks like it is.
				const Rect r = (c->computedRect.w > 0.0f) ? c->computedRect : c->floatingRect;
				// The topmost panel under the press is the one pressed, wherever on it the
				// press landed. Anything underneath is underneath: going on down the list
				// let a press on one panel's body pick up another's title bar behind it.
				if (!r.contains(in.pointer)) continue;
				pressed = c;
				if (DockChrome::gripRect(r).contains(in.pointer)) {
					mResizingFloat = c;
					f.busy = true;
					break;
				}
				if (c->closable) {
					const Rect xr = DockChrome::closeRect(r, chrome.tabHeight);
					if (xr.contains(in.pointer)) { remove(c); f.busy = true; break; }
				}
				// A panel wearing its own header takes that press while the header draws,
				// because only then is it known whether a button on the bar took it first --
				// the same rule tiles follow. Taking it here put a close button drawn on the
				// header under the bar's drag, so pressing it carried the panel instead.
				if (c->header()) break;
				const Rect titleBar{ r.x, r.y, r.w, chrome.tabHeight };
				if (titleBar.contains(in.pointer)) {
					mDragging = c;
					mDragOffset = in.pointer - glm::vec2(r.x, r.y);
					mDragFrom = in.pointer;
					mDragTravelled = false;
					f.busy = true;
				}
				break;
			}

			// Pressed is brought to the front, the way every window anyone has used
			// behaves. Floating panels are drawn and take input in the order they are
			// held, so the front is the end. Moved after the loop rather than inside it,
			// and the press itself is unaffected: the panel it landed on was already the
			// topmost one under the pointer, so its widgets got it this frame either way.
			// The order is also the order a saved layout writes them in, so the stacking
			// survives a restart.
			if (pressed && pressed != mContainers.back().get()) {
				auto at = std::find_if(mContainers.begin(), mContainers.end(),
				                       [pressed](const std::unique_ptr<DockContainer>& p) {
				                           return p.get() == pressed;
				                       });
				std::rotate(at, at + 1, mContainers.end());
				++mRevision;   // drawn in a new order, which the GUI cache cannot see
			}
		}
	}

	// --- what is under a floating panel is under it --------------------------------
	//
	// Docked panes are painted first and floating panels over them, and a widget in an
	// immediate-mode GUI decides about a press while it paints -- so without this a
	// press on a floating panel was also a press on whatever docked widget lay beneath
	// it, which a <viewport> listening for clicks made easy to see. Not while something
	// is already held: a drag that began in a pane keeps going when it passes under a
	// floating panel, the way it keeps going past the pane's own edge.
	bool DockSpace::pointerUnderFloating(Gui& gui) const {
		const GuiInput& in = gui.input();
		if (gui.activeWidget() == 0 && !mResizeSplit && mCornerId.empty()) {
			for (const auto& cp : mContainers) {
				const DockContainer* c = cp.get();
				if (!c->visible || mTree.contains(c->id())) continue;
				const Rect r = (c->computedRect.w > 0.0f) ? c->computedRect : c->floatingRect;
				if (r.contains(in.pointer)) return true;
			}
		}
		return false;
	}

	// --- draw the docked panes, and what belongs to them ---
	void DockSpace::drawPanes(Gui& gui, PaneFrame& f) {
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		if (f.underFloating) gui.pushPointerAway();

		std::vector<DockNode*> leaves;
		mTree.collectLeaves(leaves);
		for (DockNode* leaf : leaves) drawPane(gui, leaf, f.busy);

		// Where the scene shows through: the pane seeded Center, else the biggest one.
		const int centreHint = static_cast<int>(DockSide::Center);
		DockNode* centrePane = mSeedAnchor[centreHint].empty()
			? nullptr : mTree.leafFor(mSeedAnchor[centreHint]);
		if (!centrePane) centrePane = largestPane();
		mCenterBody = (centrePane && !centrePane->tabs.empty())
			? Rect{ centrePane->rect.x, centrePane->rect.y + chrome.tabHeight,
			        centrePane->rect.w, centrePane->rect.h - chrome.tabHeight }
			: f.area;

		// --- what the corner drag would do -----------------------------------------
		//
		// The half that would be cut off, or the pane that would be swallowed. Drawn
		// before the splitters so a handle still reads on top of it.
		if (f.corner.valid) {
			gui.drawRect(f.corner.preview, f.corner.into ? chrome.dropPane : chrome.dropPreview);
			if (f.corner.into) gui.drawRect(f.corner.from->rect, chrome.dropPreview);
		}

		drawSplitters(gui, f);

		if (f.underFloating) gui.popPointerAway();
	}

	// --- splitters between panes ---
	void DockSpace::drawSplitters(Gui& gui, PaneFrame& f) {
		const GuiInput& in = gui.input();
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		std::vector<DockNode*> splits;
		mTree.collectSplits(splits);
		for (DockNode* split : splits) {
			// A split with nothing showing on one side has no boundary: its showing child
			// was given the whole area, so there is no line between them to drag and
			// drawing one would put a handle across the middle of a pane.
			if (!DockLayout::anyShowing(split->first.get(), f.showing) ||
			    !DockLayout::anyShowing(split->second.get(), f.showing)) {
				continue;
			}
			const Rect handle = split->handleRect(kSplit);
			const bool hot = (split == mResizeSplit) || (!f.busy && handle.contains(in.pointer));
			// A splitter has no id of its own, so its handle's position is its identity --
			// stable for as long as it is in the same place, which is as long as it needs
			// to be for a hover to fade.
			const uint32_t splitKey = static_cast<uint32_t>(handle.x) * 2654435761u
			                        ^ static_cast<uint32_t>(handle.y);
			gui.drawRect(handle, gui.motion().colour(splitKey ^ kMotionFill,
			                                         hot ? chrome.splitterHover : chrome.splitter,
			                                         chrome.motion.seconds, chrome.motion.curve));
			if (!f.busy && in.lPressed && handle.contains(in.pointer)) {
				mResizeSplit = split;
				f.busy = true;
			}
		}
	}

	// --- draw floating containers (creation order; last on top) ---
	void DockSpace::drawFloating(Gui& gui, PaneFrame& f) {
		for (auto& cp : mContainers) {
			DockContainer* c = cp.get();
			// Hidden, so the next time it is shown it arrives rather than being suddenly
			// there again. A docked one is drawn by its pane instead.
			if (!c->visible) { c->fresh = true; continue; }
			if (mTree.contains(c->id())) continue;
			drawFloatingPanel(gui, f, c);
		}
	}

	void DockSpace::drawFloatingPanel(Gui& gui, PaneFrame& f, DockContainer* c) {
		const GuiInput& in = gui.input();
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		const bool active = (c == mDragging);
		const DockStyle& s = gui.theme().dock(c->variant);
		const float seconds = s.motion.seconds;
		Motion& motion = gui.motion();

		// A floating panel keeps the same value a docked pane uses for its rect, so
		// the two are one continuous position. Dock it and the pane glides from where
		// the panel was let go of into the space it was given -- rather than the panel
		// disappearing here and a pane appearing over there.
		//
		// *Where* it is belongs to the hand and is tracked exactly, never eased: a
		// panel being carried that lagged the pointer would feel like rubber. How big
		// it is does not, and that is the other half of the same glide -- a panel
		// pulled out of a pane shrinks from that pane's size to a window's while you
		// carry it, which is the drop played backwards. The grip is the exception,
		// because there the hand is holding the size itself.
		Rect r = c->floatingRect;
		if (seconds > 0.0f) {
			const uint32_t key = gui.motionKey(c->id().c_str(), kMotionPlace);
			motion.reset(key + 0u, r.x);
			motion.reset(key + 1u, r.y);
			if (c == mResizingFloat) {
				motion.reset(key + 2u, r.w);
				motion.reset(key + 3u, r.h);
			} else {
				r.w = motion.value(key + 2u, r.w, seconds, s.motion.curve);
				r.h = motion.value(key + 3u, r.h, seconds, s.motion.curve);
			}
		}

		// Arriving: spawned, or restored from a saved arrangement, or shown again --
		// it fades and grows into place, which is the closing ghost's shrink played
		// the other way round.
		const float arrival = DockChrome::arrive(gui, *c, s);
		const DockChrome::Framed framed =
			DockChrome::drawPanel(gui, *c, r, arrival, active, chrome.tabHeight, f.busy);
		if (framed.grabbed) {
			mDragging = c;
			mDragOffset = in.pointer - glm::vec2(framed.shown.x, framed.shown.y);
			mDragFrom = in.pointer;
			mDragTravelled = false;
			f.busy = true;
		}
	}

	// --- drop guidance while dragging ---
	//
	// It fades in with the drag and out again after the drop, rather than blinking on
	// the frame a panel is picked up. Kept painting for the length of the fade, which
	// is why this is asked every frame and not only while dragging: the guidance has
	// to outlive the drag that asked for it.
	void DockSpace::drawDropGuidance(Gui& gui, PaneFrame& f) {
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		const Rect& area = f.area;
		const uint32_t guideKey = gui.motionKey("#dropguide", kMotionOpen) ^ mKeySalt;
		const float guide = gui.motion().value(guideKey, mDragging ? 1.0f : 0.0f,
		                                       chrome.motion.seconds, chrome.motion.curve);
		if (guide > 0.004f) {
			gui.pushOpacity(guide);
			// The four window edges, so docking across a whole side is discoverable.
			const Rect bands[4] = {
				{ area.x, area.y, kEdgeBand, area.h },
				{ area.x + area.w - kEdgeBand, area.y, kEdgeBand, area.h },
				{ area.x, area.y, area.w, kEdgeBand },
				{ area.x, area.y + area.h - kEdgeBand, area.w, kEdgeBand },
			};
			const DockDrop bandSide[4] = { DockDrop::Left, DockDrop::Right, DockDrop::Top, DockDrop::Bottom };
			for (int i = 0; i < 4; ++i) {
				const bool hot = f.wholeEdge && (bandSide[i] == f.previewZone);
				gui.drawRect(bands[i], hot ? chrome.dropBandHot : chrome.dropBand);
			}
			// The pane under the pointer, and the panel drawn where the drop would put
			// it. Only while there is still a drag to guide -- after the drop these have
			// nothing to point at, and the edges fade out on their own.
			if (mDragging) {
				if (f.previewPane) gui.drawRect(f.previewPane->rect, chrome.dropPane);
				if (f.previewZone == DockDrop::Center && f.previewPane) {
					// Joining a tab group. The rectangle here is the whole pane, and a
					// whole pane lit up says nothing that the pane tint above has not
					// already said -- what actually happens is that one more tab appears
					// at the end of the strip, so that is what is drawn.
					const DockStyle& joined = gui.theme().dock(mDragging->variant);
					const Rect tab{
						tabStripEnd(gui, f.previewPane), f.previewPane->rect.y,
						gui.measureText(mDragging->title.c_str()) + joined.tabPadding * 2.0f,
						chrome.tabHeight,
					};
					drawDropGhost(gui, tab, chrome, mDragging->title.c_str(), true);
				} else if (f.previewZone != DockDrop::None) {
					drawDropGhost(gui, f.previewRect, chrome, mDragging->title.c_str(), false);
				}
			}
			gui.popOpacity();
		}
	}

	// --- panels on their way out ---
	//
	// Drawn last, so a closing panel fades over the panes that are already gliding
	// into the space it left. Chrome only: the container it belonged to is gone, and
	// what a panel looks like while it goes is its frame and its name.
	void DockSpace::drawClosing(Gui& gui) {
		for (size_t i = 0; i < mClosing.size();) {
			ClosingPane& ghost = mClosing[i];
			const DockStyle& s = gui.theme().dock(ghost.variant);
			const float seconds = s.motion.seconds;
			if (seconds <= 0.0f) { mClosing.erase(mClosing.begin() + i); continue; }

			const uint32_t key = gui.motionKey(ghost.id.c_str(), kMotionCollapse);
			if (!ghost.started) {
				// From its beginning, not from whatever the pane that used to be here had
				// reached: this panel's place in the table is one it has just inherited.
				gui.motion().reset(key, 0.0f);
				ghost.started = true;
			}
			const float gone = gui.motion().value(key, 1.0f, seconds, s.motion.curve);
			if (gone >= 0.999f) { mClosing.erase(mClosing.begin() + i); continue; }

			// Shrinking a little as it fades, toward its own middle, so it reads as
			// leaving rather than as merely dimming.
			const float shrink = 1.0f - gone * 0.06f;
			const Rect r = DockChrome::scaledAbout(ghost.rect, shrink);
			gui.pushOpacity(1.0f - gone);
			if (s.radius > 0.0f) gui.drawRectRounded(r, s.pane, s.radius);
			else                 gui.drawRect(r, s.pane);
			gui.drawRect({ r.x, r.y, r.w, s.tabHeight }, s.tabStrip);
			gui.drawText(ghost.title.c_str(),
			             { r.x + s.tabPadding, r.y + 3.0f }, s.tabText);
			gui.popOpacity();
			++i;
		}
	}
}
