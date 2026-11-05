#include "Kinds.h"
#include "KindSupport.h"
#include <iterator>

// The widgets a person reads and operates: <label>, <button>, <checkbox>, <shortcut>,
// <slider>, <number>, <textfield>, <select> and <option>.
namespace RDA::Layout::Kinds {

	namespace {
		// A keymap the application keeps in state is a binding like any other.
		void setShortcut(Widget& w, const Value& v) { w.setShortcut(v.asText()); }

		// ---- label -------------------------------------------------------------------
		std::unique_ptr<Widget> makeLabel(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<Label>(id, std::string(bp.text(node, "text")));
		}
		void constLabel(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Label& w = as<Label>(widget);
			variantFrom(bp, node, w.variant);
			w.wrap = bp.boolean(node, "wrap", false);
			w.spans = std::string(bp.text(node, "spans"));
			w.hAlign = textAlignFrom(bp.text(node, "hAlign"), w.hAlign, w.hAlignOffset);
			w.vAlign = textAlignFrom(bp.text(node, "vAlign"), w.vAlign, w.vAlignOffset);
		}
		constexpr BoundProp kLabelProps[] = {
			{ "text",    [](Widget& w, const Value& v) { as<Label>(w).text = v.asText(); } },
			{ "wrap",    [](Widget& w, const Value& v) { as<Label>(w).wrap = v.truthy(); } },
			{ "spans",   [](Widget& w, const Value& v) { as<Label>(w).spans = v.asText(); } },
			{ "hAlign",  [](Widget& w, const Value& v) { Label& l = as<Label>(w); l.hAlign = textAlignFrom(v.text, l.hAlign, l.hAlignOffset); } },
			{ "vAlign",  [](Widget& w, const Value& v) { Label& l = as<Label>(w); l.vAlign = textAlignFrom(v.text, l.vAlign, l.vAlignOffset); } },
			{ "variant", [](Widget& w, const Value& v) { as<Label>(w).variant = Variant(std::string_view(v.text)); } },
		};

		// ---- button ------------------------------------------------------------------
		std::unique_ptr<Widget> makeButton(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<Button>(id, std::string(bp.text(node, "text")));
		}
		void constButton(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Button& w = as<Button>(widget);
			variantFrom(bp, node, w.variant);
			w.hAlign = textAlignFrom(bp.text(node, "hAlign"), w.hAlign, w.hAlignOffset);
			w.padding = bp.number(node, "padding", w.padding);
			w.selected = bp.boolean(node, "selected", false);
			w.setShortcut(bp.text(node, "shortcut"));
		}
		constexpr BoundProp kButtonProps[] = {
			{ "text",     [](Widget& w, const Value& v) { as<Button>(w).text = v.asText(); } },
			{ "padding",  [](Widget& w, const Value& v) { as<Button>(w).padding = static_cast<float>(v.asNumber()); } },
			{ "hAlign",   [](Widget& w, const Value& v) { Button& b = as<Button>(w); b.hAlign = textAlignFrom(v.text, b.hAlign, b.hAlignOffset); } },
			{ "selected", [](Widget& w, const Value& v) { as<Button>(w).selected = v.truthy(); } },
			{ "variant",  [](Widget& w, const Value& v) { as<Button>(w).variant = Variant(std::string_view(v.text)); } },
			{ "shortcut", &setShortcut },
		};
		constexpr EventSlot kButtonEvents[] = {
			{ "onClick", [](Widget& w, EventRunner run) { as<Button>(w).onClick = plain(std::move(run)); } },
		};

		// ---- checkbox ----------------------------------------------------------------
		std::unique_ptr<Widget> makeCheckbox(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<Checkbox>(id, std::string(bp.text(node, "label")));
		}
		void constCheckbox(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Checkbox& w = as<Checkbox>(widget);
			variantFrom(bp, node, w.variant);
			w.value = bp.boolean(node, "value", false);
			w.setShortcut(bp.text(node, "shortcut"));
		}
		constexpr BoundProp kCheckboxProps[] = {
			{ "label",    [](Widget& w, const Value& v) { as<Checkbox>(w).label = v.asText(); } },
			{ "value",    [](Widget& w, const Value& v) { as<Checkbox>(w).value = v.truthy(); } },
			{ "variant",  [](Widget& w, const Value& v) { as<Checkbox>(w).variant = Variant(std::string_view(v.text)); } },
			{ "shortcut", &setShortcut },
		};
		constexpr EventSlot kCheckboxEvents[] = {
			{ "onChange", [](Widget& w, EventRunner run) { as<Checkbox>(w).onChange = withBool(std::move(run)); } },
		};

		// ---- shortcut ----------------------------------------------------------------
		std::unique_ptr<Widget> makeShortcut(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<ShortcutKey>(id);
		}
		void constShortcut(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			widget.setShortcut(bp.text(node, "keys"));
		}
		constexpr BoundProp kShortcutProps[] = {
			{ "keys", &setShortcut },
		};
		constexpr EventSlot kShortcutEvents[] = {
			{ "onPress", [](Widget& w, EventRunner run) { as<ShortcutKey>(w).onPress = plain(std::move(run)); } },
		};

		// ---- slider ------------------------------------------------------------------
		std::unique_ptr<Widget> makeSlider(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Slider>(id);
		}
		void constSlider(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Slider& w = as<Slider>(widget);
			variantFrom(bp, node, w.variant);
			w.minValue = bp.number(node, "min", 0.0f);
			w.maxValue = bp.number(node, "max", 1.0f);
			w.value    = bp.number(node, "value", 0.0f);
		}
		constexpr BoundProp kSliderProps[] = {
			{ "value",   [](Widget& w, const Value& v) { as<Slider>(w).value = static_cast<float>(v.asNumber()); } },
			{ "min",     [](Widget& w, const Value& v) { as<Slider>(w).minValue = static_cast<float>(v.asNumber()); } },
			{ "max",     [](Widget& w, const Value& v) { as<Slider>(w).maxValue = static_cast<float>(v.asNumber()); } },
			{ "variant", [](Widget& w, const Value& v) { as<Slider>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kSliderEvents[] = {
			{ "onChange", [](Widget& w, EventRunner run) { as<Slider>(w).onChange = withNumber(std::move(run)); } },
		};

		// ---- number ------------------------------------------------------------------
		std::unique_ptr<Widget> makeNumber(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<NumberField>(id, std::string(bp.text(node, "label")));
		}
		void constNumber(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			NumberField& w = as<NumberField>(widget);
			variantFrom(bp, node, w.variant);
			w.value    = bp.number(node, "value", static_cast<float>(w.value));
			w.step     = bp.number(node, "step", static_cast<float>(w.step));
			w.minValue = bp.number(node, "min", static_cast<float>(w.minValue));
			w.maxValue = bp.number(node, "max", static_cast<float>(w.maxValue));
			w.decimals = static_cast<int>(bp.number(node, "decimals", static_cast<float>(w.decimals)));
		}
		constexpr BoundProp kNumberProps[] = {
			{ "label",    [](Widget& w, const Value& v) { as<NumberField>(w).label = v.asText(); } },
			{ "value",    [](Widget& w, const Value& v) { as<NumberField>(w).value = v.asNumber(); } },
			{ "step",     [](Widget& w, const Value& v) { as<NumberField>(w).step = v.asNumber(); } },
			{ "min",      [](Widget& w, const Value& v) { as<NumberField>(w).minValue = v.asNumber(); } },
			{ "max",      [](Widget& w, const Value& v) { as<NumberField>(w).maxValue = v.asNumber(); } },
			{ "decimals", [](Widget& w, const Value& v) { as<NumberField>(w).decimals = static_cast<int>(v.asNumber()); } },
			{ "variant",  [](Widget& w, const Value& v) { as<NumberField>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kNumberEvents[] = {
			{ "onChange", [](Widget& w, EventRunner run) { as<NumberField>(w).onChange = withNumber(std::move(run)); } },
		};

		// ---- textfield ---------------------------------------------------------------
		std::unique_ptr<Widget> makeTextField(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			const std::string_view mode = bp.text(node, "mode", "line");
			TextFieldMode m = TextFieldMode::Line;
			if (mode == "document") m = TextFieldMode::Document;
			else if (mode == "code") m = TextFieldMode::Code;
			return std::make_unique<TextField>(id, m);
		}
		void constTextField(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			TextField& w = as<TextField>(widget);
			variantFrom(bp, node, w.variant);
			w.setText(std::string(bp.text(node, "text")));
			w.setPlaceholder(std::string(bp.text(node, "placeholder")));
			w.setSuggestion(std::string(bp.text(node, "suggestion")));
			w.language = std::string(bp.text(node, "language"));
			w.submitKey = submitFrom(bp.text(node, "submitKey"), w.submitKey);
		}
		constexpr BoundProp kTextFieldProps[] = {
			{ "text",        [](Widget& w, const Value& v) { as<TextField>(w).setText(v.asText()); } },
			{ "placeholder", [](Widget& w, const Value& v) { as<TextField>(w).setPlaceholder(v.asText()); } },
			{ "suggestion",  [](Widget& w, const Value& v) { as<TextField>(w).setSuggestion(v.asText()); } },
			{ "language",    [](Widget& w, const Value& v) { as<TextField>(w).language = v.asText(); } },
			{ "submitKey",   [](Widget& w, const Value& v) { TextField& f = as<TextField>(w); f.submitKey = submitFrom(v.text, f.submitKey); } },
			{ "variant",     [](Widget& w, const Value& v) { as<TextField>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kTextFieldEvents[] = {
			{ "onChange",  [](Widget& w, EventRunner run) { as<TextField>(w).onChange = withText(std::move(run)); } },
			{ "onCaret",   [](Widget& w, EventRunner run) { as<TextField>(w).onCaret = withNumber(std::move(run)); } },
			{ "onAccept",  [](Widget& w, EventRunner run) { as<TextField>(w).onAccept = plain(std::move(run)); } },
			{ "onDismiss", [](Widget& w, EventRunner run) { as<TextField>(w).onDismiss = plain(std::move(run)); } },
			{ "onSubmit",  [](Widget& w, EventRunner run) { as<TextField>(w).onSubmit = plain(std::move(run)); } },
		};

		// ---- select and option -------------------------------------------------------
		std::unique_ptr<Widget> makeSelect(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Select>(id);
		}
		void constSelect(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Select& w = as<Select>(widget);
			variantFrom(bp, node, w.variant);
			w.value = std::string(bp.text(node, "value"));
			const std::string_view placeholder = bp.text(node, "placeholder");
			if (!placeholder.empty()) w.placeholder = std::string(placeholder);
			w.of = std::string(bp.text(node, "of"));
			const std::string_view textCol = bp.text(node, "textColumn");
			const std::string_view valueCol = bp.text(node, "valueColumn");
			if (!textCol.empty())  w.textColumn = std::string(textCol);
			if (!valueCol.empty()) w.valueColumn = std::string(valueCol);
		}
		constexpr BoundProp kSelectProps[] = {
			{ "value",       [](Widget& w, const Value& v) { as<Select>(w).value = v.asText(); } },
			{ "placeholder", [](Widget& w, const Value& v) { as<Select>(w).placeholder = v.asText(); } },
			{ "of",          [](Widget& w, const Value& v) { as<Select>(w).of = v.asText(); } },
			{ "textColumn",  [](Widget& w, const Value& v) { as<Select>(w).textColumn = v.asText(); } },
			{ "valueColumn", [](Widget& w, const Value& v) { as<Select>(w).valueColumn = v.asText(); } },
			{ "variant",     [](Widget& w, const Value& v) { as<Select>(w).variant = Variant(std::string_view(v.text)); } },
		};
		constexpr EventSlot kSelectEvents[] = {
			{ "onChange", [](Widget& w, EventRunner run) { as<Select>(w).onChange = withText(std::move(run)); } },
		};

		std::unique_ptr<Widget> makeOption(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<Option>(id, std::string(bp.text(node, "text")),
			                                std::string(bp.text(node, "value")));
		}
		constexpr BoundProp kOptionProps[] = {
			{ "text",  [](Widget& w, const Value& v) { as<Option>(w).text = v.asText(); } },
			{ "value", [](Widget& w, const Value& v) { as<Option>(w).value = v.asText(); } },
		};
	}

	const WidgetKind kLabelKind{ "label", nullptr, &makeLabel, &constLabel,
	                             kLabelProps, std::size(kLabelProps) };
	const WidgetKind kButtonKind{ "button", nullptr, &makeButton, &constButton,
	                              kButtonProps, std::size(kButtonProps),
	                              kButtonEvents, std::size(kButtonEvents) };
	const WidgetKind kCheckboxKind{ "checkbox", nullptr, &makeCheckbox, &constCheckbox,
	                                kCheckboxProps, std::size(kCheckboxProps),
	                                kCheckboxEvents, std::size(kCheckboxEvents) };
	const WidgetKind kShortcutKind{ "shortcut", nullptr, &makeShortcut, &constShortcut,
	                                kShortcutProps, std::size(kShortcutProps),
	                                kShortcutEvents, std::size(kShortcutEvents) };
	const WidgetKind kSliderKind{ "slider", nullptr, &makeSlider, &constSlider,
	                              kSliderProps, std::size(kSliderProps),
	                              kSliderEvents, std::size(kSliderEvents) };
	const WidgetKind kNumberKind{ "number", nullptr, &makeNumber, &constNumber,
	                              kNumberProps, std::size(kNumberProps),
	                              kNumberEvents, std::size(kNumberEvents) };
	const WidgetKind kTextFieldKind{ "textfield", nullptr, &makeTextField, &constTextField,
	                                 kTextFieldProps, std::size(kTextFieldProps),
	                                 kTextFieldEvents, std::size(kTextFieldEvents) };
	const WidgetKind kSelectKind{ "select", nullptr, &makeSelect, &constSelect,
	                              kSelectProps, std::size(kSelectProps),
	                              kSelectEvents, std::size(kSelectEvents) };
	const WidgetKind kOptionKind{ "option", nullptr, &makeOption, nullptr,
	                              kOptionProps, std::size(kOptionProps) };
}
