#include <Layout/LayoutLoader.h>
#include <GraphicalObjects/Motion.h>
#include <algorithm>
#include <Layout/Bindings.h>
#include <Core/Commands.h>
#include <Core/Tables.h>
#include <Layout/ListView.h>
#include <Layout/ScriptHost.h>
#include <Layout/Instantiator.h>
#include <Layout/WidgetRegistry.h>
#include <Layout/WidgetSchema.h>
#include <Core/Wake.h>
#include <GraphicalObjects/Widget.h>
#include <Logger/Logger.h>
#include <cstdlib>
#include <memory>
#include <vector>

namespace RDA::Layout {

	LayoutInstance& LayoutInstance::operator=(LayoutInstance&& other) noexcept {
		if (this != &other) {
			release();
			mRoot = other.mRoot;
			mObservers = std::move(other.mObservers);
			other.mRoot = nullptr;
			other.mObservers = std::make_shared<ObserverSink>();
		}
		return *this;
	}

	namespace {
		// Everything that has opened and not yet closed, for the way out. Weak, because the
		// hosts are the application's and one may simply go. Never destroyed, for the reason
		// bindings() is not: a global LayoutHost goes after statics here would have.
		using OpenList = std::vector<std::weak_ptr<LayoutInstance::ObserverSink>>;
		OpenList& openLayouts() {
			static OpenList* open = new OpenList;
			return *open;
		}

		void closeSink(const std::shared_ptr<LayoutInstance::ObserverSink>& sink) {
			if (!sink->announced) return;
			sink->announced = false;
			if (sink->onClose) sink->onClose(nullptr);
		}
	}

	void LayoutInstance::markAnnounced() {
		mObservers->announced = true;
		OpenList& open = openLayouts();
		open.erase(std::remove_if(open.begin(), open.end(),
		                          [](const std::weak_ptr<ObserverSink>& one) { return one.expired(); }),
		           open.end());
		open.push_back(mObservers);
	}

	void LayoutInstance::announceOpen() {
		if (mObservers->announced) return;
		// Held, because a handler can reach a command that replaces this very layout.
		const std::shared_ptr<ObserverSink> sink = mObservers;
		markAnnounced();
		if (sink->onOpen) sink->onOpen(nullptr);
	}

	void LayoutInstance::announceClose() {
		const std::shared_ptr<ObserverSink> sink = mObservers;
		closeSink(sink);
	}

	void closeOpenLayouts() {
		// Taken first: a handler may open something, and this is walking the list.
		const OpenList open = std::move(openLayouts());
		openLayouts().clear();
		for (const std::weak_ptr<LayoutInstance::ObserverSink>& one : open) {
			if (const std::shared_ptr<LayoutInstance::ObserverSink> sink = one.lock()) closeSink(sink);
		}
	}

	void LayoutInstance::release() {
		for (const ObserverId observer : mObservers->ids) bindings().remove(observer);
		mObservers->ids.clear();
		mObservers->open = false;
		// The functions go with the bindings: nothing that runs them is left.
		mObservers->module.reset();
		mObservers = std::make_shared<ObserverSink>();
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
		                                   Table* rowTable = nullptr,
		                                   const ScriptModule* module = nullptr) {
			std::vector<uint32_t> ids;
			ids.reserve(program.signals.size());
			for (const std::string& pooled : program.signals) {
				// The layout's own functions: "$name" is one a grammar binding calls, "%3"
				// a binding compiled to JavaScript whole ("%r3" when it reads the row).
				if (!pooled.empty() && (pooled.front() == '$' || pooled.front() == '%')) {
					uint32_t id = kNoScript;
					if (module) {
						if (pooled.front() == '$') {
							id = module->function(pooled.substr(1));
						} else {
							const size_t digits = pooled.find_first_of("0123456789");
							if (digits != std::string::npos)
								id = module->binding(static_cast<uint32_t>(std::strtoul(pooled.c_str() + digits, nullptr, 10)));
						}
					}
					if (id == kNoScript) {
						RDA_LOG_WARNING("layout: " << what << " runs code this layout's blueprint does not "
						                "carry -- was it compiled by an older engine?");
					}
					ids.push_back(id);
					continue;
				}
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
		// `row` and `slot` are set only for a handler attached inside a <list> row
		// template, and say where to ask which row this copy of the template is showing
		// when the handler actually fires. Everything outside a list passes nullptr and
		// behaves exactly as it did.
		EventRunner makeRunner(Program program, std::vector<uint32_t> ids, std::string event,
		                       ListView* row = nullptr, size_t slot = 0) {
			// Takes the value the widget passed, so a handler that named a parameter can
			// read it. Null for an event that carries nothing, like a click.
			return [program = std::move(program), ids = std::move(ids), event = std::move(event), row, slot]
			       (const Value* passed) {
				// Asked at fire time rather than captured: the slot shows a different row
				// every time the list scrolls, and a handler captured with one of them
				// would act on whatever was under the pointer when the layout loaded.
				RowRef ref;
				const bool haveRow = row && row->rowRefFor(slot, ref);
				// A handler may write state and call commands, whatever is happening
				// around it -- a click that lands while a value binding is being applied
				// is still a click.
				ScriptHost::Frame frame(ScriptHost::Mode::Handler);
				const EvalResult result = evaluate(program, signals(), ids, passed,
				                                   haveRow ? &ref : nullptr);
				if (!result.ok) {
					RDA_LOG_WARNING("handler " << event << ": " << result.error);
					return;
				}
				// The writes this handler made have marked their bindings; asking for a
				// frame is what gets them applied and drawn, since an idle window would
				// otherwise never look again.
				RDA::Wake::requestFrame();
			};
		}

		// A value binding whose property has no setter. Said when the layout loads rather
		// than when the first value arrives, and the binding is not made: there is nothing
		// for it to write. A property the schema knows takes a constant only -- the
		// compiler refuses a binding there, so this is a blueprint from an older one.
		void reportUnbindable(const Blueprint& bp, const BlueprintNode& node,
		                      const std::string& property, const std::string& id) {
			const std::string_view type = bp.string(node.type);
			const WidgetDesc* widget = findWidget(type);
			const PropDesc* prop = widget ? findProp(*widget, property) : nullptr;
			if (prop && prop->constant) {
				RDA_LOG_WARNING("layout: '" << property << "' on '" << id << "' takes a constant: <"
				                << type << "> reads it once, when it is built - the binding is ignored");
				return;
			}
			RDA_LOG_WARNING("layout: nothing named '" << property << "' on '" << id
			                << "' to drive - the binding is ignored");
		}
	}

	// True when a program reads a column, which is what decides where its binding
	// belongs: one that does is the list's business, applied when a row is rebound;
	// one that does not is an ordinary binding on an ordinary widget.
	static bool readsRow(const Program& program) {
		for (const Instruction& instruction : program.code) {
			if (instruction.op == Op::PushRowField) return true;
			// A binding compiled to JavaScript that names the row's parameter.
			if (instruction.op == Op::RunScript && instruction.operand < program.signals.size() &&
			    program.signals[instruction.operand].rfind("%r", 0) == 0) return true;
		}
		return false;
	}

	// The signals a program reads, resolved. A row binding that reads one of these has to
	// be re-applied when it moves, and the row is the only thing that can do that -- so
	// the list observes them and re-binds itself. Deduplicated, because one program
	// reading the same signal twice should not put the list in the dirty list twice.
	static std::vector<uint32_t> signalsReadBy(const Program& program,
	                                           const std::vector<uint32_t>& ids) {
		std::vector<uint32_t> out;
		for (const Instruction& instruction : program.code) {
			const bool reads = instruction.op == Op::LoadSignal ||
			                   instruction.op == Op::StoreSignal;
			if (!reads || instruction.operand >= ids.size()) continue;
			const uint32_t signal = ids[instruction.operand];
			if (std::find(out.begin(), out.end(), signal) == out.end()) out.push_back(signal);
		}
		return out;
	}

	namespace {
		// A list's row bindings, kept by the layout runtime rather than by the list.
		//
		// A row binding cannot be an ordinary one. An ordinary binding knows the single
		// widget it writes; a row binding is one program over a pool of rows, and a signal
		// write has no way to say which row it meant. So the signal graph drives the list,
		// the list says which slot shows which row, and this evaluates that slot's programs
		// against that row -- on the script host, which the list has no business knowing.
		class RowBindingSet : public ListView::RowBinder {
		public:
			struct Binding {
				Program               program;
				std::vector<uint32_t> ids;      // column indices, and signal ids for the rest
				Widget*               target = nullptr;
				std::string           property;
				PropSetter            setter = nullptr;  // found when the template was built
				bool                  warned = false;    // said once, not once per rebind
			};

			explicit RowBindingSet(const ListView& list) : mList(list) {}

			// One entry per pooled row, in the list's slot order.
			std::vector<std::vector<Binding>> slots;

			// An observer the list grows as its rows' JavaScript bindings read signals:
			// those cannot be listed when the layout loads, only seen while they run.
			ObserverId scriptRefresh = kNoObserver;

			void bind(size_t slot, const RowRef& row) override {
				if (slot >= slots.size()) return;
				for (Binding& binding : slots[slot]) {
					if (!binding.target) continue;
					// A binding that runs JavaScript is watched while it runs, and what it
					// read is added to what the list re-binds its rows for.
					const ScriptHost::Frame frame(ScriptHost::Mode::Value);
					const EvalResult result = evaluate(binding.program, signals(), binding.ids,
					                                   nullptr, &row);
					if (scriptRefresh != kNoObserver) {
						for (const uint32_t signal : frame.signals()) bindings().watch(scriptRefresh, signal);
					}
					if (!result.ok) {
						// Once per binding: a list re-binds as it scrolls, and a function
						// that throws on one row will throw on the next forty.
						if (!result.scriptError || !binding.warned) {
							RDA_LOG_WARNING("list " << mList.id() << ": " << binding.property << " - " << result.error);
						}
						if (result.scriptError) binding.warned = true;
						continue;
					}
					// No row binding is kept without a setter; the loader says so when the
					// layout loads -- once, rather than once per row as a pool rebinds.
					binding.setter(*binding.target, result.value);
				}
			}

			std::string text(const RowRef& row) override {
				// The template's text, in the order it was written, evaluated against this
				// row: the first that says anything is the row's name, which is what a
				// person would read.
				if (slots.empty()) return {};
				for (const Binding& binding : slots.front()) {
					if (binding.property != "text") continue;
					const ScriptHost::Frame frame(ScriptHost::Mode::Value);
					const EvalResult result = evaluate(binding.program, signals(), binding.ids, nullptr, &row);
					if (!result.ok) continue;
					std::string name = result.value.asText();
					if (!name.empty()) return name;
				}
				return {};
			}

		private:
			const ListView& mList;   // owns this, so outlives it
		};

		// Where a walk's bindings go, and what they may read.
		struct BindingSite {
			LayoutInstance::ObserverSink& observers;
			const ScriptModule*           module = nullptr;
			// Set for a row: its table, for the columns its bindings read, and its list,
			// which is how a handler finds the row it is on when it fires.
			Table*         table = nullptr;
			ListView*      list = nullptr;
			RowBindingSet* rows = nullptr;
			// A row built after the layout loaded -- because the list's view grew, in the
			// middle of a frame -- applies its bindings at once rather than when their
			// signals next move.
			bool late = false;
			// Whether to say what is wrong. Every row is a copy of one template, so only the
			// first says it: a pool of forty rows would otherwise say it forty times.
			bool report = true;
		};

		void bindWalk(const Blueprint& bp, const Walk& walk, const BindingSite& site) {
			for (size_t b = 0; b < bp.bindingCount(); ++b) {
				const BlueprintBinding& record = bp.binding(b);
				if (record.node >= walk.made.size()) continue;
				Widget* target = walk.made[record.node];
				if (!target) continue; // not this walk's, or its widget was skipped: no subject
				const WidgetKind& kind = *walk.kinds[record.node];

				const std::string property(bp.string(record.key));
				const std::string where = property + " on '" + target->id() + "'";

				if (record.kind == static_cast<uint32_t>(BindingKind::Event)) {
					Program program = bp.programFor(record);
					std::vector<uint32_t> ids = resolveNames(program, where, site.table, site.module);
					// In a row, handed the list and this copy's slot, so `item` means
					// something inside a handler as well as inside a value binding.
					const size_t slot = site.list ? site.list->currentSlot() : 0;
					EventRunner run = makeRunner(std::move(program), std::move(ids), property,
					                             site.list, slot);
					// The widget first, so a root <popup> keeps the onClose it means.
					if (attachEvent(kind, *target, property, run)) continue;
					// Node 0 of a layout, never of a row: a row's walk carries its list.
					if (!site.list && record.node == 0 && isLayoutEvent(property)) {
						(property == "onOpen" ? site.observers.onOpen : site.observers.onClose) = std::move(run);
						continue;
					}
					if (site.report) {
						RDA_LOG_WARNING("layout: nothing answers '" << property << "' on '"
						                << target->id() << "'");
					}
					continue;
				}

				const PropSetter setter = findSetter(kind, property);
				if (!setter) {
					if (site.report) reportUnbindable(bp, bp.node(record.node), property, target->id());
					continue;
				}
				Program program = bp.programFor(record);
				std::vector<uint32_t> ids = resolveNames(program, where, site.table, site.module);

				if (site.rows && readsRow(program)) {
					ListView& list = *site.list;
					RowBindingSet& rows = *site.rows;
					// A row binding that also reads a signal: the list watches it on the
					// row's behalf. Registered once for the list rather than once per pooled
					// row -- every copy runs the same program and the refresh re-binds all of
					// them, so the other copies' registrations would be the same observer.
					if (list.currentSlot() == 0) {
						std::vector<uint32_t> watched = signalsReadBy(program, ids);
						if (!watched.empty()) {
							site.observers.ids.push_back(bindings().addRowRefresh(std::move(watched), &list));
						}
						// One that runs JavaScript reads what it reads only once it runs, so
						// the list is given an observer of its own to grow as its rows find out.
						if (program.usesScript() && rows.scriptRefresh == kNoObserver) {
							const ObserverId refresh = bindings().addRowRefresh({}, &list);
							site.observers.ids.push_back(refresh);
							rows.scriptRefresh = refresh;
						}
					}

					RowBindingSet::Binding binding;
					binding.program  = std::move(program);
					binding.ids      = std::move(ids);
					binding.target   = target;
					binding.property = property;
					binding.setter   = setter;
					rows.slots.back().push_back(std::move(binding));
					continue;
				}

				// An ordinary binding, driven by signals like any other. In a row it reads
				// nothing about the row, and it goes on working as the row is reused.
				const ObserverId observer = bindings().add(std::move(program), std::move(ids), target,
				                                           property, setter);
				site.observers.ids.push_back(observer);
				if (site.late) bindings().applyNow(observer);
			}
		}
	}

	// One copy of a list's row template. Called once per pooled row, so every widget
	// exists before anything scrolls and none is built while it does.
	static void buildPooledRow(const Blueprint& bp, size_t templateRoot, ListView& list,
	                           RowBindingSet& rows, const std::string& idPrefix, Table* table,
	                           LayoutInstance::ObserverSink& observers, bool late) {
		const Walk walk = walkNodes(bp, templateRoot, list, idPrefix, /*row*/ true);
		list.beginRow(walk.root);
		rows.slots.emplace_back();

		BindingSite site{ observers, observers.module.get() };
		site.table = table;
		site.list = &list;
		site.rows = &rows;
		site.late = late;
		site.report = list.currentSlot() == 0;
		bindWalk(bp, walk, site);
	}

	LayoutInstance instantiate(const Blueprint& blueprint, Widget& parent,
	                           const std::string& idPrefix) {
		LayoutInstance instance;
		if (!blueprint.valid() || blueprint.nodeCount() == 0) return instance;

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

		// The layout's functions, loaded before anything that names them is resolved. A
		// blueprint with none -- almost every one -- does nothing here and starts nothing.
		if (blueprint.hasScript()) {
			const std::string name(blueprint.nodeCount() > 0 ? blueprint.string(blueprint.node(0).id) : "layout");
			std::string error;
			instance.sink()->module = scriptHost().load(blueprint, name, error);
			if (!instance.sink()->module) {
				RDA_LOG_ERROR("layout '" << name << "': " << error << ". Its bindings that run "
				              "code will report that they cannot, and the rest work as usual");
			}
		}
		const ScriptModule* module = instance.sink()->module.get();

		const Walk walk = walkNodes(blueprint, 0, parent, idPrefix, /*row*/ false);
		instance.adopt(walk.root);

		// ---- row templates ---------------------------------------------------------
		// Built after the tree, because a row needs its list to exist and the list needs
		// its own properties applied before anyone asks which table it shows.
		for (const auto& entry : walk.templates) {
			ListView* list = entry.first;
			const size_t templateRoot = entry.second;
			const uint32_t listNode = blueprint.node(templateRoot).parent;

			const uint32_t tableId = tables().find(list->of);
			Table* table = (tableId == kNoTable) ? nullptr : &tables().at(tableId);
			if (!table) {
				RDA_LOG_WARNING("layout: list '" << list->id() << "' shows table '" << list->of
				                << "', which nothing declared");
			}

			// How many copies to build now. None, unless the layout asked for some: the
			// list builds what its view needs the first time it is drawn, and more if the
			// view grows -- so a list three rows tall has three, and one that fills a tall
			// window has what fills it. `poolSize` builds that many ahead of time, for a
			// list that would rather not build anything in the middle of a frame.
			int copies = 0;
			if (listNode != kNoParent) {
				const float asked = blueprint.number(blueprint.node(listNode), "poolSize", 0.0f);
				if (asked > 0.0f) copies = static_cast<int>(asked);
			}
			const std::shared_ptr<LayoutInstance::ObserverSink> sink = instance.sink();
			// The list holds its rows' bindings through this, and asks it to apply them.
			auto rows = std::make_shared<RowBindingSet>(*list);
			list->setRowBinder(rows);
			for (int k = 0; k < copies; ++k) {
				buildPooledRow(blueprint, templateRoot, *list, *rows,
				               idPrefix + "#" + std::to_string(k) + "/", table, *sink, false);
			}

			// And the means to build more, for when the view outgrows the pool. Its own copy
			// of the blueprint, because the one this was loaded from belongs to whoever
			// loaded it and may be replaced by a hot reload while the list lives on.
			auto source = std::make_shared<Blueprint>();
			if (blueprint.cloneInto(*source)) {
				// The set by raw pointer: the list owns it and the factory, so it outlives both.
				RowBindingSet* bound = rows.get();
				list->setRowFactory([source, templateRoot, idPrefix, sink, list, bound] {
					if (!sink->open) return;
					const uint32_t id = tables().find(list->of);
					Table* table = (id == kNoTable) ? nullptr : &tables().at(id);
					buildPooledRow(*source, templateRoot, *list, *bound,
					               idPrefix + "#" + std::to_string(list->poolCount()) + "/", table, *sink, true);
				});
			}
		}

		// ---- bindings ----------------------------------------------------------------
		bindWalk(blueprint, walk, BindingSite{ *instance.sink(), module });
		return instance;
	}
}
