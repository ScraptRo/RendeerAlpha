#include "KindSupport.h"

namespace RDA::Layout::Kinds {

	bool verticalFrom(std::string_view word, bool fallback) {
		if (word == "vertical")   return true;
		if (word == "horizontal") return false;
		return fallback;
	}

	Alignment alignFrom(std::string_view text, Alignment fallback) {
		// Split first, so the words below are compared against the word alone.
		float offset = 0.0f;
		const std::string_view word = splitAlignOffset(text, offset);
		const auto with = [offset](Align a) { return Alignment{ a, offset }; };
		if (word == "stretch") return with(Align::Stretch);
		if (word == "start")   return with(Align::Start);
		if (word == "center")  return with(Align::Center);
		if (word == "end")     return with(Align::End);
		if (word == "auto")    return with(Align::Auto);
		return fallback;
	}

	TextAlign textAlignFrom(std::string_view text, TextAlign fallback, float& offset) {
		float nudge = 0.0f;
		const std::string_view word = splitAlignOffset(text, nudge);
		TextAlign align = fallback;
		if      (word == "start")  align = TextAlign::Start;
		else if (word == "center") align = TextAlign::Center;
		else if (word == "end")    align = TextAlign::End;
		else return fallback;   // and the offset stays what it was, with the word it belongs to
		offset = nudge;
		return align;
	}

	TextAlign textAlignFrom(std::string_view text, TextAlign fallback) {
		float ignored = 0.0f;
		return textAlignFrom(text, fallback, ignored);
	}

	Distribute spreadFrom(std::string_view word, Distribute fallback) {
		if (word == "spaceBetween") return Distribute::SpaceBetween;
		if (word == "spaceAround")  return Distribute::SpaceAround;
		return fallback;
	}

	Popup::Placement popupPlacementFrom(std::string_view word, Popup::Placement fallback) {
		if (word == "below") return Popup::Placement::Below;
		if (word == "above") return Popup::Placement::Above;
		if (word == "right") return Popup::Placement::Right;
		if (word == "left")  return Popup::Placement::Left;
		if (word == "over")  return Popup::Placement::Over;
		return fallback;
	}

	uint8_t liveFrom(std::string_view word) {
		if (word == "polite")    return 1;
		if (word == "assertive") return 2;
		return 0;
	}

	Anchor anchorFrom(std::string_view word, Anchor fallback) {
		if (word == "fill")        return Anchor::Fill;
		if (word == "stretchX")    return Anchor::StretchX;
		if (word == "stretchY")    return Anchor::StretchY;
		if (word == "bottomLeft")  return Anchor::BottomLeft;
		if (word == "bottomRight") return Anchor::BottomRight;
		return fallback;
	}

	void applySize(const Blueprint& bp, const BlueprintNode& node, std::string_view key,
	               std::string_view minKey, std::string_view maxKey, SizeSpec& spec) {
		if (const BlueprintProp* p = bp.find(node, key)) {
			if (p->kind == static_cast<uint32_t>(PropKind::String)) {
				// A word this build does not understand leaves the size alone, the same
				// answer an unknown alignment gets.
				SizeSpec::parse(bp.string(p->text), spec);
			} else {
				spec = SizeSpec::fixed(p->number);
			}
		}
		spec.min = bp.number(node, minKey, spec.min);
		spec.max = bp.number(node, maxKey, spec.max);
	}

	void sizeFrom(const Value& value, SizeSpec& spec) {
		if (value.kind == Value::Kind::Text) {
			SizeSpec::parse(value.text, spec);
			return;
		}
		spec = SizeSpec::fixed(static_cast<float>(value.asNumber()));
	}

	void variantFrom(const Blueprint& bp, const BlueprintNode& node, Variant& variant) {
		const std::string_view named = bp.text(node, "variant");
		if (!named.empty()) variant = Variant(named);
	}
}
