#include <Layout/Bindings.h>
#include <GraphicalObjects/Widget.h>
#include <Logger/Logger.h>

#include <cmath>

namespace RDA::Layout {

	BindingRuntime& bindings() {
		static BindingRuntime runtime;
		return runtime;
	}

	namespace {
		// A size property accepts the same vocabulary from an expression as it does from a
		// constant: a number is a fixed size, the words mean what they mean elsewhere.
		bool applySize(SizeSpec& spec, const Value& value) {
			if (value.kind == Value::Kind::Text) {
				const std::string word = value.text;
				if (word == "content") { spec = SizeSpec::content(); return true; }
				if (word.rfind("fill", 0) == 0) {
					float weight = 1.0f;
					if (word.size() > 5 && word[4] == ':') {
						weight = static_cast<float>(std::atof(word.c_str() + 5));
						if (weight <= 0.0f) weight = 1.0f;
					}
					spec = SizeSpec::fill(weight);
					return true;
				}
				return false;
			}
			spec = SizeSpec::fixed(static_cast<float>(value.asNumber()));
			return true;
		}
	}

	bool applyBoundValue(RDA::Widget& widget, const std::string& property, const Value& value) {
		// Properties every widget has.
		if (property == "visible")      { widget.visible = value.truthy(); return true; }
		if (property == "x")            { widget.rect.x = static_cast<float>(value.asNumber()); return true; }
		if (property == "y")            { widget.rect.y = static_cast<float>(value.asNumber()); return true; }
		if (property == "w")            { widget.rect.w = static_cast<float>(value.asNumber()); return true; }
		if (property == "h")            { widget.rect.h = static_cast<float>(value.asNumber()); return true; }
		if (property == "marginRight")  { widget.marginRight = static_cast<float>(value.asNumber()); return true; }
		if (property == "marginBottom") { widget.marginBottom = static_cast<float>(value.asNumber()); return true; }
		if (property == "width")        { return applySize(widget.width, value); }
		if (property == "height")       { return applySize(widget.height, value); }
		if (property == "minWidth")     { widget.width.min = static_cast<float>(value.asNumber()); return true; }
		if (property == "maxWidth")     { widget.width.max = static_cast<float>(value.asNumber()); return true; }
		if (property == "minHeight")    { widget.height.min = static_cast<float>(value.asNumber()); return true; }
		if (property == "maxHeight")    { widget.height.max = static_cast<float>(value.asNumber()); return true; }

		// Properties a particular widget has. The dispatch mirrors the loader's, because
		// it is answering the same question about the same set of names.
		if (auto* w = dynamic_cast<Label*>(&widget)) {
			if (property == "text")    { w->text = value.asText(); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Button*>(&widget)) {
			if (property == "text")    { w->text = value.asText(); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Checkbox*>(&widget)) {
			if (property == "label")   { w->label = value.asText(); return true; }
			if (property == "value")   { w->value = value.truthy(); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Slider*>(&widget)) {
			if (property == "value")   { w->value = static_cast<float>(value.asNumber()); return true; }
			if (property == "min")     { w->minValue = static_cast<float>(value.asNumber()); return true; }
			if (property == "max")     { w->maxValue = static_cast<float>(value.asNumber()); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<TextField*>(&widget)) {
			if (property == "text")    { w->text = value.asText(); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Stack*>(&widget)) {
			if (property == "vertical") { w->vertical = value.truthy(); return true; }
			if (property == "spacing")  { w->spacing = static_cast<float>(value.asNumber()); return true; }
			if (property == "padding")  { w->padding = static_cast<float>(value.asNumber()); return true; }
		} else if (auto* w = dynamic_cast<Panel*>(&widget)) {
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		}
		return false;
	}

	ObserverId BindingRuntime::add(Program program, std::vector<uint32_t> signalIds,
	                               Widget* target, std::string property) {
		Binding binding;
		binding.program   = std::move(program);
		binding.signalIds = std::move(signalIds);
		binding.target    = target;
		binding.property  = std::move(property);
		binding.live      = true;

		// Ids are handed out densely and never reused, so Signals can index its own
		// bookkeeping by them without a map.
		const ObserverId id = static_cast<ObserverId>(mBindings.size());
		mBindings.push_back(std::move(binding));
		++mLive;

		for (const uint32_t signal : mBindings.back().signalIds) {
			signals().observe(signal, id);
		}
		return id;
	}

	void BindingRuntime::remove(ObserverId observer) {
		if (observer >= mBindings.size() || !mBindings[observer].live) return;
		signals().forget(observer);
		mBindings[observer] = Binding{}; // frees the program; leaves the slot as a hole
		--mLive;
	}

	bool BindingRuntime::applyOne(Binding& binding) {
		if (!binding.live || !binding.target) return false;

		const EvalResult result = evaluate(binding.program, signals(), binding.signalIds);
		if (!result.ok) {
			RDA_LOG_WARNING("binding: " << binding.property << " - " << result.error);
			// Stopped rather than retried every frame: the program is wrong, and it will
			// still be wrong next frame.
			binding.live = false;
			return false;
		}
		if (!applyBoundValue(*binding.target, binding.property, result.value)) {
			RDA_LOG_WARNING("binding: nothing named '" << binding.property
			                << "' on this widget to drive");
			binding.live = false;
			return false;
		}
		return true;
	}

	size_t BindingRuntime::applyAll() {
		size_t applied = 0;
		for (Binding& binding : mBindings) {
			if (applyOne(binding)) ++applied;
		}
		return applied;
	}

	size_t BindingRuntime::applyDirty() {
		const std::vector<ObserverId>& dirty = signals().dirty();
		if (dirty.empty()) return 0;

		size_t applied = 0;
		// Copied because applying can write a signal — a handler's effect landing on a
		// bound property — which would otherwise grow the list being walked.
		const std::vector<ObserverId> pending = dirty;
		signals().clearDirty();

		for (const ObserverId observer : pending) {
			if (observer >= mBindings.size()) continue;
			if (applyOne(mBindings[observer])) ++applied;
		}
		return applied;
	}
}
