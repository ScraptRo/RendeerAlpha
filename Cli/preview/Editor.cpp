#include "Editor.h"

#include "Starters.h"

#include <RendeerAlpha.h>
#include <Core/FileStamp.h>
#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Window.h>
#include <Layout/Blueprint.h>
#include <Layout/ImportScan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Cli {

	namespace {

		using RDA::Gui;
		using RDA::Rect;
		using RDA::Layout::PropDesc;
		using RDA::Layout::PropType;
		namespace Jsx = RDA::Layout::Jsx;

		// Its own colours and its own small text, drawn with the plain primitives. The project's
		// theme is what is being designed underneath, and a tool that wore it would change every
		// time the thing it is editing did.
		const uint32_t kPaper   = RDA::rgba(23, 25, 32, 245);
		const uint32_t kRow     = RDA::rgba(38, 42, 52);
		const uint32_t kRaised  = RDA::rgba(54, 60, 74);
		const uint32_t kInk     = RDA::rgba(228, 230, 235);
		const uint32_t kDim     = RDA::rgba(150, 156, 170);
		const uint32_t kAccent  = RDA::rgba(96, 165, 250);
		const uint32_t kOutline = RDA::rgba(96, 165, 250, 170);
		const uint32_t kHover   = RDA::rgba(250, 204, 21, 200);
		const uint32_t kTint    = RDA::rgba(96, 165, 250, 40);
		const uint32_t kWarning = RDA::rgba(248, 150, 120);

		constexpr float kPanelWidth = 264.0f;
		constexpr float kMargin = 8.0f;
		constexpr float kTitleHeight = 28.0f;
		constexpr float kItemHeight = 24.0f;
		constexpr float kRowHeight = 30.0f;
		constexpr float kLabelWidth = 92.0f;

		RDA::TextStyle small() {
			RDA::TextStyle style;
			style.size = 13.0f;
			return style;
		}

		void text(Gui& gui, const std::string& what, float x, float y, uint32_t color) {
			gui.drawText(what.c_str(), { x, y }, color, small());
		}

		// `what`, cut to fit, so a long value cannot run out of the box it is drawn in.
		std::string fitted(const Gui& gui, const std::string& what, float width) {
			if (gui.measureText(what.c_str(), small()) <= width) return what;
			std::string cut = what;
			while (!cut.empty() && gui.measureText((cut + "...").c_str(), small()) > width) cut.pop_back();
			return cut + "...";
		}

		void outline(Gui& gui, const Rect& rect, uint32_t color, float width) {
			gui.drawRect({ rect.x, rect.y, rect.w, width }, color);
			gui.drawRect({ rect.x, rect.y + rect.h - width, rect.w, width }, color);
			gui.drawRect({ rect.x, rect.y, width, rect.h }, color);
			gui.drawRect({ rect.x + rect.w - width, rect.y, width, rect.h }, color);
		}

		// A small flag on a widget's top edge, saying which one it is.
		void tag(Gui& gui, const Rect& rect, const std::string& what, uint32_t color) {
			const float width = gui.measureText(what.c_str(), small()) + 10.0f;
			const float y = rect.y >= 18.0f ? rect.y - 18.0f : rect.y;
			gui.drawRectRounded({ rect.x, y, width, 18.0f }, color, 3.0f);
			text(gui, what, rect.x + 5.0f, y + 1.0f, RDA::rgba(16, 18, 24));
		}

		bool readFile(const std::string& path, std::string& into) {
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			std::ostringstream all;
			all << in.rdbuf();
			into = all.str();
			return true;
		}

		bool writeFile(const std::string& path, const std::string& text) {
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			return out && out.write(text.data(), static_cast<std::streamsize>(text.size()));
		}

		std::vector<std::string> wordsOf(const char* values) {
			std::vector<std::string> out;
			std::istringstream all(values ? values : "");
			std::string one;
			while (std::getline(all, one, '|')) out.push_back(one);
			return out;
		}

		// A choice from a short list, rather than something to type.
		bool isChoice(const PropDesc& prop) {
			return prop.type == PropType::Enum || prop.type == PropType::Align;
		}
	}

	Editor::Editor() {
		RDA::parseChord("F2", mToggle);
		RDA::parseChord("Escape", mCancel);
	}

	void Editor::show(const std::string& blueprint, const std::string& source) {
		mBlueprint = blueprint;
		mSource = source;
		mStamp = 0;
		mSelected = -1;
		mFields.clear();
		mAnnounced = false;
	}

	// ---- what is on screen ----------------------------------------------------------------------

	void Editor::reload(Gui& gui) {
		RDA::Layout::Blueprint blueprint;
		std::string error;
		if (!RDA::Layout::loadBlueprintFile(mBlueprint, blueprint, error)) return;

		const std::string selected = !mPendingSelect.empty() ? mPendingSelect
		                           : mSelected >= 0 ? mNodes[static_cast<size_t>(mSelected)].id : std::string();
		mPendingSelect.clear();

		mNodes.clear();
		mNodes.resize(blueprint.nodeCount());
		mAnyEditable = false;
		mSelected = -1;
		for (size_t i = 0; i < blueprint.nodeCount(); ++i) {
			const RDA::Layout::BlueprintNode& source = blueprint.node(i);
			Node& node = mNodes[i];
			node.id = std::string(blueprint.string(source.id));
			node.type = std::string(blueprint.string(source.type));
			node.editable = blueprint.boolean(source, "editable", false);
			node.horizontal = blueprint.text(source, "arrange") == "horizontal";
			node.arranges = node.type == "stack" || node.type == "scroll";
			mAnyEditable = mAnyEditable || node.editable;
			if (source.parent != RDA::Layout::kNoParent) {
				const Node& parent = mNodes[source.parent];
				node.parent = static_cast<int>(source.parent);
				node.depth = parent.depth + 1;
				node.inside = parent.inside;
				mNodes[source.parent].children.push_back(static_cast<int>(i));
			}
			node.inside = node.inside || node.editable;
			gui.wantAnchor(node.id);
			if (node.id == selected) mSelected = static_cast<int>(i);
		}
		// The ids asked for have no rectangles until a walk has drawn them, and a window with
		// nothing happening in it is not walking.
		gui.markDirty();
		rendeerRequestRedraw();
		readFields();

		if (mAnyEditable && !mAnnounced) {
			mAnnounced = true;
			std::printf("rda: editing %s -- drag widgets into what is outlined; F2 hides this\n", mSource.c_str());
			std::fflush(stdout);
		}
	}

	void Editor::measure(Gui& gui) {
		// From the last node up, so a child is known before its parent is asked. A <container>
		// draws nothing and reports no rectangle of its own, so it is what its children cover.
		for (size_t i = mNodes.size(); i-- > 0;) {
			Node& node = mNodes[i];
			node.placed = gui.anchorRect(node.id, node.rect);
			if (node.placed) continue;
			for (const int child : node.children) {
				const Node& one = mNodes[static_cast<size_t>(child)];
				if (!one.placed) continue;
				if (!node.placed) {
					node.rect = one.rect;
					node.placed = true;
					continue;
				}
				const float right = (std::max)(node.rect.x + node.rect.w, one.rect.x + one.rect.w);
				const float bottom = (std::max)(node.rect.y + node.rect.h, one.rect.y + one.rect.h);
				node.rect.x = (std::min)(node.rect.x, one.rect.x);
				node.rect.y = (std::min)(node.rect.y, one.rect.y);
				node.rect.w = right - node.rect.x;
				node.rect.h = bottom - node.rect.y;
			}
		}
	}

	int Editor::nodeAt(glm::vec2 point) const {
		int best = -1;
		for (size_t i = 0; i < mNodes.size(); ++i) {
			const Node& node = mNodes[i];
			if (!node.inside || !node.placed || !node.rect.contains(point)) continue;
			if (best < 0 || node.depth >= mNodes[static_cast<size_t>(best)].depth) best = static_cast<int>(i);
		}
		return best;
	}

	int Editor::dropTargetAt(glm::vec2 point) const {
		int best = -1;
		for (size_t i = 0; i < mNodes.size(); ++i) {
			const Node& node = mNodes[i];
			if (!node.editable || !node.placed || !node.rect.contains(point)) continue;
			if (best < 0 || node.depth >= mNodes[static_cast<size_t>(best)].depth) best = static_cast<int>(i);
		}
		return best;
	}

	size_t Editor::dropIndex(int target, glm::vec2 point) const {
		const Node& parent = mNodes[static_cast<size_t>(target)];
		size_t index = 0;
		for (const int child : parent.children) {
			const Node& one = mNodes[static_cast<size_t>(child)];
			if (!one.placed) continue;
			const float middle = parent.horizontal ? one.rect.x + one.rect.w * 0.5f : one.rect.y + one.rect.h * 0.5f;
			const float along = parent.horizontal ? point.x : point.y;
			if (along < middle) return index;
			++index;
		}
		return parent.children.size();
	}

	// ---- changing the file ----------------------------------------------------------------------

	Editor::Located Editor::locate(int node, std::string* why) const {
		const Node& target = mNodes[static_cast<size_t>(node)];
		std::vector<std::string> files = RDA::Layout::importedFiles(mSource);
		if (files.empty()) files.push_back(mSource);

		std::vector<Located> read;
		std::string missing;
		bool anyElements = false;
		for (const std::string& file : files) {
			std::string source;
			if (!readFile(file, source)) continue;
			Located one;
			one.file = file;
			one.document = std::make_unique<Jsx::Document>(std::move(source));
			anyElements = anyElements || !one.document->elements().empty();
			std::string stopped;
			one.element = one.document->find(target.id, target.type, &stopped);
			if (one.element >= 0) return one;
			if (missing.empty()) missing = stopped;
			read.push_back(std::move(one));
		}

		// What a component returns goes by its call site's id, in a different file.
		if (target.editable) {
			for (Located& one : read) {
				one.element = one.document->editableRoot(target.type);
				if (one.element >= 0) return std::move(one);
			}
		}

		if (why) *why = anyElements ? missing : "this layout is written as objects, and only JSX can be edited here";
		return {};
	}

	bool Editor::edit(int node, const Change& change) {
		std::string why;
		Located found = locate(node, &why);
		if (!found.document) {
			mStatus = why;
			std::printf("rda: %s\n", mStatus.c_str());
			std::fflush(stdout);
			return false;
		}
		change(*found.document, found.element);
		if (!writeFile(found.file, found.document->text())) {
			mStatus = "could not write " + found.file;
			return false;
		}
		mStatus.clear();
		// The loop is asleep until something happens, and this is what should happen.
		rendeerRequestRedraw();
		return true;
	}

	void Editor::drop(glm::vec2 point) {
		const std::string type = mCarried;
		mCarried.clear();
		const int target = dropTargetAt(point);
		if (target < 0) return;
		const Starter* starter = starterFor(type);
		if (!starter) return;

		const Node& parent = mNodes[static_cast<size_t>(target)];
		const size_t index = parent.arranges ? dropIndex(target, point) : parent.children.size();

		// Nothing arranges a child of a <panel> or a <container>: it goes where it was let go,
		// and it needs a size, or it would fill its parent.
		std::string placement;
		if (!parent.arranges) {
			const int x = static_cast<int>(std::lround(point.x - parent.rect.x));
			const int y = static_cast<int>(std::lround(point.y - parent.rect.y));
			placement = "x={" + std::to_string(x) + "} y={" + std::to_string(y) + "} width={" +
			            std::to_string(static_cast<int>(starter->width)) + "} height={" +
			            std::to_string(static_cast<int>(starter->height)) + "}";
		}

		std::string id;
		const bool written = edit(target, [&](Jsx::Document& document, int element) {
			id = document.uniqueId(type);
			// By the child it lands in front of, because the compiled children and the written
			// ones are not always the same list: code can make some of them.
			size_t before = document.elements()[static_cast<size_t>(element)].children.size();
			if (index < parent.children.size()) {
				const Node& next = mNodes[static_cast<size_t>(parent.children[index])];
				const int found = document.find(next.id, next.type);
				const std::vector<int>& written = document.elements()[static_cast<size_t>(element)].children;
				const auto at = std::find(written.begin(), written.end(), found);
				if (at != written.end()) before = static_cast<size_t>(at - written.begin());
			}
			document.insertChild(element, starterSource(*starter, id, placement), before);
		});
		if (!written) return;
		mPendingSelect = parent.id + "/" + id;
		std::printf("rda: added <%s> %s to %s\n", type.c_str(), id.c_str(), parent.id.c_str());
		std::fflush(stdout);
	}

	void Editor::removeSelected() {
		if (mSelected < 0) return;
		const std::string id = mNodes[static_cast<size_t>(mSelected)].id;
		const bool written = edit(mSelected, [](Jsx::Document& document, int element) {
			document.removeElement(element);
		});
		if (!written) return;
		select(-1);
		std::printf("rda: removed %s\n", id.c_str());
		std::fflush(stdout);
	}

	// ---- the properties -------------------------------------------------------------------------

	void Editor::select(int node) {
		commitAll();
		mSelected = node;
		mScroll = 0.0f;
		readFields();
	}

	void Editor::readFields() {
		mFields.clear();
		if (mSelected < 0) return;
		const Node& node = mNodes[static_cast<size_t>(mSelected)];
		const RDA::Layout::WidgetDesc* widget = RDA::Layout::findWidget(node.type);
		if (!widget) return;

		const Located found = locate(mSelected);
		if (!found.document) return;

		const auto add = [&](const PropDesc& prop) {
			if (prop.type == PropType::Event || std::string_view(prop.name) == "editable") return;
			Field field;
			field.prop = &prop;
			const Jsx::Value value = found.document->value(found.element, prop.name);
			field.computed = value.computed;
			field.text = field.saved = value.text;
			mFields.push_back(std::move(field));
		};
		for (size_t i = 0; i < widget->propCount; ++i) add(widget->props[i]);

		// Of the common ones, the handful that say where and how big, and which of those depends
		// on what is holding it: a stack ignores x, and only a stack reads hAlignSelf.
		const bool arranged = node.parent >= 0 && mNodes[static_cast<size_t>(node.parent)].arranges;
		for (const char* name : { "width", "height" }) {
			if (const PropDesc* prop = RDA::Layout::findProp(*widget, name)) add(*prop);
		}
		for (const char* name : { "hAlignSelf", "vAlignSelf" }) {
			if (!arranged) break;
			if (const PropDesc* prop = RDA::Layout::findProp(*widget, name)) add(*prop);
		}
		for (const char* name : { "x", "y" }) {
			if (arranged) break;
			if (const PropDesc* prop = RDA::Layout::findProp(*widget, name)) add(*prop);
		}
	}

	void Editor::commit(Field& field) {
		if (field.text == field.saved || field.computed || mSelected < 0) return;
		const std::string name = field.prop->name;

		if (field.text.empty()) {
			if (edit(mSelected, [&](Jsx::Document& document, int element) {
				document.removeAttribute(element, name);
			})) field.saved.clear();
			return;
		}

		std::string value, why;
		if (!RDA::Layout::Jsx::formatValue(field.prop->type, field.text, value, why)) {
			mStatus = name + " wants " + why;
			field.text = field.saved;
			return;
		}
		if (edit(mSelected, [&](Jsx::Document& document, int element) {
			document.setAttribute(element, name, value);
		})) field.saved = field.text;
	}

	void Editor::commitAll() {
		for (Field& field : mFields) commit(field);
	}

	// ---- the frame ------------------------------------------------------------------------------

	void Editor::frame(Gui& gui) {
		if (mBlueprint.empty()) return;

		// The file, not an event: a save by hand is the same thing as one from here.
		const int64_t stamp = RDA::fileStamp(mBlueprint);
		if (stamp != mStamp) {
			mStamp = stamp;
			reload(gui);
		}
		if (!mAnyEditable) {
			closePanel();
			return;
		}

		// Closed with its own X, which is the same as asking for it to go.
		if (mPanelWindow && !mPanelWindow->windowIsUp()) {
			closePanel();
			mVisible = false;
			gui.markDirty();
			rendeerRequestRedraw();
		}

		// Whichever window has the keyboard.
		readKeys(gui);
		if (mPanelWindow && !mPanelIsNew) readKeys(mPanelWindow->gui());
		if (!mVisible) {
			closePanel();
			return;
		}

		// For the next walk: whatever is under this takes no pointer while it is up.
		gui.claimPointer();
		measure(gui);

		if (mFloating && !mPanelWindow) openPanel(gui);
		if (mPanelIsNew) {
			// The loop began this frame's GUIs before the window existed, so there is nothing
			// to draw into until the next one.
			mPanelIsNew = false;
			drawOutlines(gui, gui.input().pointer, false);
			rendeerRequestRedraw();
			return;
		}

		Gui& panelGui = mPanelWindow ? mPanelWindow->gui() : gui;
		const RDA::GuiInput& in = gui.input();
		const RDA::GuiInput& panelIn = panelGui.input();
		if (panelIn.lPressed) mPressAt = panelIn.pointer;

		const Rect panel = panelRect(panelGui);
		// Only a panel drawn over the layout can be what a click in the layout is on.
		const bool overPanel = !mPanelWindow && panel.contains(in.pointer);
		glm::vec2 pointer{ 0.0f };
		const bool located = pointerOverLayout(gui, panelGui, pointer);

		if (!mCarried.empty()) {
			if (mHeld) {
				if (panelIn.lReleased) {
					mHeld = false;
					// Let go on the panel was a click, and it stays picked up. So does a drag that
					// ended somewhere that cannot be worked out, for a click to say where.
					if (located && !panel.contains(panelIn.pointer)) drop(pointer);
				}
			} else if (in.lPressed && !overPanel) {
				drop(in.pointer);
			} else if (panelIn.lPressed && panel.contains(panelIn.pointer)) {
				mCarried.clear();
			}
		} else if (in.lPressed && !overPanel) {
			select(nodeAt(in.pointer));
		}

		drawOutlines(gui, pointer, overPanel);
		drawPanel(panelGui);
		if (!mCarried.empty() && located) drawCarried(gui, pointer);
	}

	void Editor::readKeys(Gui& gui) {
		for (const RDA::Chord& chord : gui.input().chords) {
			if (chord == mToggle) {
				mVisible = !mVisible;
				gui.markDirty();
				rendeerRequestRedraw();
			}
			if (chord == mCancel) {
				mCarried.clear();
				mHeld = false;
			}
		}
	}

	bool Editor::pointerOverLayout(const Gui& gui, const Gui& panel, glm::vec2& out) const {
		out = gui.input().pointer;
		if (!mPanelWindow || !mHeld) return true;

		int layoutX = 0, layoutY = 0, panelX = 0, panelY = 0;
		getMainWindow()->getPosition(layoutX, layoutY);
		mPanelWindow->getPosition(panelX, panelY);
		// Two windows cannot both be at the origin, because the panel opens inside the layout's.
		// That is how a system that keeps positions to itself looks from here.
		if (layoutX == 0 && layoutY == 0 && panelX == 0 && panelY == 0) return false;

		const glm::vec2 between(static_cast<float>(panelX - layoutX), static_cast<float>(panelY - layoutY));
		out = panel.input().pointer + between / gui.unitScale();
		return true;
	}

	void Editor::openPanel(Gui& gui) {
		const float scale = gui.unitScale();
		const glm::vec2 viewport = gui.input().viewport;
		const float height = (std::min)(viewport.y, 640.0f);
		RDA::Window* window = rendeerCreateWindow(static_cast<uint32_t>(kPanelWidth * scale),
		                                          static_cast<uint32_t>(height * scale), "rda editor");
		if (!window) {
			// Drawn over the layout, as a shot has it, rather than not at all.
			mFloating = false;
			return;
		}
		window->setAlwaysOnTop(true);

		// Inside the layout's window rather than beside it, where there may be no screen.
		int x = 0, y = 0;
		getMainWindow()->getPosition(x, y);
		window->setPosition(x + static_cast<int>((viewport.x - kPanelWidth - 16.0f) * scale),
		                    y + static_cast<int>(16.0f * scale));
		mPanelWindow = window;
		mPanelIsNew = true;
	}

	void Editor::closePanel() {
		if (!mPanelWindow) return;
		commitAll();
		mCarried.clear();
		mHeld = false;
		rendeerDestroyWindow(mPanelWindow);
		mPanelWindow = nullptr;
		mPanelIsNew = false;
	}

	bool Editor::clicked(const Gui& gui, const Rect& rect) const {
		const RDA::GuiInput& in = gui.input();
		return in.lReleased && rect.contains(in.pointer) && rect.contains(mPressAt);
	}

	Rect Editor::panelRect(const Gui& gui) const {
		const glm::vec2 viewport = gui.input().viewport;
		if (mPanelWindow) return { 0.0f, 0.0f, viewport.x, viewport.y };
		const float x = mPanelAt.x >= 0.0f ? mPanelAt.x : viewport.x - kPanelWidth - kMargin;
		const float y = mPanelAt.y >= 0.0f ? mPanelAt.y : kMargin;
		return { x, y, kPanelWidth, (std::max)(120.0f, viewport.y - y - kMargin) };
	}

	void Editor::drawOutlines(Gui& gui, glm::vec2 pointer, bool overPanel) {
		for (const Node& node : mNodes) {
			if (!node.editable || !node.placed) continue;
			outline(gui, node.rect, kOutline, 1.0f);
		}

		if (!mCarried.empty()) {
			// Where it would land: the container, and between which of its children.
			const int target = dropTargetAt(pointer);
			if (target < 0) return;
			const Node& parent = mNodes[static_cast<size_t>(target)];
			gui.drawRect(parent.rect, kTint);
			outline(gui, parent.rect, kAccent, 2.0f);
			if (!parent.arranges) return;
			const size_t index = dropIndex(target, pointer);
			float at = parent.horizontal ? parent.rect.x + 4.0f : parent.rect.y + 4.0f;
			if (!parent.children.empty()) {
				const bool past = index >= parent.children.size();
				const Node& near = mNodes[static_cast<size_t>(parent.children[past ? parent.children.size() - 1 : index])];
				if (parent.horizontal) at = past ? near.rect.x + near.rect.w + 2.0f : near.rect.x - 2.0f;
				else                   at = past ? near.rect.y + near.rect.h + 2.0f : near.rect.y - 2.0f;
			}
			if (parent.horizontal) gui.drawRect({ at - 1.0f, parent.rect.y, 2.0f, parent.rect.h }, kAccent);
			else                   gui.drawRect({ parent.rect.x, at - 1.0f, parent.rect.w, 2.0f }, kAccent);
			return;
		}

		const int hovered = overPanel ? -1 : nodeAt(pointer);
		if (hovered >= 0 && hovered != mSelected) {
			const Node& node = mNodes[static_cast<size_t>(hovered)];
			outline(gui, node.rect, kHover, 1.0f);
			tag(gui, node.rect, "<" + node.type + ">", kHover);
		}
		if (mSelected >= 0 && mNodes[static_cast<size_t>(mSelected)].placed) {
			const Node& node = mNodes[static_cast<size_t>(mSelected)];
			outline(gui, node.rect, kAccent, 2.0f);
			tag(gui, node.rect, "<" + node.type + ">", kAccent);
		}
	}

	void Editor::drawCarried(Gui& gui, glm::vec2 pointer) {
		const std::string label = "<" + mCarried + ">";
		const float width = gui.measureText(label.c_str(), small()) + 14.0f;
		gui.drawRectRounded({ pointer.x + 10.0f, pointer.y + 10.0f, width, 22.0f }, kAccent, 4.0f);
		text(gui, label, pointer.x + 17.0f, pointer.y + 13.0f, RDA::rgba(16, 18, 24));
	}

	void Editor::drawPanel(Gui& gui) {
		const RDA::GuiInput& in = gui.input();
		const Rect panel = panelRect(gui);

		// Over the layout it is moved by its title, so that it can be taken off whatever it is
		// covering. In a window of its own the system does that.
		if (!mPanelWindow) {
			const Rect title{ panel.x, panel.y, panel.w, kTitleHeight };
			if (in.lPressed && title.contains(in.pointer)) {
				mMovingPanel = true;
				mGrab = in.pointer - glm::vec2(panel.x, panel.y);
			}
			if (mMovingPanel) {
				mPanelAt.x = (std::max)(0.0f, in.pointer.x - mGrab.x);
				mPanelAt.y = (std::max)(0.0f, in.pointer.y - mGrab.y);
				if (!in.lDown) mMovingPanel = false;
			}
		}

		if (mPanelWindow) gui.drawRect(panel, kPaper);
		else              gui.drawRectRounded(panel, kPaper, 8.0f);
		text(gui, "Editing", panel.x + 12.0f, panel.y + 7.0f, kInk);
		const std::string hint = "F2 hides this";
		text(gui, hint, panel.x + panel.w - gui.measureText(hint.c_str(), small()) - 12.0f, panel.y + 7.0f, kDim);

		const float inner = panel.w - 24.0f;
		float y = drawPalette(gui, panel.x + 12.0f, panel.y + kTitleHeight, inner);

		y += 8.0f;
		gui.drawRect({ panel.x + 12.0f, y, inner, 1.0f }, kRaised);
		y += 8.0f;

		if (!mCarried.empty() && !mHeld) {
			text(gui, "Click inside an outline to", panel.x + 12.0f, y, kInk);
			text(gui, "place it. Esc puts it back.", panel.x + 12.0f, y + 18.0f, kInk);
		} else if (mSelected < 0) {
			text(gui, "Click a widget inside an outline", panel.x + 12.0f, y, kDim);
			text(gui, "to edit it, or add one from here.", panel.x + 12.0f, y + 18.0f, kDim);
		} else {
			const Node& node = mNodes[static_cast<size_t>(mSelected)];
			text(gui, fitted(gui, "<" + node.type + ">  " + node.id, inner), panel.x + 12.0f, y, kInk);
			y += 22.0f;

			// Room for the fields is what is left above the bottom line.
			const float bottom = panel.y + panel.h - (node.parent >= 0 ? 42.0f : 12.0f) - (mStatus.empty() ? 0.0f : 40.0f);
			drawFields(gui, { panel.x + 12.0f, y, inner, (std::max)(0.0f, bottom - y) });

			if (node.parent >= 0) {
				const Rect remove{ panel.x + 12.0f, panel.y + panel.h - 34.0f, inner, kItemHeight };
				const bool over = remove.contains(in.pointer);
				gui.drawRectRounded(remove, over ? RDA::rgba(120, 50, 50) : kRow, 4.0f);
				text(gui, "Remove from the file", remove.x + 8.0f, remove.y + 4.0f, kInk);
				if (clicked(gui, remove)) removeSelected();
			}
		}
		if (!mStatus.empty()) {
			float at = panel.y + panel.h - (mSelected >= 0 ? 76.0f : 44.0f);
			int shown = 0;
			for (const auto& [begin, length] : gui.wrapText(mStatus, inner, small())) {
				if (shown++ == 2) break;
				text(gui, mStatus.substr(begin, length), panel.x + 12.0f, at, kWarning);
				at += 17.0f;
			}
		}
	}

	float Editor::drawPalette(Gui& gui, float x, float y, float width) {
		const RDA::GuiInput& in = gui.input();
		size_t count = 0;
		const Starter* all = starters(count);
		const float cell = (width - 6.0f) * 0.5f;

		for (size_t i = 0; i < count; ++i) {
			const Rect item{ x + static_cast<float>(i % 2) * (cell + 6.0f),
			                 y + static_cast<float>(i / 2) * (kItemHeight + 4.0f), cell, kItemHeight };
			const bool over = item.contains(in.pointer);
			const bool carried = mCarried == all[i].type;
			gui.drawRectRounded(item, carried ? kAccent : over ? kRaised : kRow, 4.0f);
			text(gui, all[i].type, item.x + 8.0f, item.y + 4.0f, carried ? RDA::rgba(16, 18, 24) : kInk);
			if (over && in.lPressed) {
				mCarried = all[i].type;
				mHeld = true;
			}
		}
		return y + static_cast<float>((count + 1) / 2) * (kItemHeight + 4.0f);
	}

	void Editor::drawFields(Gui& gui, const Rect& area) {
		const RDA::GuiInput& in = gui.input();
		const float content = static_cast<float>(mFields.size()) * kRowHeight;
		if (area.contains(in.pointer) && in.scroll != 0.0f) mScroll -= in.scroll * kRowHeight;
		mScroll = (std::max)(0.0f, (std::min)(mScroll, content - area.h));

		gui.pushClipRect(area);
		float y = area.y - mScroll;
		for (Field& field : mFields) {
			const Rect row{ area.x, y, area.w, kRowHeight - 4.0f };
			y += kRowHeight;
			// Only what fits whole is drawn, so nothing half under the edge can be clicked.
			if (row.y < area.y - 0.5f || row.y + row.h > area.y + area.h + 0.5f) continue;
			drawField(gui, field, row);
		}
		gui.popClipRect();
	}

	void Editor::drawField(Gui& gui, Field& field, const Rect& row) {
		const RDA::GuiInput& in = gui.input();
		text(gui, fitted(gui, field.prop->name, kLabelWidth - 6.0f), row.x, row.y + 5.0f, kDim);
		const Rect box{ row.x + kLabelWidth, row.y, row.w - kLabelWidth, row.h };

		if (field.computed) {
			// Typing over an expression would lose it, so it is only shown.
			gui.drawRectRounded(box, kRow, 4.0f);
			text(gui, fitted(gui, field.text, box.w - 12.0f), box.x + 6.0f, box.y + 5.0f, kDim);
			return;
		}

		if (field.prop->type == PropType::Bool) {
			const bool on = field.text == "true";
			const Rect tick{ box.x, box.y + 4.0f, 18.0f, 18.0f };
			gui.drawRectRounded(tick, on ? kAccent : kRow, 3.0f);
			text(gui, field.text.empty() ? "default" : field.text, box.x + 26.0f, box.y + 5.0f, kDim);
			if (clicked(gui, box)) {
				field.text = on ? "false" : "true";
				commit(field);
			}
			return;
		}

		if (isChoice(*field.prop)) {
			// Each click is the next word, with no word at all as one of them.
			const bool over = box.contains(in.pointer);
			gui.drawRectRounded(box, over ? kRaised : kRow, 4.0f);
			text(gui, field.text.empty() ? "default" : field.text, box.x + 6.0f, box.y + 5.0f,
			     field.text.empty() ? kDim : kInk);
			if (clicked(gui, box)) {
				const std::vector<std::string> words = wordsOf(field.prop->values);
				const auto at = std::find(words.begin(), words.end(), field.text);
				if (at == words.end())          field.text = words.empty() ? "" : words.front();
				else if (at + 1 == words.end()) field.text.clear();
				else                            field.text = *(at + 1);
				commit(field);
			}
			return;
		}

		RDA::TextFieldStyle style = RDA::TextFieldStyle::forMode(RDA::TextFieldMode::Line);
		style.padding = 4.0f;
		style.background = kRow;
		bool focused = false;
		bool submitted = false;
		Gui::TextFieldExtras extras;
		extras.submitted = &submitted;
		const std::string id = std::string("rda.editor.") + field.prop->name;
		gui.textField(id.c_str(), field.text, "", box, style, &focused, 0, &extras);
		// Saved on Enter, and on leaving it.
		if (submitted || (field.focused && !focused)) commit(field);
		field.focused = focused;
	}
}
