#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Viewports.h>
#include <Core/Tables.h>
#include <algorithm>
#include <cstdint>

// Gui, the frame: begin() and end(), and the test between them, canReuseRetained(), that
// decides whether last frame's retained geometry can stand in for this one's.
namespace RDA {

	bool Gui::canReuseRetained() const {
		if (!mCacheValid || mLayoutDirty) return false;

		// Any input at all could move a hover highlight, a caret or a dock, so the tree
		// has to be walked to find out.
		if (mInput.pointer != mLastPointer) return false;
		if (mInput.lDown || mInput.lPressed || mInput.lReleased) return false;
		// The other two buttons, for the same reason the first one is here: a walk is
		// the only thing that asks a widget whether the pointer is on it, so a right
		// press that does not walk is a context menu that never opens. Left out when
		// they were added, and the symptom was a menu that worked only when something
		// else had already forced a walk that frame.
		if (mInput.rDown || mInput.rPressed || mInput.rReleased) return false;
		if (mInput.mDown || mInput.mPressed || mInput.mReleased) return false;
		if (mInput.scroll != 0.0f) return false;
		if (!mInput.typed.empty() || !mInput.editKeys.empty()) return false;
		if (mInput.copy || mInput.cut || mInput.paste || mInput.selectAll || mInput.submit) return false;
		if (mInput.undo || mInput.redo) return false;
		// A shortcut is offered by the widgets the walk reaches, so a frame with a key in
		// it has to walk -- F5 on an idle window is input like any other.
		if (!mInput.chords.empty()) return false;
		// A drag draws a figure that follows the pointer and a target that lights up, and
		// a drop of files is handed to whatever the walk finds under it.
		if (mDragDrop.busy() || !mInput.droppedFiles.empty()) return false;
		// A screen reader has just started listening and has no tree yet, or asked for
		// something to be done.
		if (mA11y.wantsWalk()) return false;

		// A focused field blinks its caret, which is geometry changing on a timer.
		if (mFocus.id != 0) return false;

		// And so is a colour on its way somewhere. The cache decides by looking at input,
		// and an animation is the one thing that changes without any.
		if (mMotion.moving()) return false;

		// The layout is measured against the target, and a Viewport widget's quad samples
		// whatever texture it was given — either changing invalidates the geometry.
		if (mInput.viewport != mLastViewport) return false;
		if (mSceneTexture != mLastSceneTexture) return false;
		// A backend replaced what a <viewport> is drawing. Nothing else about the frame
		// says so -- same tree, same input -- so the drawings have a revision of their own.
		if (viewports().revision() != mLastViewportRevision) return false;
		// And the theme was swapped underneath it. Everything on screen is a different
		// colour and not one of the checks above can tell.
		if (mTheme.revision() != mLastThemeRevision) return false;
		// And rows arrived, or changed, under a <list>. Same tree, same input, different
		// contents -- and until this was here, a transcript that was only being appended
		// to reached the screen when the reader happened to move the mouse.
		if (tables().revision() != mLastTableRevision) return false;
		// And the atlas learned a character. A glyph baked on demand usually changes the
		// text's width too, which would be caught by the geometry differing -- but one
		// whose advance happens to match what the replacement box had would otherwise sit
		// there as a box until something else moved.
		if (mCanvas.font() && mCanvas.font()->revision() != mLastFontRevision) return false;
		// And a picture a backend registered was replaced. Same tree, same input, a
		// different image -- a preview updated while nobody touched the mouse would
		// otherwise sit there showing the one before it.
		if (images().revision() != mLastImageRevision) return false;
		// And a stream got a frame. Same tree, same input, a different picture -- a feed
		// that only advanced when the pointer moved would be the same bug twice.
		if (streams().revision() != mLastStreamRevision) return false;
		// And something asked to be looked at again -- an icon part-way through its loop.
		if (mAwake) return false;

		// Docks added or closed outside the walk, or queued from inside it and still
		// waiting to be applied.
		if (mDockSpace.revision() != mLastDockRevision) return false;
		if (mDockSpace.hasPendingWork()) return false;

		return true;
	}

	void Gui::begin(const GuiInput& input) {
		mInput = input;
		mWalkDestroyed = Widget::destroyedCount();
		// While an input method is composing, the keys are its: Backspace edits the word
		// being composed, Enter chooses a candidate. Whatever still reaches here is not for
		// the interface -- not the field, and not a shortcut.
		if (!mInput.preedit.empty()) {
			mInput.editKeys.clear();
			mInput.chords.clear();
		}
		// Before anything asks: a widget's paint reads where a value is *now*, so the
		// stepping has to have happened. Values nobody asked for last frame are dropped
		// here too, which is what keeps the table the size of what is on screen.
		mMotion.step(input.dt);
		// Before anything is drawn and whether or not the tree is walked: the clear happens
		// on the render path, and a value that stops being asked for stops moving.
		{
			const BackgroundStyle& ground = mTheme.background();
			mBackgroundColor = mMotion.colour(kMotionBackground, ground.color,
			                                  ground.motion.seconds, ground.motion.curve);
		}
		// Everything handed out last frame is free again. Two stores, no deallocation.
		mFrameArena.reset();
		mCanvas.beginFrame(input.viewport);
		mShortcuts.clear();
		mDragDrop.resetFrame();
		mInteraction.beginFrame(mInput);
		mFocus.beginFrame(mInput);
		mScrollConsumed = false;

		// Everything queued from last frame's callbacks lands here, before anything walks
		// the tree — the one point where restructuring is unambiguously safe. A structural
		// edit obviously invalidates the cached geometry.
		if (!mTree.empty()) mLayoutDirty = true;
		mTree.flush();

		// What a screen reader asked for since last frame. Taken before the cache decides,
		// because an action waiting is a reason to walk -- it is applied to its widget as
		// the walk reaches it, which is the only moment the widget is known to exist.
		mA11y.beginFrame();

		if (canReuseRetained()) {
			// Truncate back to where the retained walk finished last frame. The geometry
			// is already in the buffers, so this costs three size assignments and no
			// copying; the application's immediate calls then append after it as usual.
			mCanvas.reuse(mRetainedVertices, mRetainedIndices, mRetainedCommands);
			// `mInteraction.hot` and `mViewportRect` are deliberately left alone: without input they
			// still describe the situation the skipped walk would have reproduced.
			++mCacheStats.reused;
			mWalked = false;
			return;
		}
		mWalked = true;

		// Walking, so describing -- when anybody is listening.
		mA11y.beginWalk();

		// Only now: this frame walks the tree, so this is a frame on which every value
		// still in use will be asked for. Ageing on a reused frame would forget a
		// colour that is sitting there perfectly visible.
		mMotion.forget();

		// Asked for again by whatever is still playing, every frame it still is. Cleared
		// here rather than when it settles, so nothing has to remember to stop asking.
		mAwake = false;
		mChrome.beginWalk();

		mCanvas.beginWalk();
		mOverlays.clear();
		// Where things are is a fact about this frame, so it is thrown away at the top of
		// it. What is *wanted* is not, and persists.
		mAnchorRect.clear();

		mInteraction.beginWalk(mInput);
		mViewportRect = Rect{}; // a Viewport widget re-reports it during the walk below

		{
			// Guarded: any direct structural edit from inside the walk now warns instead
			// of quietly corrupting the container it happens in.
			WidgetWalkGuard guard;

			// Walk the retained tree first: it forms the base layer, and any immediate
			// calls the app makes in onUpdate then draw on top of it and win input ties.
			mRetainedRoot.paint(*this, glm::vec2(0.0f));

			// Dockable containers sit above the static tree.
			mDockSpace.update(*this, Rect{ 0.0f, 0.0f, mInput.viewport.x, mInput.viewport.y });

			// And anything that asked to draw over the lot -- an open dropdown, which has
			// to escape the panel that clips the widget it belongs to. The clip goes back
			// to the whole viewport for these, and they are painted in the order they
			// asked, so the last one to open is on top.
			//
			// Copied first: a paintAbove is allowed to ask again for next frame, and
			// growing the list being iterated would invalidate the iterator.
			const std::vector<std::pair<Widget*, glm::vec2>> above = mOverlays;
			mOverlays.clear();
			// The real pointer, for whatever is in front. Restored here rather than at the
			// end of the frame, because an overlay is exactly the thing that should have
			// it -- and restored unconditionally, since a frame where nothing claimed it
			// kept a copy of the same input anyway.
			mInteraction.restoreInput(mInput);
			for (const auto& one : above) {
				mCanvas.pushClip(glm::vec4(0.0f, 0.0f, mInput.viewport.x, mInput.viewport.y));
				one.first->paintAbove(*this, one.second);
				mCanvas.popClip();
			}
		}

		// Close the in-progress command so the retained portion is a whole number of draw
		// commands; immediate calls then start a fresh one and the split is clean.
		mCanvas.flushCmd();
		mRetainedVertices = mCanvas.drawData().vertices.size();
		mRetainedIndices = mCanvas.drawData().indices.size();
		mRetainedCommands = mCanvas.drawData().commands.size();
		mLastPointer = mInput.pointer;
		mLastViewport = mInput.viewport;
		mLastSceneTexture = mSceneTexture;
		mLastViewportRevision = viewports().revision();
		mLastThemeRevision = mTheme.revision();
		mLastTableRevision = tables().revision();
		if (mCanvas.font()) mLastFontRevision = mCanvas.font()->revision();
		mLastImageRevision = images().revision();
		mLastStreamRevision = streams().revision();
		mLastDockRevision = mDockSpace.revision();
		mLayoutDirty = false;
		mCacheValid = true;
		++mCacheStats.walked;
	}

	void Gui::end() {
		// Widgets died between the walk and here. The engine's loop walks every window,
		// then calls onUpdate, then ends each frame -- and onUpdate is where a layout is
		// reloaded and a route is swapped, which is to say where a whole tree can be
		// replaced. Everything this frame noted by pointer may now point at a widget that
		// is gone, so none of it is followed: no shortcut, no context menu, no drop and no
		// tree for a screen reader this frame. Its requests wait for the next one, which is
		// asked for now and walks the tree that is there.
		//
		// Found by a screen reader: a button pressed through UI Automation, then a hot
		// reload, and the tree it was about to be told about was built from freed widgets.
		if (Widget::destroyedCount() != mWalkDestroyed) {
			mDragDrop.resetFrame();
			mContext = nullptr;
			mShortcuts.clear();
			mA11y.forget();
			mLayoutDirty = true;
			mAwake = true;
		}

		// Hot is last-writer-wins, so once the walk is over it names whatever is on top
		// under the pointer: one of the bars only if nothing drawn over it took the pointer.
		if (mWalked) {
			mChrome.endWalk(mInteraction.hot);
		}
		// Files from outside, to the deepest element under them that asked. Called here
		// for the reason a context menu is: the handler may change the tree.
		if (mDragDrop.deliverFiles(mInput)) mAwake = true;

		if (mDragDrop.update(mInput, mInteraction, mCanvas, mTheme)) mAwake = true;

		// The ring first, and before the flush: it is geometry like everything else, and
		// added after the last command is closed it would never reach one. Drawn last
		// among the widgets, so it sits on top of whatever it surrounds.
		mFocus.drawRing(mCanvas, mTheme);

		mCanvas.flushCmd();

		// The right button's one handler, called here rather than where it was claimed:
		// the walk was in the middle of iterating the tree, and a menu handler that adds
		// or removes a widget would have pulled the container out from under it.
		if (Widget* asked = mContext) {
			mContext = nullptr;
			if (asked->onContext) asked->onContext();
			// Whatever it did, the frame is worth looking at again -- a menu opening is
			// a binding, and a binding is not input.
			mAwake = true;
		}

		// Shortcuts, here for the same reason.
		if (!mShortcuts.empty() && mShortcuts.dispatch(mInput, mFocus)) mAwake = true;
		mFocus.releasePages();

		// The tree a screen reader reads, from what this walk found. Requests nothing
		// answered -- a widget that was not drawn -- are dropped: acting on one later,
		// once it happened to be drawn again, would be acting on something nobody saw.
		mA11y.endFrame(mFocus.id, mUnitScale);

		// A release anywhere ends the interaction if no widget consumed it.
		if (mInput.lReleased) mInteraction.active = 0;
		mFocus.endFrame(mInput.lPressed);

		// What the frame drew, as a number; see Canvas::fingerprint.
		const uint64_t hash = mCanvas.fingerprint();

		mDrawChanged = (hash != mDrawVersion);
		mDrawVersion = hash;
	}
}
