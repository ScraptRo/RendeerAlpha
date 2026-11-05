#pragma once
#include <Layout/WidgetRegistry.h>
#include <GraphicalObjects/Widget.h>
#include <cstdint>
#include <string>
#include <string_view>

// What the kind records share: one parser for each vocabulary a property is written in,
// used for a constant and for a binding's result alike, and the adapters a record's
// handlers are written with.
//
// One parser per vocabulary is the point. The loader and the binding runtime each had
// their own copy of these words, and they had drifted: "auto" was an alignment to one and
// not the other, and an `arrange` the binding did not recognise turned the stack vertical
// where the same word as a constant left it alone. A word this build does not know leaves
// the value as it was, whichever way it arrived.
namespace RDA::Layout::Kinds {

	// The widget a record built, as the type it built. Safe because a record is found by
	// the element that made the widget, never by asking the widget what it is.
	template <class W>
	W& as(Widget& widget) { return static_cast<W&>(widget); }

	// ---- words -------------------------------------------------------------------------

	// "vertical" / "horizontal" rather than a boolean: `vertical={false}` makes the reader
	// work out what the other direction is called.
	bool verticalFrom(std::string_view word, bool fallback);

	// Where something sits on an axis. The word may carry a nudge: "center+20". "auto" is
	// only ever written by a child, and means the container decides.
	Alignment alignFrom(std::string_view text, Alignment fallback);

	// Text inside a widget's own box: the same words without "stretch", since text is the
	// size it is. The nudge, if there is one, is written to `offset`.
	TextAlign textAlignFrom(std::string_view text, TextAlign fallback, float& offset);
	TextAlign textAlignFrom(std::string_view text, TextAlign fallback);

	Distribute spreadFrom(std::string_view word, Distribute fallback);

	// "below" | "above" | "right" | "left" | "over", for a <popup>.
	Popup::Placement popupPlacementFrom(std::string_view word, Popup::Placement fallback);

	// "off" | "polite" | "assertive", as the 0, 1, 2 a widget keeps.
	uint8_t liveFrom(std::string_view word);

	// Which of its parent's edges a node follows.
	Anchor anchorFrom(std::string_view word, Anchor fallback);

	// ---- shared shapes -----------------------------------------------------------------

	// A size from a node: a number is fixed, a word means what a container makes of it.
	// `spec` keeps whatever the widget set for itself where the node says nothing, so a
	// widget type that grows its own default sizing keeps it.
	void applySize(const Blueprint& bp, const BlueprintNode& node, std::string_view key,
	               std::string_view minKey, std::string_view maxKey, SizeSpec& spec);

	// The same vocabulary from a binding: `width="content-23"` and
	// `width={() => `content-${state.gap}`}` are one language.
	void sizeFrom(const Value& value, SizeSpec& spec);

	// The variant a node names, if it names one; otherwise the widget keeps its own.
	void variantFrom(const Blueprint& bp, const BlueprintNode& node, Variant& variant);

	// ---- handlers ----------------------------------------------------------------------

	// For an event that carries nothing.
	inline std::function<void()> plain(EventRunner run) {
		return [run = std::move(run)] { run(nullptr); };
	}

	// For one that hands over a number, whatever width the widget reports it in.
	inline std::function<void(double)> withNumber(EventRunner run) {
		return [run = std::move(run)](double value) {
			const Value passed = Value::fromNumber(value);
			run(&passed);
		};
	}

	inline std::function<void(bool)> withBool(EventRunner run) {
		return [run = std::move(run)](bool value) {
			const Value passed = Value::fromBool(value);
			run(&passed);
		};
	}

	inline std::function<void(const std::string&)> withText(EventRunner run) {
		return [run = std::move(run)](const std::string& value) {
			const Value passed = Value::fromText(value);
			run(&passed);
		};
	}
}
