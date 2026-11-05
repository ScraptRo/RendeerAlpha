#include "Kinds.h"
#include "KindSupport.h"
#include <Core/Colour.h>
#include <iterator>

// The widgets that show something the layout did not draw: <image>, <stream> and
// <viewport>.
namespace RDA::Layout::Kinds {

	namespace {
		// ---- image -------------------------------------------------------------------
		Image::Fit imageFitFrom(std::string_view word) {
			return word == "stretch" ? Image::Fit::Stretch : Image::Fit::Contain;
		}
		// A colour this build cannot read leaves the tint as it was.
		void tintFrom(const std::string& text, uint32_t& tint) {
			uint32_t parsed = 0;
			if (parseColor(text.c_str(), parsed)) tint = parsed;
		}

		std::unique_ptr<Widget> makeImage(const Blueprint& bp, const BlueprintNode& node, const std::string& id) {
			return std::make_unique<Image>(id, std::string(bp.text(node, "src")));
		}
		void constImage(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Image& w = as<Image>(widget);
			if (const std::string_view tint = bp.text(node, "tint"); !tint.empty()) {
				tintFrom(std::string(tint), w.tint);
			}
			w.fit = imageFitFrom(bp.text(node, "fit"));
		}
		constexpr BoundProp kImageProps[] = {
			{ "src",  [](Widget& w, const Value& v) { as<Image>(w).source = v.asText(); } },
			{ "tint", [](Widget& w, const Value& v) { tintFrom(v.asText(), as<Image>(w).tint); } },
			{ "fit",  [](Widget& w, const Value& v) { as<Image>(w).fit = imageFitFrom(v.text); } },
		};

		// ---- stream ------------------------------------------------------------------
		Stream::Fit streamFitFrom(std::string_view word) {
			return word == "stretch" ? Stream::Fit::Stretch : Stream::Fit::Contain;
		}

		std::unique_ptr<Widget> makeStream(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Stream>(id);
		}
		void constStream(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			Stream& w = as<Stream>(widget);
			w.name = std::string(bp.text(node, "name"));
			w.fit = streamFitFrom(bp.text(node, "fit"));
		}
		constexpr BoundProp kStreamProps[] = {
			{ "name", [](Widget& w, const Value& v) { as<Stream>(w).name = v.asText(); } },
			{ "fit",  [](Widget& w, const Value& v) { as<Stream>(w).fit = streamFitFrom(v.text); } },
		};

		// ---- viewport ----------------------------------------------------------------
		std::unique_ptr<Widget> makeViewport(const Blueprint&, const BlueprintNode&, const std::string& id) {
			return std::make_unique<Viewport>(id);
		}
		void constViewport(const Blueprint& bp, const BlueprintNode& node, Widget& widget) {
			const std::string_view named = bp.text(node, "name");
			if (!named.empty()) as<Viewport>(widget).name = std::string(named);
		}
		// The name is asked for every frame -- by the events it reports and by whichever
		// viewport the GPU path draws into -- so one can follow state.
		constexpr BoundProp kViewportProps[] = {
			{ "name", [](Widget& w, const Value& v) { as<Viewport>(w).name = v.asText(); } },
		};
	}

	const WidgetKind kImageKind{ "image", nullptr, &makeImage, &constImage,
	                             kImageProps, std::size(kImageProps) };
	const WidgetKind kStreamKind{ "stream", nullptr, &makeStream, &constStream,
	                              kStreamProps, std::size(kStreamProps) };
	const WidgetKind kViewportKind{ "viewport", nullptr, &makeViewport, &constViewport,
	                                kViewportProps, std::size(kViewportProps) };
}
