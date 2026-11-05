#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <Core/Route.h>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <algorithm>

namespace RDA {

	namespace {
		// Loop-thread only, like the tree it counts.
		uint64_t gWidgetsDestroyed = 0;
	}

	Widget::~Widget() { ++gWidgetsDestroyed; }
	uint64_t Widget::destroyedCount() { return gWidgetsDestroyed; }

	Rect Widget::resolveRect(glm::vec2 origin, const Rect& parent, glm::vec2 content) const {
		const bool left = hasAnchor(anchor, Anchor::Left);
		const bool right = hasAnchor(anchor, Anchor::Right);
		const bool top = hasAnchor(anchor, Anchor::Top);
		const bool bottom = hasAnchor(anchor, Anchor::Bottom);

		const float roomW = (std::max)(0.0f, parent.w - rect.x);
		const float roomH = (std::max)(0.0f, parent.h - rect.y);
		const float sizeW = freeExtent(width, rect.w, roomW, content.x);
		const float sizeH = freeExtent(height, rect.h, roomH, content.y);

		// Start from the classic behaviour — a fixed rect at a fixed offset — and let the
		// anchors override each axis. With Anchor::TopLeft this is exactly what the
		// widget layer did before anchoring existed.
		Rect out{ origin.x + rect.x, origin.y + rect.y, sizeW, sizeH };

		if (parent.w > 0.0f) {
			if (left && right) {
				// Both edges: the width is whatever is left between the two margins.
				out.w = (std::max)(0.0f, parent.w - rect.x - marginRight);
			} else if (right) {
				// Right only: keep the size, follow the right edge.
				out.x = origin.x + parent.w - marginRight - sizeW;
			}
		}
		if (parent.h > 0.0f) {
			if (top && bottom) {
				out.h = (std::max)(0.0f, parent.h - rect.y - marginBottom);
			} else if (bottom) {
				out.y = origin.y + parent.h - marginBottom - sizeH;
			}
		}

		// A widget never given a size on an axis would come out zero-sized here, and
		// simply not appear — which is never what was meant. Fall back to the space the
		// parent has left, so a child placed straight into a container fills it rather
		// than vanishing, and any layout it runs for its own children has a real extent
		// to divide up. An explicit size, or a pair of anchored edges, is left alone.
		if (!(left && right) && sizeW <= 0.0f && parent.w > 0.0f) out.w = roomW;
		if (!(top && bottom) && sizeH <= 0.0f && parent.h > 0.0f) out.h = roomH;
		return out;
	}

	float Widget::motionSeconds() const {
		for (const Widget* at = this; at; at = at->mParent) {
			// The barrier is about seeing *past* a widget, not about the widget itself: a
			// list still slides when its container says everything does, and only what the
			// list puts inside itself is excluded. Asked before the value, so a list's own
			// duration is its own and is not handed down to its rows either.
			if (at != this && !at->inheritsMotion()) break;
			if (at->animateMs > 0.0f) return at->animateMs / 1000.0f;
		}
		return 0.0f;
	}

	float Widget::presence(Gui& gui) const {
		const float seconds = motionSeconds();
		if (seconds <= 0.0f) return visible ? 1.0f : 0.0f;
		// Asked for every frame, including while it is nothing, so the value stays in the
		// table: evicted, a hidden widget would forget it was hidden and the next time it
		// was shown it would appear at full size instead of growing into place.
		return gui.motion().value(gui.motionKey(mId.c_str(), kMotionCollapse),
		                          visible ? 1.0f : 0.0f, seconds);
	}

	bool Widget::acceptsKind(std::string_view kind) const {
		if (accepts.empty()) return true;
		std::string_view rest(accepts);
		while (!rest.empty()) {
			const size_t bar = rest.find('|');
			if (rest.substr(0, bar) == kind) return true;
			if (bar == std::string_view::npos) break;
			rest.remove_prefix(bar + 1);
		}
		return false;
	}

	std::string Widget::dragLabel() const {
		for (const std::unique_ptr<Widget>& child : mChildren) {
			if (!child || !child->visible) continue;
			std::string found = child->dragLabel();
			if (!found.empty()) return found;
		}
		return {};
	}

	void Widget::setShortcut(std::string_view text) {
		shortcut = {};
		if (text.empty()) return;
		std::string error;
		if (parseChord(text, shortcut, &error)) {
			const char* refused = shortcutRefusal(shortcut);
			if (!refused) return;
			error = refused;
			shortcut = {};
		}
		RDA_LOG_WARNING("shortcut '" << text << "' on '" << mId << "' " << error
		                << " - it has no shortcut");
	}

	void Widget::offerShortcuts(std::vector<Widget*>& out) {
		if (shortcut.valid()) out.push_back(this);
		for (const std::unique_ptr<Widget>& child : mChildren) {
			if (child && child->visible) child->offerShortcuts(out);
		}
	}

	Widget* pickShortcut(const std::vector<Widget*>& offered, const Chord& chord,
	                     Widget** alsoOffered) {
		Widget* chosen = nullptr;
		if (alsoOffered) *alsoOffered = nullptr;
		for (auto it = offered.rbegin(); it != offered.rend(); ++it) {
			if (!*it || (*it)->shortcut != chord) continue;
			if (!chosen) { chosen = *it; if (!alsoOffered) break; continue; }
			if (*it != chosen) { *alsoOffered = *it; break; }
		}
		return chosen;
	}

	Rect Widget::placement(Gui& gui, glm::vec2 origin) {
		const Rect where = [&] {
			if (mArrangedValid) {
				mArrangedValid = false; // consumed; re-set by the parent next frame
				return mArranged;
			}
			const Rect parent = gui.currentClipRect();
			// Measured only when asked for: some widgets count their lines to answer.
			const bool measures = width.mode == SizeSpec::Mode::Content
			                   || height.mode == SizeSpec::Mode::Content;
			const glm::vec2 content = measures ? measureContent(gui, { parent.w, parent.h })
			                                   : glm::vec2{ 0.0f, 0.0f };
			const Rect resolved = resolveRect(origin, parent, content);
			// A widget a container placed by its own x and y was never eased at all --
			// only a stack's children were, and a stack has no x and y to move between.
			// So this is the other half of what a route is for, and it is opt-in twice
			// over: a route has to be written, and a duration has to be given.
			const float seconds = motionSeconds();
			if (route.empty() || seconds <= 0.0f) return resolved;
			const Route::Shape* shape = Route::named(route);
			Motion& motion = gui.motion();
			const uint32_t key = gui.motionKey(mId.c_str(), kMotionPlace);
			const glm::vec2 travelled =
				motion.along(key, { resolved.x, resolved.y }, seconds, pace, shape);
			return Rect{ travelled.x, travelled.y, resolved.w, resolved.h };
		}();

		// Where this ended up, for whoever asked -- a <popup> anchored to it, and nothing
		// else so far. Free when nobody is asking.
		gui.noteAnchorIfWanted(mId, where);

		// Declared before this widget's children paint, so a button sitting on the bar
		// wins the press over the bar itself.
		if (dragWindow) gui.windowGrip(mId.c_str(), where);
		if (maximizeButton) gui.noteMaximizeButton(where);

		// Being drawn is being reachable. The cheap half of the question first: on a frame
		// with no key in it, this is one empty check.
		if (shortcut.valid() && gui.wantsShortcuts()) gui.offerShortcut(this);

		// Dragging and dropping, each as cheap as the shortcut above on a frame that has
		// nothing to do with it: a press, a drag in progress and a drop of files are all
		// rare, and each is asked about before anything else is looked at.
		const GuiInput& in = gui.input();
		if (!drag.empty() && in.lPressed && where.contains(in.pointer)) gui.notePickUp(this, where);
		if (onDrop && gui.dragging() && acceptsKind(gui.dragKind())) gui.offerDropTarget(this, where);
		if (onFiles && !in.droppedFiles.empty() && where.contains(in.pointer)) gui.noteFileTarget(this);

		// Described for a screen reader, when one is listening. Not from a subtree that
		// takes no input -- a screen on its way out is not somewhere the reader can go.
		if (gui.accessibilityWanted() && !gui.inert()) gui.noteAccessible(this, where);

		// The right button. Claimed rather than answered: a child drawn after this one
		// overwrites the claim, so the deepest widget that wanted it is the one that
		// hears it.
		if (onContext && gui.input().rPressed && where.contains(gui.input().pointer)) {
			gui.noteContext(this);
		}

		// Hover, for whoever asked. Only the change is reported: a handler wants to know
		// the pointer arrived, not that it is still here.
		if (onHover) {
			const bool over = where.contains(gui.input().pointer);
			if (over != mHovered) {
				mHovered = over;
				onHover(over);
			}
		}
		return where;
	}

	// ---- walk guard ------------------------------------------------------------------
	namespace {
		int gWalkDepth = 0;

		// Warns rather than asserts: a structural edit during the walk is now survivable
		// (paintChildren re-reads its size), so this points at the cause without taking
		// the application down over something that will merely look odd for a frame.
		int gBuildDepth = 0;   // inside a WidgetBuildScope

		void warnIfWalking(const char* what) {
			if (gWalkDepth > 0 && gBuildDepth == 0) {
				RDA_LOG_WARNING("Widget tree edited during the paint walk (" << what
					<< "). Queue it with Gui::tree() instead.");
			}
		}
	}

	WidgetBuildScope::WidgetBuildScope() { ++gBuildDepth; }
	WidgetBuildScope::~WidgetBuildScope() { --gBuildDepth; }

	WidgetWalkGuard::WidgetWalkGuard() { ++gWalkDepth; }
	WidgetWalkGuard::~WidgetWalkGuard() { --gWalkDepth; }
	bool WidgetWalkGuard::active() { return gWalkDepth > 0; }

	// ---- structural edits ------------------------------------------------------------
	Widget* Widget::addChild(std::unique_ptr<Widget> child) {
		warnIfWalking("addChild");
		Widget* raw = child.get();
		if (raw) raw->mParent = this;
		mChildren.push_back(std::move(child));
		return raw;
	}

	Widget* Widget::insertChild(std::unique_ptr<Widget> child, size_t index) {
		warnIfWalking("insertChild");
		Widget* raw = child.get();
		if (raw) raw->mParent = this;
		if (index >= mChildren.size()) mChildren.push_back(std::move(child));
		else mChildren.insert(mChildren.begin() + index, std::move(child));
		return raw;
	}

	std::unique_ptr<Widget> Widget::detachChild(Widget* child) {
		warnIfWalking("detachChild");
		for (auto it = mChildren.begin(); it != mChildren.end(); ++it) {
			if (it->get() != child) continue;
			std::unique_ptr<Widget> held = std::move(*it);
			mChildren.erase(it);
			held->mParent = nullptr;
			return held;
		}
		return nullptr;
	}

	void WidgetTree::remove(Widget* widget) {
		if (!widget) return;
		mCommands.push_back({ Op::Remove, widget->parent(), widget, nullptr, -1 });
	}

	void WidgetTree::move(Widget* child, int index) {
		if (!child) return;
		mCommands.push_back({ Op::Move, child->parent(), child, nullptr, index });
	}

	void WidgetTree::flush() {
		if (mCommands.empty()) return;
		// Taken by value first: applying a command can run application code that queues
		// more, and those belong to the next flush rather than to this loop.
		std::vector<Command> batch;
		batch.swap(mCommands);

		for (Command& command : batch) {
			if (!command.parent) continue;
			switch (command.op) {
			case Op::Insert:
				command.parent->insertChild(std::move(command.node),
					command.index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(command.index));
				break;
			case Op::Remove:
				// Dropping the returned owner destroys it here, outside any walk.
				command.parent->detachChild(command.target);
				break;
			case Op::Move:
				command.parent->moveChild(command.target,
					command.index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(command.index));
				break;
			}
		}
	}

	Widget* Widget::find(const std::string& id) {
		if (mId == id) return this;
		for (auto& child : mChildren) {
			if (Widget* found = child->find(id)) return found;
		}
		return nullptr;
	}

	bool Widget::remove(const std::string& id) {
		warnIfWalking("remove");
		for (auto it = mChildren.begin(); it != mChildren.end(); ++it) {
			if ((*it)->id() == id) {
				mChildren.erase(it);
				return true;
			}
			if ((*it)->remove(id)) return true;
		}
		return false;
	}

	bool Widget::moveChild(Widget* child, size_t index) {
		warnIfWalking("moveChild");
		for (size_t i = 0; i < mChildren.size(); ++i) {
			if (mChildren[i].get() != child) continue;
			if (index >= mChildren.size()) index = mChildren.size() - 1;
			std::unique_ptr<Widget> held = std::move(mChildren[i]);
			mChildren.erase(mChildren.begin() + i);
			mChildren.insert(mChildren.begin() + index, std::move(held));
			return true;
		}
		return false;
	}

	void Widget::paintChildren(Gui& gui, glm::vec2 origin) {
		// Indexed, not a range-for, and the size is re-read every step.
		//
		// A widget's callback can legally change the tree: a button whose onClick adds or
		// removes a sibling is an obvious thing to write, and it happens *during* this
		// walk. A range-for would be iterating a vector that push_back had just
		// reallocated, and the next step would read freed memory.
		//
		// This makes that survivable rather than correct: a child added mid-walk may be
		// painted this frame or next, and removing one can shift the rest. Callers that
		// care should defer structural edits until after the walk, the way DockSpace
		// queues its removals.
		for (size_t i = 0; i < mChildren.size(); ++i) {
			Widget* child = mChildren[i].get();
			if (child && child->visible) child->paint(gui, origin);
		}
	}

	// ---- what each widget tells a screen reader -------------------------------------------
	//
	// One function per widget, saying what it is in the words it already has: a button is
	// called what it says, a checkbox what its label says. `alt` wins wherever it is given,
	// and is the only name a widget with no words of its own -- a slider, a picture -- has.

	bool Widget::describe(AccessibleInfo& out) const {
		// A container is a node only when somebody named it. Otherwise it is layout, and
		// what is inside it belongs to whatever is around it.
		if (alt.empty()) return false;
		out.role = AccessibleRole::Group;
		out.name = alt;
		return true;
	}
}
