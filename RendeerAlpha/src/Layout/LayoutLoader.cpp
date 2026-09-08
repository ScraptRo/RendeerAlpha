#include <Layout/LayoutLoader.h>
#include <Layout/Bindings.h>
#include <Core/Commands.h>
#include <Core/Tables.h>
#include <Layout/ListView.h>
#include <Layout/WidgetSchema.h>
#include <RendeerAlpha.h>
#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Docking.h>
#include <Logger/Logger.h>
#include <cstdlib>
#include <memory>
#include <vector>

namespace RDA::Layout {

	namespace {
		// A size prop is either a number (fixed), or one of the words a layout container
		// understands. Same vocabulary as the XML components already accept, so the two
		// front ends describe sizing identically rather than nearly identically.
		//
		// `spec` comes in holding whatever the widget set for itself and is only written
		// where the blueprint actually says something, so a widget type that grows its
		// own default sizing keeps it instead of having it overwritten with zero.
		void applySize(const Blueprint& bp, const BlueprintNode& node, std::string_view key,
		               std::string_view minKey, std::string_view maxKey, SizeSpec& spec) {
			if (const BlueprintProp* p = bp.find(node, key)) {
				if (p->kind == static_cast<uint32_t>(PropKind::String)) {
					// A word this build does not understand leaves the size alone, the
					// same answer an unknown alignment gets.
					SizeSpec::parse(bp.string(p->text), spec);
				} else {
					spec = SizeSpec::fixed(p->number);
				}
			}
			spec.min = bp.number(node, minKey, spec.min);
			spec.max = bp.number(node, maxKey, spec.max);
		}

		// A word this build does not know leaves the value alone, so a layout using a
		// newer alignment still lays out rather than snapping to a default.
		// "vertical" / "horizontal" rather than a boolean: `vertical={false}` makes the
		// reader work out what the other direction is called.
		bool verticalFrom(std::string_view word, bool fallback) {
			if (word == "vertical")   return true;
			if (word == "horizontal") return false;
			return fallback;
		}
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
			// Only a child ever writes this, and it means the container decides.
			if (word == "auto")    return with(Align::Auto);
			return fallback;
		}
		// Text inside a widget's own box, which is a different question from where a
		// container puts its children -- so the same word means the same thing and the
		// vocabulary is the same three, with no "stretch": text is the size it is.
		TextAlign textAlignFrom(std::string_view word, TextAlign fallback) {
			if (word == "start")  return TextAlign::Start;
			if (word == "center") return TextAlign::Center;
			if (word == "end")    return TextAlign::End;
			return fallback;
		}

		// "center+20" on a label: the word, and the nudge written out where it was read.
		TextAlign textAlignFrom(std::string_view text, TextAlign fallback, float& offset) {
			const std::string_view word = splitAlignOffset(text, offset);
			return textAlignFrom(word, fallback);
		}
		DockSide sideFrom(std::string_view word, DockSide fallback) {
			if (word == "floating") return DockSide::Floating;
			if (word == "left")     return DockSide::Left;
			if (word == "right")    return DockSide::Right;
			if (word == "top")      return DockSide::Top;
			if (word == "bottom")   return DockSide::Bottom;
			if (word == "center")   return DockSide::Center;
			return fallback;
		}
		Distribute spreadFrom(std::string_view word, Distribute fallback) {
			if (word == "spaceBetween") return Distribute::SpaceBetween;
			if (word == "spaceAround")  return Distribute::SpaceAround;
			return fallback;
		}

		std::unique_ptr<Widget> makeWidget(const Blueprint& bp, const BlueprintNode& node,
		                                   const std::string& id) {
			const std::string_view type = bp.string(node.type);

			// Widgets whose text is a constructor argument read it here; everything else
			// is applied below, once, for every type.
			if (type == "container") return std::make_unique<Container>(id);
			if (type == "panel")     return std::make_unique<Panel>(id);
			if (type == "stack")     return std::make_unique<Stack>(id);
			if (type == "scroll")    return std::make_unique<Scroll>(id);
			if (type == "splitter")  return std::make_unique<Splitter>(id);
			if (type == "viewport")  return std::make_unique<Viewport>(id);
			if (type == "label")     return std::make_unique<Label>(id, std::string(bp.text(node, "text")));
			if (type == "button")    return std::make_unique<Button>(id, std::string(bp.text(node, "text")));
			if (type == "checkbox")  return std::make_unique<Checkbox>(id, std::string(bp.text(node, "label")));
			if (type == "slider")    return std::make_unique<Slider>(id);
			if (type == "dockspace") return std::make_unique<DockHost>(id);
			if (type == "list")      return std::make_unique<ListView>(id);
			if (type == "image")     return std::make_unique<Image>(id, std::string(bp.text(node, "src")));
			if (type == "tabs")      return std::make_unique<Tabs>(id);
			if (type == "tab")       return std::make_unique<Tab>(id, std::string(bp.text(node, "title")));
			if (type == "select")    return std::make_unique<Select>(id);
			if (type == "option")    return std::make_unique<Option>(id,
				                       std::string(bp.text(node, "text")),
				                       std::string(bp.text(node, "value")));
			if (type == "dock")      return std::make_unique<DockContainer>(id, std::string(bp.text(node, "title")));
			if (type == "textfield") {
				const std::string_view mode = bp.text(node, "mode", "line");
				TextFieldMode m = TextFieldMode::Line;
				if (mode == "document") m = TextFieldMode::Document;
				else if (mode == "code") m = TextFieldMode::Code;
				return std::make_unique<TextField>(id, m);
			}
			return nullptr;
		}

		void applyCommon(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			widget.rect = {
				bp.number(node, "x", 0.0f), bp.number(node, "y", 0.0f),
				bp.number(node, "w", 0.0f), bp.number(node, "h", 0.0f),
			};
			widget.visible      = bp.boolean(node, "visible", true);
			widget.animateMs    = bp.number(node, "animate", 0.0f);
			widget.marginRight  = bp.number(node, "marginRight", 0.0f);
			widget.marginBottom = bp.number(node, "marginBottom", 0.0f);
			widget.hAlignSelf   = alignFrom(bp.text(node, "hAlignSelf"), Alignment{ Align::Auto, 0.0f });
			widget.vAlignSelf   = alignFrom(bp.text(node, "vAlignSelf"), Alignment{ Align::Auto, 0.0f });
			applySize(bp, node, "width",  "minWidth",  "maxWidth",  widget.width);
			applySize(bp, node, "height", "minHeight", "maxHeight", widget.height);

			if (const BlueprintProp* anchor = bp.find(node, "anchor")) {
				if (anchor->kind == static_cast<uint32_t>(PropKind::String)) {
					const std::string_view a = bp.string(anchor->text);
					if      (a == "fill")        widget.anchor = Anchor::Fill;
					else if (a == "stretchX")    widget.anchor = Anchor::StretchX;
					else if (a == "stretchY")    widget.anchor = Anchor::StretchY;
					else if (a == "bottomLeft")  widget.anchor = Anchor::BottomLeft;
					else if (a == "bottomRight") widget.anchor = Anchor::BottomRight;
				}
			}
		}

		// Per-type properties. Kept apart from applyCommon so adding a widget type means
		// touching two obvious places rather than hunting through one long function.
		void applyTyped(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			const std::string_view type = bp.string(node.type);
			const std::string_view variant = bp.text(node, "variant");

			if (auto* w = dynamic_cast<Panel*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
			} else if (auto* w = dynamic_cast<Image*>(&widget)) {
				w->fit = bp.text(node, "fit") == "stretch" ? Image::Fit::Stretch
				                                          : Image::Fit::Contain;
			} else if (auto* w = dynamic_cast<Tabs*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->value = static_cast<int>(bp.number(node, "value", 0.0f));
				w->barHeight = bp.number(node, "barHeight", 0.0f);
			} else if (auto* w = dynamic_cast<Select*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->value = std::string(bp.text(node, "value"));
				const std::string_view placeholder = bp.text(node, "placeholder");
				if (!placeholder.empty()) w->placeholder = std::string(placeholder);
			} else if (auto* w = dynamic_cast<Label*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->wrap = bp.boolean(node, "wrap", false);
				w->hAlign = textAlignFrom(bp.text(node, "hAlign"), w->hAlign, w->hAlignOffset);
				w->vAlign = textAlignFrom(bp.text(node, "vAlign"), w->vAlign, w->vAlignOffset);
			} else if (auto* w = dynamic_cast<Button*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->hAlign = textAlignFrom(bp.text(node, "hAlign"), w->hAlign, w->hAlignOffset);
				w->padding = bp.number(node, "padding", w->padding);
			} else if (auto* w = dynamic_cast<Checkbox*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->value = bp.boolean(node, "value", false);
			} else if (auto* w = dynamic_cast<Slider*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->minValue = bp.number(node, "min", 0.0f);
				w->maxValue = bp.number(node, "max", 1.0f);
				w->value    = bp.number(node, "value", 0.0f);
			} else if (auto* w = dynamic_cast<TextField*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->setText(std::string(bp.text(node, "text")));
			} else if (auto* w = dynamic_cast<Stack*>(&widget)) {
				w->vertical = verticalFrom(bp.text(node, "arrange"), w->vertical);
				w->spacing  = bp.number(node, "spacing", 4.0f);
				w->padding  = bp.number(node, "padding", 0.0f);
				w->hAlign   = alignFrom(bp.text(node, "hAlign"), w->hAlign);
				w->vAlign   = alignFrom(bp.text(node, "vAlign"), w->vAlign);
				w->spread   = spreadFrom(bp.text(node, "spread"), w->spread);
			} else if (auto* w = dynamic_cast<ListView*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->of        = std::string(bp.text(node, "of"));
				w->rowHeight = bp.number(node, "rowHeight", w->rowHeight);
				w->spacing   = bp.number(node, "spacing", w->spacing);
				w->barWidth  = bp.number(node, "barWidth", w->barWidth);
				w->wheelStep = bp.number(node, "wheelStep", w->wheelStep);
			} else if (auto* w = dynamic_cast<Scroll*>(&widget)) {
				if (!variant.empty()) w->variant = Variant(variant);
				w->vertical   = bp.boolean(node, "vertical", w->vertical);
				w->horizontal = bp.boolean(node, "horizontal", w->horizontal);
				w->spacing    = bp.number(node, "spacing", w->spacing);
				w->padding    = bp.number(node, "padding", w->padding);
				w->hAlign     = alignFrom(bp.text(node, "hAlign"), w->hAlign);
				w->barWidth   = bp.number(node, "barWidth", w->barWidth);
				w->wheelStep  = bp.number(node, "wheelStep", w->wheelStep);
			} else if (auto* w = dynamic_cast<DockHost*>(&widget)) {
				w->persist = std::string(bp.text(node, "persist"));
			} else if (auto* w = dynamic_cast<DockContainer*>(&widget)) {
				// `title` was a constructor argument; the rest is where the panel starts,
				// and only until somebody moves it.
				if (!variant.empty()) w->variant = Variant(variant);
				w->dock     = sideFrom(bp.text(node, "side"), w->dock);
				w->dockSize = bp.number(node, "size", w->dockSize);
				w->closable = bp.boolean(node, "closable", w->closable);
			} else if (auto* w = dynamic_cast<Viewport*>(&widget)) {
				std::string_view named = bp.text(node, "name");
				if (!named.empty()) w->name = std::string(named);
			} else if (auto* w = dynamic_cast<Splitter*>(&widget)) {
				w->vertical = verticalFrom(bp.text(node, "arrange"), w->vertical);
				w->minValue = bp.number(node, "min", 40.0f);
				w->maxValue = bp.number(node, "max", 4000.0f);
				w->value    = bp.number(node, "value", 120.0f);
			}
			(void)type;
		}

		// A prop nobody applies is the quietest kind of bug: a misspelled `varaint` looks
		// exactly like a widget that refused to be styled. The schema knows what each
		// widget takes, so the mismatch can be named instead of shrugged at.
		//
		// It is a warning rather than a failure on purpose — a layout compiled against a
		// newer engine should still load here, minus whatever this build has never heard
		// of. Type checking at authoring time is where a typo is meant to be caught; this
		// is the backstop for everything that skipped it.
		// Properties that existed and were renamed. Worth their own message: the generic
		// "has no property" is true but unhelpful, and an old layout that silently lost
		// its alignment is exactly what a rename must not cause.
		std::string_view renamedTo(std::string_view property, std::string_view type) {
			if (property == "align") {
				if (type == "label" || type == "button") return "hAlign";
				return "hAlign or vAlign, whichever axis you meant";
			}
			if (property == "valign")    return "vAlign";
			if (property == "alignSelf") return "hAlignSelf or vAlignSelf";
			// It stopped meaning "where do they sit" when hAlign and vAlign took that
			// over, and a property that only spreads children apart should say so.
			if (property == "justify")   return "spread, and only its spaceBetween and spaceAround; hAlign or vAlign place them";
			return {};
		}

		void reportUnknownProps(const Blueprint& bp, const BlueprintNode& node) {
			const std::string_view type = bp.string(node.type);
			const WidgetDesc* widget = findWidget(type);
			if (!widget) return;

			for (uint32_t i = 0; i < node.propCount; ++i) {
				const std::string_view name = bp.string(bp.prop(node.firstProp + i).key);
				// A signal the layout declared for itself, carried here rather than in a
				// section of its own. Not a property of anything.
				if (name.rfind("!signal ", 0) == 0) continue;
				if (findProp(*widget, name)) continue;
				if (const std::string_view now = renamedTo(name, type); !now.empty()) {
					RDA_LOG_WARNING("layout: <" << type << "> '" << name << "' was renamed to "
					                << now << " (id '" << bp.string(node.id) << "') - ignored. "
					                "Alignment is named for the axis now, so it means the same "
					                "thing whichever way a stack runs");
					continue;
				}
				RDA_LOG_WARNING("layout: <" << type << "> has no property '" << name
				                << "' (id '" << bp.string(node.id) << "') - ignored");
			}
		}
	}

	LayoutInstance& LayoutInstance::operator=(LayoutInstance&& other) noexcept {
		if (this != &other) {
			release();
			mRoot = other.mRoot;
			mObservers = std::move(other.mObservers);
			other.mRoot = nullptr;
			other.mObservers.clear();
		}
		return *this;
	}

	void LayoutInstance::release() {
		for (const ObserverId observer : mObservers) bindings().remove(observer);
		mObservers.clear();
		mRoot = nullptr;
	}

	namespace {
		// The signals a program names, resolved against the table. A name nobody declared
		// is created as a number and reported: silently inventing it would turn a
		// misspelling into a binding that reads zero forever and looks like it works.
		// Every name a program refers to, turned into the id the evaluator indexes by.
		// The pool holds signals and commands together; a command carries a mark, because
		// which registry a name belongs to is otherwise only knowable from the opcode
		// that reads it, and this runs over names rather than code.
		std::vector<uint32_t> resolveNames(const Program& program, const std::string& what,
		                                   Table* rowTable = nullptr) {
			std::vector<uint32_t> ids;
			ids.reserve(program.signals.size());
			for (const std::string& pooled : program.signals) {
				if (!pooled.empty() && pooled.front() == '#') {
					// A column of the row a template shows, resolved to an index now so that
					// scrolling never looks a name up.
					const std::string name = pooled.substr(1);
					const uint32_t column = rowTable ? rowTable->column(name) : kNoColumn;
					if (column == kNoColumn) {
						RDA_LOG_WARNING("layout: " << what << " reads " << name
						                << ", which is not a column of the list's table");
					}
					ids.push_back(column);
					continue;
				}
				if (!pooled.empty() && pooled.front() == '@') {
					const std::string name = pooled.substr(1);
					const uint32_t id = commands().find(name);
					if (id == kNoCommand) {
						// Left unresolved rather than invented: a command that does not
						// exist has no work to do, and the handler says so when pressed.
						RDA_LOG_WARNING("layout: " << what << " calls commands." << name
						                << "(), which the application never declared");
					}
					ids.push_back(id);
					continue;
				}
				uint32_t id = signals().find(pooled);
				if (id == kNoSignal) {
					RDA_LOG_WARNING("layout: " << what << " reads state." << pooled
					                << ", which nothing declared - assuming a number");
					id = signals().define(pooled, 0.0);
				}
				ids.push_back(id);
			}
			return ids;
		}

		// An event handler is a program that runs when the widget says so. It is captured
		// in the widget's own callback rather than registered with the signal graph: it
		// answers to input, not to state, and the widget already owns its lifetime.
		bool attachEvent(Widget& widget, const std::string& event, Program program,
		                 std::vector<uint32_t> ids) {
			// Takes the value the widget passed, so a handler that named a parameter can
			// read it. Null for an event that carries nothing, like a click.
			auto runner = [program = std::move(program), ids = std::move(ids), event]
			              (const Value* passed) {
				const EvalResult result = evaluate(program, signals(), ids, passed);
				if (!result.ok) {
					RDA_LOG_WARNING("handler " << event << ": " << result.error);
					return;
				}
				// The writes this handler made have marked their bindings; asking for a
				// frame is what gets them applied and drawn, since an idle window would
				// otherwise never look again.
				rendeerRequestRedraw();
			};

			if (event == "onClick") {
				if (auto* button = dynamic_cast<Button*>(&widget)) {
					button->onClick = [runner] { runner(nullptr); };
					return true;
				}
			} else if (event == "onChange") {
				if (auto* box = dynamic_cast<Checkbox*>(&widget)) {
					box->onChange = [runner](bool state) {
						const Value passed = Value::fromBool(state);
						runner(&passed);
					};
					return true;
				}
				if (auto* slider = dynamic_cast<Slider*>(&widget)) {
					slider->onChange = [runner](float value) {
						const Value passed = Value::fromNumber(value);
						runner(&passed);
					};
					return true;
				}
				if (auto* field = dynamic_cast<TextField*>(&widget)) {
					field->onChange = [runner](const std::string& text) {
						const Value passed = Value::fromText(text);
						runner(&passed);
					};
					return true;
				}
				if (auto* tabs = dynamic_cast<Tabs*>(&widget)) {
					tabs->onChange = [runner](int index) {
						const Value passed = Value::fromNumber(static_cast<double>(index));
						runner(&passed);
					};
					return true;
				}
				if (auto* select = dynamic_cast<Select*>(&widget)) {
					select->onChange = [runner](std::string chosen) {
						const Value passed = Value::fromText(chosen);
						runner(&passed);
					};
					return true;
				}
			}
			return false;
		}
	}

	// True when a program reads a column, which is what decides where its binding
	// belongs: one that does is the list's business, applied when a row is rebound;
	// one that does not is an ordinary binding on an ordinary widget.
	static bool readsRow(const Program& program) {
		for (const Instruction& instruction : program.code) {
			if (instruction.op == Op::PushRowField) return true;
		}
		return false;
	}

	// One copy of a list's row template. Called once per pooled row, so every widget
	// exists before anything scrolls and none is built while it does.
	static void buildPooledRow(const Blueprint& bp, size_t templateRoot, ListView& list,
	                           const std::string& idPrefix, Table* table,
	                           LayoutInstance& instance) {
		std::vector<Widget*> made(bp.nodeCount(), nullptr);
		std::vector<bool> mine(bp.nodeCount(), false);
		Widget* rowRoot = nullptr;

		for (size_t i = templateRoot; i < bp.nodeCount(); ++i) {
			const BlueprintNode& node = bp.node(i);
			if (i == templateRoot) mine[i] = true;
			else if (node.parent != kNoParent && node.parent < i && mine[node.parent]) mine[i] = true;
			else continue;

			Widget* into = (i == templateRoot) ? &list : made[node.parent];
			if (!into) continue;

			auto widget = makeWidget(bp, node, idPrefix + std::string(bp.string(node.id)));
			if (!widget) continue;
			applyCommon(bp, node, *widget);
			applyTyped(bp, node, *widget);
			made[i] = into->addChild(std::move(widget));
			if (!rowRoot) rowRoot = made[i];
		}

		list.beginRow(rowRoot);

		for (size_t b = 0; b < bp.bindingCount(); ++b) {
			const BlueprintBinding& record = bp.binding(b);
			if (record.node >= mine.size() || !mine[record.node]) continue;
			Widget* target = made[record.node];
			if (!target) continue;

			const std::string property(bp.string(record.key));
			const std::string where = property + " on '" + target->id() + "'";
			Program program = bp.programFor(record);
			std::vector<uint32_t> ids = resolveNames(program, where, table);

			if (record.kind == static_cast<uint32_t>(BindingKind::Event)) {
				if (!attachEvent(*target, property, std::move(program), std::move(ids))) {
					RDA_LOG_WARNING("layout: nothing answers '" << property << "' on '"
					                << target->id() << "'");
				}
				continue;
			}

			if (readsRow(program)) {
				ListView::RowBinding binding;
				binding.program  = std::move(program);
				binding.ids      = std::move(ids);
				binding.target   = target;
				binding.property = property;
				list.addRowBinding(std::move(binding));
			} else {
				// Nothing about the row: an ordinary binding, driven by signals like any
				// other, and it goes on working as the row is reused.
				instance.track(bindings().add(std::move(program), std::move(ids), target, property));
			}
		}
	}

	LayoutInstance instantiate(const Blueprint& blueprint, Widget& parent,
	                           const std::string& idPrefix) {
		LayoutInstance instance;
		if (!blueprint.valid() || blueprint.nodeCount() == 0) return instance;

		// Where each node's widget ended up, indexed by node. Parents are always earlier
		// in the array (parse() checks it), so by the time a child is reached its parent
		// is already in here — which is what makes this one forward pass.
		// Signals the layout declared for itself, defined before any binding resolves a
		// name. define() returns the existing signal when there is one, so a hot reload
		// finds the value the user left rather than the one the file starts with.
		if (blueprint.nodeCount() > 0) {
			const BlueprintNode& root = blueprint.node(0);
			for (uint32_t i = 0; i < root.propCount; ++i) {
				const BlueprintProp& prop = blueprint.prop(root.firstProp + i);
				const std::string_view key = blueprint.string(prop.key);
				if (key.rfind("!signal ", 0) != 0) continue;
				const std::string name(key.substr(8));
				switch (static_cast<PropKind>(prop.kind)) {
				case PropKind::Bool:
					signals().define(name, prop.number != 0.0f);
					break;
				case PropKind::String:
					signals().define(name, blueprint.string(prop.text));
					break;
				default:
					signals().define(name, static_cast<double>(prop.number));
					break;
				}
			}
		}

		std::vector<Widget*> made(blueprint.nodeCount(), nullptr);
		Widget* root = nullptr;
		// Lists, and where each one's template starts, collected as they are met.
		std::vector<std::pair<ListView*, size_t>> templates;

		for (size_t i = 0; i < blueprint.nodeCount(); ++i) {
			const BlueprintNode& node = blueprint.node(i);

			// A node whose parent was skipped is skipped too, which is how an unknown
			// widget takes its subtree with it instead of reparenting it somewhere odd.
			Widget* into = (node.parent == kNoParent) ? &parent : made[node.parent];
			if (!into) continue;

			// A list's child is not a child: it is the template every pooled row is built
			// from. Left out of this pass, and with it everything below it, since a node
			// whose parent was skipped is skipped too.
			if (auto* list = dynamic_cast<ListView*>(into)) {
				templates.emplace_back(list, i);
				continue;
			}

			auto widget = makeWidget(blueprint, node, idPrefix + std::string(blueprint.string(node.id)));
			if (!widget) {
				const std::string_view wanted = blueprint.string(node.type);
				if (wanted == "scrollview") {
					RDA_LOG_WARNING("layout: <scrollview> is now <scroll> (id '"
					                << blueprint.string(node.id) << "'), skipping it and its "
					                "children. One element scrolls: a column of children, "
					                "down by default and sideways with horizontal={true}");
				} else {
					RDA_LOG_WARNING("layout: no widget type named '" << wanted
					                << "' (id '" << blueprint.string(node.id) << "'), skipping it and its children");
				}
				continue;
			}

			applyCommon(blueprint, node, *widget);
			applyTyped(blueprint, node, *widget);
			reportUnknownProps(blueprint, node);

			made[i] = into->addChild(std::move(widget));
			if (!root) root = made[i];
		}
		instance.adopt(root);

		// ---- row templates ---------------------------------------------------------
		// Built after the tree, because a row needs its list to exist and the list needs
		// its own properties applied before anyone asks which table it shows.
		for (const auto& entry : templates) {
			ListView* list = entry.first;
			const size_t templateRoot = entry.second;
			const uint32_t listNode = blueprint.node(templateRoot).parent;

			const uint32_t tableId = tables().find(list->of);
			Table* table = (tableId == kNoTable) ? nullptr : &tables().at(tableId);
			if (!table) {
				RDA_LOG_WARNING("layout: list '" << list->id() << "' shows table '" << list->of
				                << "', which nothing declared");
			}

			// How many copies to keep. Enough for a tall window, because the height is
			// not known here -- a layout can raise it, and the list says so if it runs
			// out rather than leaving a gap nobody can explain.
			int copies = 40;
			if (listNode != kNoParent) {
				const float asked = blueprint.number(blueprint.node(listNode), "poolSize", 0.0f);
				if (asked > 0.0f) copies = static_cast<int>(asked);
			}
			for (int k = 0; k < copies; ++k) {
				buildPooledRow(blueprint, templateRoot, *list,
				               idPrefix + "#" + std::to_string(k) + "/", table, instance);
			}
		}

		// ---- bindings ----------------------------------------------------------------
		for (size_t i = 0; i < blueprint.bindingCount(); ++i) {
			const BlueprintBinding& record = blueprint.binding(i);
			Widget* target = made[record.node];
			if (!target) continue; // its widget was skipped, so its binding has no subject

			const std::string property(blueprint.string(record.key));
			const std::string where = property + " on '" + target->id() + "'";
			Program program = blueprint.programFor(record);
			std::vector<uint32_t> ids = resolveNames(program, where);

			if (record.kind == static_cast<uint32_t>(BindingKind::Event)) {
				if (!attachEvent(*target, property, std::move(program), std::move(ids))) {
					RDA_LOG_WARNING("layout: nothing answers '" << property << "' on '"
					                << target->id() << "'");
				}
				continue;
			}
			instance.track(bindings().add(std::move(program), std::move(ids), target, property));
		}

		return instance;
	}
}
