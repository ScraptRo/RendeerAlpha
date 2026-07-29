#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Gui.h>

namespace RDA {

	Widget* Widget::addChild(std::unique_ptr<Widget> child) {
		Widget* raw = child.get();
		mChildren.push_back(std::move(child));
		return raw;
	}

	Widget* Widget::find(const std::string& id) {
		if (mId == id) return this;
		for (auto& child : mChildren) {
			if (Widget* found = child->find(id)) return found;
		}
		return nullptr;
	}

	bool Widget::remove(const std::string& id) {
		for (auto it = mChildren.begin(); it != mChildren.end(); ++it) {
			if ((*it)->id() == id) {
				mChildren.erase(it);
				return true;
			}
			if ((*it)->remove(id)) return true;
		}
		return false;
	}

	void Widget::paintChildren(Gui& gui, glm::vec2 origin) {
		for (auto& child : mChildren) {
			if (child->visible) child->paint(gui, origin);
		}
	}

	// ---- concrete widgets: each is one immediate-mode call ------------------------
	void Container::paint(Gui& gui, glm::vec2 origin) {
		paintChildren(gui, glm::vec2(origin.x + rect.x, origin.y + rect.y));
	}

	void Panel::paint(Gui& gui, glm::vec2 origin) {
		Rect abs{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h };
		gui.beginPanel(mId.c_str(), abs, variant);
		// Children are positioned relative to the panel's top-left.
		paintChildren(gui, glm::vec2(abs.x, abs.y));
		gui.endPanel();
	}

	void Label::paint(Gui& gui, glm::vec2 origin) {
		// A named variant's color takes precedence; otherwise the per-instance `color`.
		uint32_t c = (variant != kDefaultVariant) ? gui.theme().label(variant).color : color;
		gui.label(text.c_str(), glm::vec2(origin.x + rect.x, origin.y + rect.y), c);
	}

	void Button::paint(Gui& gui, glm::vec2 origin) {
		Rect abs{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h };
		if (gui.button(mId.c_str(), text.c_str(), abs, variant)) {
			if (onClick) onClick();
		}
	}

	void Checkbox::paint(Gui& gui, glm::vec2 origin) {
		Rect abs{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h };
		if (gui.checkbox(mId.c_str(), label.c_str(), value, abs, variant)) {
			if (onChange) onChange(value);
		}
	}

	void Slider::paint(Gui& gui, glm::vec2 origin) {
		Rect abs{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h };
		if (gui.sliderFloat(mId.c_str(), value, minValue, maxValue, abs, variant)) {
			if (onChange) onChange(value);
		}
	}

	void TextField::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect),
		// so the field grows and shrinks as its container is resized.
		Rect abs = (rect.w > 0.0f && rect.h > 0.0f)
			? Rect{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h }
			: gui.currentClipRect();
		// A named variant supplies the whole style; otherwise the per-instance one.
		const TextFieldStyle& used = (variant != kDefaultVariant) ? gui.theme().textField(variant) : style;
		if (gui.textField(mId.c_str(), text, abs, used)) {
			if (onChange) onChange(text);
		}
	}

	void Viewport::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect).
		Rect area = (rect.w > 0.0f && rect.h > 0.0f)
			? Rect{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h }
			: gui.currentClipRect();
		gui.setViewportRect(area); // so the engine renders the scene at this resolution
		const Texture* scene = gui.sceneTexture();
		if (scene) gui.image(area, scene);
		else       gui.drawRect(area, rgba(18, 20, 26)); // Fullscreen mode: no scene texture
	}
}
