#include <Layout/Instantiator.h>
#include <Layout/ListView.h>
#include <Layout/WidgetRegistry.h>
#include <Layout/WidgetSchema.h>
#include <GraphicalObjects/Widget.h>
#include <Logger/Logger.h>

namespace RDA::Layout {

	namespace {
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


		// Says what is wrong with a node: no widget by its name, or properties its widget
		// does not take. False when there is no such widget, which skips it and its subtree.
		bool vetNode(const Blueprint& bp, const BlueprintNode& node, const WidgetKind* kind) {
			if (!kind) {
				const std::string_view wanted = bp.string(node.type);
				if (wanted == "scrollview") {
					RDA_LOG_WARNING("layout: <scrollview> is now <scroll> (id '"
					                << bp.string(node.id) << "'), skipping it and its "
					                "children. One element scrolls: a column of children, "
					                "down by default and sideways with horizontal={true}");
				} else {
					RDA_LOG_WARNING("layout: no widget type named '" << wanted
					                << "' (id '" << bp.string(node.id) << "'), skipping it and its children");
				}
				return false;
			}
			reportUnknownProps(bp, node);
			return true;
		}
	}

	// The one walk over a blueprint's nodes, for a whole layout and for one pooled row.
	//
	// A layout's walk starts at the first node and puts each node with no parent into
	// `top`. A row's starts at its template's root, which goes into the list, and takes
	// only what descends from it. Parents are always earlier in the array (parse()
	// checks it), so a child's parent is built by the time the child is reached, and
	// that is what makes this one forward pass. A node whose parent was skipped is
	// skipped too, which is how an unknown widget takes its subtree with it instead of
	// reparenting it somewhere odd.
	//
	// The two walks used to be two copies, and they had drifted: nothing ever checked a
	// row template's nodes, because the layout's walk skipped them and the row's never
	// reported anything. A layout's walk now checks them where it meets them, once, and
	// a row's says nothing, being a copy of what was checked.
	Walk walkNodes(const Blueprint& bp, size_t first, Widget& top, const std::string& idPrefix, bool row) {
		const size_t count = bp.nodeCount();
		Walk walk;
		walk.made.assign(count, nullptr);
		walk.kinds.assign(count, nullptr);
		// A row takes its template's subtree and nothing else.
		std::vector<bool> taken(count, false);
		// Inside a list's template, which a layout's walk checks and leaves for the rows.
		std::vector<bool> templated(count, false);

		for (size_t i = first; i < count; ++i) {
			const BlueprintNode& node = bp.node(i);
			const bool child = node.parent != kNoParent;
			if (row && i != first && !(child && taken[node.parent])) continue;
			taken[i] = true;
			const WidgetKind* kind = findKind(bp.string(node.type));

			if (!row && child && templated[node.parent]) {
				templated[i] = true;
				vetNode(bp, node, kind);
				continue;
			}

			Widget* into = (!child || (row && i == first)) ? &top : walk.made[node.parent];
			if (!into) continue;

			// A list's child is not a child: it is the template every pooled row is built
			// from. Left out of this pass, and with it everything below it.
			if (!row && child && walk.kinds[node.parent]->rowTemplate) {
				walk.templates.emplace_back(static_cast<ListView*>(into), i);
				templated[i] = true;
				vetNode(bp, node, kind);
				continue;
			}

			if (row ? !kind : !vetNode(bp, node, kind)) continue;
			auto widget = buildWidget(*kind, bp, node, idPrefix + std::string(bp.string(node.id)));
			if (!widget) continue;
			walk.made[i] = into->addChild(std::move(widget));
			walk.kinds[i] = kind;
			if (!walk.root) walk.root = walk.made[i];
		}
		return walk;
	}
}
