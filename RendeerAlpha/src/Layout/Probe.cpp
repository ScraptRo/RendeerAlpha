#include <Layout/Probe.h>
#include <Layout/Bindings.h>
#include <Logger/Logger.h>

namespace RDA::Layout {

	namespace {
		// Depth-first over the tree, so a caller sees ids in the order the layout reads.
		void collect(const Widget& widget, std::vector<std::string>& out) {
			out.push_back(widget.id());
			for (const auto& child : widget.children()) {
				if (child) collect(*child, out);
			}
		}

		Widget* search(Widget& widget, std::string_view id) {
			if (widget.id() == id) return &widget;
			for (const auto& child : widget.children()) {
				if (!child) continue;
				if (Widget* found = search(*child, id)) return found;
			}
			return nullptr;
		}
	}

	bool Probe::open(const std::string& blueprintPath) {
		close();
		Blueprint blueprint;
		if (!loadBlueprintFile(blueprintPath, blueprint, mError)) return false;
		return openFrom(std::move(blueprint));
	}

	bool Probe::openFrom(Blueprint blueprint) {
		close();
		mBlueprint = std::move(blueprint);
		mInstance = instantiate(mBlueprint, mRoot);
		if (!mInstance.valid()) {
			mError = "the blueprint holds no widgets this build knows how to make";
			return false;
		}
		// The state a layout opens in is what its bindings say, not what the signals
		// happened to be left at. A frame does this before anything is drawn.
		bindings().applyAll();
		mError.clear();
		return true;
	}

	void Probe::close() {
		mInstance.release();
		mRoot.clearChildren();
		mError.clear();
	}

	std::vector<std::string> Probe::ids() const {
		std::vector<std::string> out;
		for (const auto& child : mRoot.children()) {
			if (child) collect(*child, out);
		}
		return out;
	}

	Widget* Probe::find(std::string_view id) const {
		for (const auto& child : mRoot.children()) {
			if (!child) continue;
			if (Widget* found = search(*child, id)) return found;
		}
		return nullptr;
	}

	Widget* Probe::require(std::string_view id, const char* what) {
		Widget* widget = find(id);
		if (!widget) {
			mError = "no widget called '" + std::string(id) + "'";
			RDA_LOG_WARNING("Probe: " << mError);
			return nullptr;
		}
		mError = "'" + std::string(id) + "' is not a widget you can " + what;
		return widget;
	}

	size_t Probe::settle() { return bindings().applyDirty(); }

	bool Probe::click(std::string_view id) {
		Widget* widget = require(id, "click");
		if (!widget) return false;
		if (auto* button = dynamic_cast<Button*>(widget)) {
			if (!button->onClick) {
				mError = "'" + std::string(id) + "' has no onClick";
				return false;
			}
			button->onClick();
			settle();
			mError.clear();
			return true;
		}
		RDA_LOG_WARNING("Probe: " << mError);
		return false;
	}

	bool Probe::set(std::string_view id, bool value) {
		Widget* widget = require(id, "tick");
		if (!widget) return false;
		if (auto* box = dynamic_cast<Checkbox*>(widget)) {
			box->value = value;
			if (box->onChange) box->onChange(value);
			settle();
			mError.clear();
			return true;
		}
		RDA_LOG_WARNING("Probe: " << mError);
		return false;
	}

	bool Probe::set(std::string_view id, double value) {
		Widget* widget = require(id, "drag");
		if (!widget) return false;
		if (auto* slider = dynamic_cast<Slider*>(widget)) {
			slider->value = static_cast<float>(value);
			if (slider->onChange) slider->onChange(slider->value);
			settle();
			mError.clear();
			return true;
		}
		RDA_LOG_WARNING("Probe: " << mError);
		return false;
	}

	bool Probe::type(std::string_view id, std::string text) {
		Widget* widget = require(id, "type into");
		if (!widget) return false;
		if (auto* field = dynamic_cast<TextField*>(widget)) {
			field->setText(text);
			if (field->onChange) field->onChange(text);
			settle();
			mError.clear();
			return true;
		}
		RDA_LOG_WARNING("Probe: " << mError);
		return false;
	}

	bool Probe::choose(std::string_view id, std::string value) {
		Widget* widget = require(id, "choose from");
		if (!widget) return false;
		auto* select = dynamic_cast<Select*>(widget);
		if (!select) {
			RDA_LOG_WARNING("Probe: " << mError);
			return false;
		}
		// By an option's value, and only one this select actually offers -- choosing
		// something that is not in the list is what a test is trying to catch, not do.
		for (const auto& child : select->children()) {
			const Option* option = dynamic_cast<const Option*>(child.get());
			if (!option || option->value != value) continue;
			select->value = value;
			if (select->onChange) select->onChange(value);
			settle();
			mError.clear();
			return true;
		}
		mError = "'" + std::string(id) + "' has no option with the value '" + value + "'";
		RDA_LOG_WARNING("Probe: " << mError);
		return false;
	}

	bool Probe::showTab(std::string_view id, int index) {
		Widget* widget = require(id, "switch tabs on");
		if (!widget) return false;
		auto* tabs = dynamic_cast<Tabs*>(widget);
		if (!tabs) {
			RDA_LOG_WARNING("Probe: " << mError);
			return false;
		}
		int count = 0;
		for (const auto& child : tabs->children()) {
			if (dynamic_cast<const Tab*>(child.get())) ++count;
		}
		if (index < 0 || index >= count) {
			mError = "'" + std::string(id) + "' has no page " + std::to_string(index);
			RDA_LOG_WARNING("Probe: " << mError);
			return false;
		}
		tabs->value = index;
		if (tabs->onChange) tabs->onChange(index);
		settle();
		mError.clear();
		return true;
	}

	std::string Probe::text(std::string_view id) const {
		const Widget* widget = find(id);
		if (!widget) return {};
		if (auto* label = dynamic_cast<const Label*>(widget))   return label->text;
		if (auto* button = dynamic_cast<const Button*>(widget)) return button->text;
		if (auto* box = dynamic_cast<const Checkbox*>(widget))  return box->label;
		if (auto* field = dynamic_cast<const TextField*>(widget)) return field->text();
		if (auto* select = dynamic_cast<const Select*>(widget)) return select->value;
		if (auto* option = dynamic_cast<const Option*>(widget)) return option->text;
		if (auto* tab = dynamic_cast<const Tab*>(widget))       return tab->title;
		return {};
	}

	bool Probe::shown(std::string_view id) const {
		// A widget inside a hidden container is not on screen however visible it says it
		// is, and `visible={() => state.expanded}` on a parent is the usual way a layout
		// hides a group -- so this is the question a test normally means.
		const Widget* widget = find(id);
		while (widget) {
			if (!widget->visible) return false;
			if (widget == &mRoot) break;
			widget = widget->parent();
		}
		return widget != nullptr;
	}
}
