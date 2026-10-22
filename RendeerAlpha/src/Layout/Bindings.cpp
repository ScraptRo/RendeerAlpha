#include <Layout/Bindings.h>
#include <GraphicalObjects/Motion.h>
#include <GraphicalObjects/Widget.h>
#include <Layout/ListView.h>
#include <Layout/TreeView.h>
#include <Layout/GridView.h>
#include <Layout/ScriptHost.h>
#include <Core/Tables.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <cmath>

namespace RDA::Layout {

	// Never destroyed, on purpose.
	//
	// An application holds its LayoutHost or Router as a global -- the documented shape,
	// and the natural one -- and a global is destroyed after main returns, in the same
	// static-destruction pass as this runtime. The order between the two is defined, and
	// wrong: a function-local static is destroyed in reverse order of construction, and
	// this one is first constructed inside onStart, so it dies *before* the application's
	// global. That global's destructor then releases its bindings into a vector that has
	// already been freed. MSVC's freed vector happens to read as empty; glibc's does not,
	// and the first Linux run of a real application segfaulted after its last frame with
	// a log that looked perfectly clean.
	//
	// One object of process lifetime, leaked at process exit, costs nothing -- and it
	// gives every path that reaches this table from a destructor the one property such a
	// table must have. signals(), commands() and tables() do the same, for the same
	// reason: release() reaches the first through this one.
	BindingRuntime& bindings() {
		static BindingRuntime* runtime = new BindingRuntime;
		return *runtime;
	}

	namespace {
		// A size property accepts the same vocabulary from an expression as it does from a
		// constant: a number is a fixed size, the words mean what they mean elsewhere.
		bool applySize(SizeSpec& spec, const Value& value) {
			// The same words, read by the same parser: `width="content-23"` and
			// `width={() => `content-${state.gap}`}` are one vocabulary, and a template
			// literal is how an offset comes to be computed at all.
			if (value.kind == Value::Kind::Text) return SizeSpec::parse(value.text, spec);
			spec = SizeSpec::fixed(static_cast<float>(value.asNumber()));
			return true;
		}

		// The same words the loader accepts, so a bound alignment and a constant one mean
		// the same thing.
		// The word may carry a nudge: "center+20". Split first, so the words below are
		// compared against the word alone.
		Alignment alignFrom(std::string_view text, Alignment fallback) {
			float offset = 0.0f;
			const std::string_view word = splitAlignOffset(text, offset);
			const auto with = [offset](Align a) { return Alignment{ a, offset }; };
			if (word == "stretch") return with(Align::Stretch);
			if (word == "start")   return with(Align::Start);
			if (word == "center")  return with(Align::Center);
			if (word == "end")     return with(Align::End);
			return fallback;
		}
		// "below" | "above" | "right" | "left" | "over", for a <popup>.
		Popup::Placement popupPlacementFrom(std::string_view word, Popup::Placement fallback) {
			if (word == "below") return Popup::Placement::Below;
			if (word == "above") return Popup::Placement::Above;
			if (word == "right") return Popup::Placement::Right;
			if (word == "left")  return Popup::Placement::Left;
			if (word == "over")  return Popup::Placement::Over;
			return fallback;
		}

		Distribute spreadFrom(std::string_view word, Distribute fallback) {
			if (word == "spaceBetween") return Distribute::SpaceBetween;
			if (word == "spaceAround")  return Distribute::SpaceAround;
			return fallback;
		}
	}

	bool applyBoundValue(RDA::Widget& widget, const std::string& property, const Value& value) {
		// Properties every widget has.
		if (property == "visible")      { widget.visible = value.truthy(); return true; }
		if (property == "animate")      { widget.animateMs = static_cast<float>(value.asNumber()); return true; }
		if (property == "x")            { widget.rect.x = static_cast<float>(value.asNumber()); return true; }
		if (property == "y")            { widget.rect.y = static_cast<float>(value.asNumber()); return true; }
		if (property == "w")            { widget.rect.w = static_cast<float>(value.asNumber()); return true; }
		if (property == "h")            { widget.rect.h = static_cast<float>(value.asNumber()); return true; }
		if (property == "marginRight")  { widget.marginRight = static_cast<float>(value.asNumber()); return true; }
		if (property == "marginBottom") { widget.marginBottom = static_cast<float>(value.asNumber()); return true; }
		if (property == "width")        { return applySize(widget.width, value); }
		if (property == "height")       { return applySize(widget.height, value); }
		// A child's own answer for an axis, which every widget has and any of them may
		// compute: `hAlignSelf={() => state.narrow ? "center" : "start"}`.
		if (property == "dragWindow")   { widget.dragWindow = value.truthy(); return true; }
		if (property == "maximizeButton") { widget.maximizeButton = value.truthy(); return true; }
		// Only on the widgets the schema gives it to, so answering it here for all of them
		// costs nothing -- and a keymap the application keeps in state is a binding.
		if (property == "shortcut" || property == "keys") { widget.setShortcut(value.asText()); return true; }
		if (property == "alt")          { widget.alt = value.asText(); return true; }
		if (property == "live") {
			const std::string word = value.asText();
			widget.live = word == "assertive" ? 2 : word == "polite" ? 1 : 0;
			return true;
		}
		if (property == "drag")         { widget.drag = value.asText(); return true; }
		if (property == "dragKind")     { widget.dragKind = value.asText(); return true; }
		if (property == "accepts")      { widget.accepts = value.asText(); return true; }
		if (property == "route")        { widget.route = value.asText(); return true; }
		if (property == "pace")         { widget.pace = paceFrom(value.asText(), widget.pace); return true; }
		if (property == "hAlignSelf")   { widget.hAlignSelf = alignFrom(value.text, widget.hAlignSelf); return true; }
		if (property == "vAlignSelf")   { widget.vAlignSelf = alignFrom(value.text, widget.vAlignSelf); return true; }
		if (property == "minWidth")     { widget.width.min = static_cast<float>(value.asNumber()); return true; }
		if (property == "maxWidth")     { widget.width.max = static_cast<float>(value.asNumber()); return true; }
		if (property == "minHeight")    { widget.height.min = static_cast<float>(value.asNumber()); return true; }
		if (property == "maxHeight")    { widget.height.max = static_cast<float>(value.asNumber()); return true; }

		// Properties a particular widget has. The dispatch mirrors the loader's, because
		// it is answering the same question about the same set of names.
		if (auto* w = dynamic_cast<Label*>(&widget)) {
			if (property == "text")    { w->text = value.asText(); return true; }
			if (property == "wrap")    { w->wrap = value.truthy(); return true; }
			if (property == "spans")   { w->spans = value.asText(); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Button*>(&widget)) {
			if (property == "text")     { w->text = value.asText(); return true; }
			if (property == "padding")  { w->padding = static_cast<float>(value.asNumber()); return true; }
			if (property == "selected") { w->selected = value.truthy(); return true; }
			if (property == "variant")  { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Stream*>(&widget)) {
			if (property == "name")    { w->name = value.asText(); return true; }
			if (property == "fit")     {
				w->fit = std::string_view(value.text) == "stretch" ? Stream::Fit::Stretch
				                                                   : Stream::Fit::Contain;
				return true;
			}
		} else if (auto* w = dynamic_cast<Image*>(&widget)) {
			if (property == "src")     { w->source = value.asText(); return true; }
			if (property == "tint")    {
				uint32_t parsed = 0;
				if (parseColor(value.asText().c_str(), parsed)) w->tint = parsed;
				return true;
			}
		} else if (auto* w = dynamic_cast<Tabs*>(&widget)) {
			if (property == "value")   { w->value = static_cast<int>(value.asNumber()); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Select*>(&widget)) {
			if (property == "value")       { w->value = value.asText(); return true; }
			if (property == "placeholder") { w->placeholder = value.asText(); return true; }
			if (property == "of")          { w->of = value.asText(); return true; }
			if (property == "textColumn")  { w->textColumn = value.asText(); return true; }
			if (property == "valueColumn") { w->valueColumn = value.asText(); return true; }
			if (property == "variant")     { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Option*>(&widget)) {
			if (property == "text")    { w->text = value.asText(); return true; }
			if (property == "value")   { w->value = value.asText(); return true; }
		} else if (auto* w = dynamic_cast<Tab*>(&widget)) {
			if (property == "title")   { w->title = value.asText(); return true; }
		} else if (auto* w = dynamic_cast<Checkbox*>(&widget)) {
			if (property == "label")   { w->label = value.asText(); return true; }
			if (property == "value")   { w->value = value.truthy(); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Slider*>(&widget)) {
			if (property == "value")   { w->value = static_cast<float>(value.asNumber()); return true; }
			if (property == "min")     { w->minValue = static_cast<float>(value.asNumber()); return true; }
			if (property == "max")     { w->maxValue = static_cast<float>(value.asNumber()); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<NumberField*>(&widget)) {
			if (property == "label")    { w->label = value.asText(); return true; }
			if (property == "value")    { w->value = value.asNumber(); return true; }
			if (property == "step")     { w->step = value.asNumber(); return true; }
			if (property == "min")      { w->minValue = value.asNumber(); return true; }
			if (property == "max")      { w->maxValue = value.asNumber(); return true; }
			if (property == "decimals") { w->decimals = static_cast<int>(value.asNumber()); return true; }
			if (property == "variant")  { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<TextField*>(&widget)) {
			if (property == "text")        { w->setText(value.asText()); return true; }
			if (property == "placeholder") { w->setPlaceholder(value.asText()); return true; }
			if (property == "suggestion")  { w->setSuggestion(value.asText()); return true; }
			if (property == "language")    { w->language = value.asText(); return true; }
			if (property == "submitKey")   { w->submitKey = submitFrom(value.text, w->submitKey); return true; }
			if (property == "variant")     { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<Stack*>(&widget)) {
			if (property == "arrange")  { w->vertical = value.text != "horizontal"; return true; }
			if (property == "spacing")  { w->spacing = static_cast<float>(value.asNumber()); return true; }
			if (property == "padding")  { w->padding = static_cast<float>(value.asNumber()); return true; }
			if (property == "hAlign")   { w->hAlign = alignFrom(value.text, w->hAlign); return true; }
			if (property == "vAlign")   { w->vAlign = alignFrom(value.text, w->vAlign); return true; }
			if (property == "spread")   { w->spread = spreadFrom(value.text, w->spread); return true; }
		} else if (auto* w = dynamic_cast<ListView*>(&widget)) {
			if (auto* tree = dynamic_cast<TreeView*>(&widget)) {
				if (property == "selected")  { tree->selected = value.asText(); return true; }
				if (property == "selectedKeys") { tree->setSelectedKeys(value.asText()); return true; }
				if (property == "multiSelect")  { tree->multiSelect = value.truthy(); return true; }
				if (property == "openDepth") { tree->openDepth = static_cast<int>(value.asNumber()); return true; }
				if (property == "indent")    { tree->indent = static_cast<float>(value.asNumber()); return true; }
			}
			if (property == "follow")    { w->follow = value.truthy(); return true; }
			if (property == "revealRow") { w->revealRow = static_cast<int>(value.asNumber()); return true; }
			if (property == "variant") { w->variant = Variant(std::string_view(value.text)); return true; }
			if (property == "sortBy")         { w->sortBy = value.asText(); return true; }
			if (property == "sortDescending") { w->sortDescending = value.truthy(); return true; }
			if (property == "filter")         { w->filter = value.asText(); return true; }
			if (property == "filterColumns")  { w->filterColumns = value.asText(); return true; }
		} else if (auto* w = dynamic_cast<GridView*>(&widget)) {
			if (property == "selected")       { w->selected = value.asText(); return true; }
			if (property == "selectedKeys")   { w->setSelectedKeys(value.asText()); return true; }
			if (property == "multiSelect")    { w->multiSelect = value.truthy(); return true; }
			if (property == "sortBy")         { w->sortBy = value.asText(); return true; }
			if (property == "sortDescending") { w->sortDescending = value.truthy(); return true; }
			if (property == "filter")         { w->filter = value.asText(); return true; }
			if (property == "filterColumns")  { w->filterColumns = value.asText(); return true; }
			if (property == "rowHeight")      { w->rowHeight = static_cast<float>(value.asNumber()); return true; }
			if (property == "variant")        { w->variant = Variant(std::string_view(value.text)); return true; }
		} else if (auto* w = dynamic_cast<GridColumn*>(&widget)) {
			if (property == "title")    { w->title = value.asText(); return true; }
			if (property == "decimals") { w->decimals = static_cast<int>(value.asNumber()); return true; }
			if (property == "sortable") { w->sortable = value.truthy(); return true; }
		} else if (auto* w = dynamic_cast<Popup*>(&widget)) {
			if (property == "open")      { w->open = value.truthy(); return true; }
			if (property == "anchor")    { w->anchor = value.asText(); return true; }
			if (property == "blocking")  { w->blocking = value.truthy(); return true; }
			if (property == "gap")       { w->gap = static_cast<float>(value.asNumber()); return true; }
			if (property == "padding")   { w->padding = static_cast<float>(value.asNumber()); return true; }
			if (property == "placement") { w->placement = popupPlacementFrom(value.text, w->placement); return true; }
			if (property == "variant")   { w->variant = Variant(std::string_view(value.text)); return true; }
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
		binding.script    = binding.program.usesScript();
		return place(std::move(binding));
	}

	ObserverId BindingRuntime::place(Binding binding) {
		// Ids are handed out densely and never reused, so Signals can index its own
		// bookkeeping by them without a map -- and it is Signals that hands them out, since
		// a computed signal observes too.
		const ObserverId id = signals().newObserver();
		if (id >= mBindings.size()) mBindings.resize(static_cast<size_t>(id) + 1);
		mBindings[id] = std::move(binding);
		++mLive;

		for (const uint32_t signal : mBindings[id].signalIds) {
			signals().observe(signal, id);
		}
		return id;
	}

	ObserverId BindingRuntime::addRowRefresh(std::vector<uint32_t> signalIds, ListView* list) {
		Binding binding;
		binding.signalIds = std::move(signalIds);
		binding.refresh   = list;
		binding.live      = true;
		return place(std::move(binding));
	}

	void BindingRuntime::watch(ObserverId observer, uint32_t signal) {
		if (observer >= mBindings.size() || !mBindings[observer].live) return;
		signals().observe(signal, observer);
	}

	void BindingRuntime::rewatch(ObserverId id, Binding& binding, const std::vector<uint32_t>& read,
	                             const std::vector<std::pair<uint32_t, uint64_t>>& tablesRead) {
		auto isStatic = [&](uint32_t s) {
			return std::find(binding.signalIds.begin(), binding.signalIds.end(), s) != binding.signalIds.end();
		};
		// Dropped: what it read last time and not this time -- a branch not taken now.
		for (const uint32_t old : binding.dynamic) {
			if (std::find(read.begin(), read.end(), old) == read.end() && !isStatic(old))
				signals().unobserve(old, id);
		}
		std::vector<uint32_t> now;
		for (const uint32_t s : read) {
			if (isStatic(s)) continue;
			now.push_back(s);
			signals().observe(s, id);   // a second edge to the same observer is not added
		}
		binding.dynamic = std::move(now);
		binding.tableReads = tablesRead;
		if (!binding.tableReads.empty() &&
		    std::find(mTableWatchers.begin(), mTableWatchers.end(), id) == mTableWatchers.end())
			mTableWatchers.push_back(id);
	}

	void BindingRuntime::remove(ObserverId observer) {
		if (observer >= mBindings.size() || !mBindings[observer].live) return;
		mTableWatchers.erase(std::remove(mTableWatchers.begin(), mTableWatchers.end(), observer),
		                     mTableWatchers.end());
		signals().forget(observer);
		mBindings[observer] = Binding{}; // frees the program; leaves the slot as a hole
		--mLive;
	}

	bool BindingRuntime::applyOne(Binding& binding) {
		if (!binding.live) return false;
		// The refresh kind evaluates nothing: the list re-binds its own rows, which is
		// where the row is known.
		if (binding.refresh) { binding.refresh->invalidateRows(); return true; }
		if (!binding.target) return false;

		if (binding.script) {
			// Watched while it runs: what the function reads is what this binding depends
			// on now, which may not be what it depended on last time.
			const ObserverId id = static_cast<ObserverId>(&binding - mBindings.data());
			const ScriptHost::Frame frame(ScriptHost::Mode::Value);
			const EvalResult result = evaluate(binding.program, signals(), binding.signalIds);
			rewatch(id, binding, frame.signals(), frame.tables());
			if (!result.ok) {
				// A function that threw is kept, with the last value it produced still on
				// screen: the next thing it reads may well be one it can handle.
				if (result.scriptError) {
					if (result.error != binding.lastError) {
						binding.lastError = result.error;
						RDA_LOG_WARNING("binding: " << binding.property << " on '"
						                << binding.target->id() << "' - " << result.error);
					}
					return false;
				}
				RDA_LOG_WARNING("binding: " << binding.property << " - " << result.error);
				binding.live = false;
				return false;
			}
			binding.lastError.clear();
			if (!applyBoundValue(*binding.target, binding.property, result.value)) {
				RDA_LOG_WARNING("binding: nothing named '" << binding.property
				                << "' on this widget to drive");
				binding.live = false;
				return false;
			}
			return true;
		}

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

	bool BindingRuntime::applyNow(ObserverId observer) {
		if (observer >= mBindings.size()) return false;
		return applyOne(mBindings[observer]);
	}

	size_t BindingRuntime::applyAll() {
		size_t applied = 0;
		for (Binding& binding : mBindings) {
			if (applyOne(binding)) ++applied;
		}
		return applied;
	}

	size_t BindingRuntime::applyDirty() {
		// Bindings whose functions read a table: a table has no observers, so whether one
		// moved is asked here -- once, of the sum of every table's version, on a frame
		// where nothing changed, and binding by binding only when something did.
		std::vector<ObserverId> fromTables;
		if (!mTableWatchers.empty()) {
			const uint64_t revision = tables().revision();
			if (revision != mSeenTables) {
				mSeenTables = revision;
				for (const ObserverId id : mTableWatchers) {
					if (id >= mBindings.size() || !mBindings[id].live) continue;
					for (const auto& [table, version] : mBindings[id].tableReads) {
						if (!tables().valid(table) || tables().at(table).version() != version) {
							fromTables.push_back(id);
							break;
						}
					}
				}
			}
		}

		const std::vector<ObserverId>& dirty = signals().dirty();
		if (dirty.empty() && fromTables.empty()) return 0;

		size_t applied = 0;
		// Copied because applying can write a signal — a handler's effect landing on a
		// bound property — which would otherwise grow the list being walked.
		std::vector<ObserverId> pending = dirty;
		signals().clearDirty();
		for (const ObserverId id : fromTables) {
			if (std::find(pending.begin(), pending.end(), id) == pending.end()) pending.push_back(id);
		}

		for (const ObserverId observer : pending) {
			if (observer >= mBindings.size()) continue;
			if (applyOne(mBindings[observer])) ++applied;
		}
		return applied;
	}
}
