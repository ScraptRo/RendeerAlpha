#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/Texture.h>

namespace RDA {

	// A picture. `source` is a path, loaded the first time it is painted and owned by
	// this widget -- so a hot reload reloads it, which costs a file read and keeps the
	// lifetime obvious. Two images of the same file are two textures; a UI has a handful,
	// and a cache that outlives the tree is a cache that has to be invalidated.
	class Image : public Widget {
	public:
		enum class Fit : uint8_t {
			Contain, // whole picture inside the box, aspect kept, centred
			Stretch, // fill the box, aspect ignored
		};
		explicit Image(std::string id, std::string source = {})
			: Widget(std::move(id)), source(std::move(source)) {}
		~Image() override;

		// A file beside the running program, or "mem:<name>" for a picture a backend
		// registered -- see Images.h. Bindable either way, so one <image> can show a file
		// now and a model's answer a moment later.
		//
		// A path ending in .svg is drawn rather than loaded: the shape is rasterised at
		// whatever size the layout turned out to give this widget, so it is sharp at that
		// size and at the next one, and on a display at 150% it is sharp at a size nobody
		// exported. See Core/Svg.h.
		std::string source;
		Fit         fit = Fit::Contain;

		// Multiplied into the picture. White leaves it alone, which is the default and is
		// what a photograph wants; a colour is what turns one white-drawn icon into every
		// colour a theme has, without a second file or a second rasterisation.
		//
		// This is why an SVG's `currentColor` -- and a fill it never stated -- come out
		// white rather than the black the specification says: white is the one colour a
		// tint can still turn into any other.
		uint32_t    tint = rgba(255, 255, 255);

		// The picture's own size in pixels, so width="content" means what it looks like.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;

	private:
		void ensureLoaded() const;
		// Hands the texture to the engine to destroy at a safe moment. See Image::release.
		void release() const;
		// The same, for the frames of a loop. Its own function because there are three
		// places a loop is dropped and every one of them has to go through the engine:
		// clearing the vector destroys the textures where they stand, which is a
		// use-after-free whenever the GUI is still describing one. See Image::release.
		void retireFrames() const;

		// Held by pointer so its address survives this widget. The frame's draw list was
		// built before the update that destroys this widget ran, and it still refers to
		// the texture by address when it is recorded afterwards.
		mutable std::unique_ptr<Texture> mTexture;
		// A registered picture is borrowed rather than owned: the registry holds it, two
		// <image>s naming it share one, and redefining the name replaces what both draw.
		// The revision is how a redefine is noticed -- the pointer may well be reused.
		mutable const Texture* mShared = nullptr;
		mutable uint64_t       mSharedRevision = 0;
		mutable std::string mLoaded;  // the source the above were resolved from
		mutable bool        mFailed = false;

		// A drawn picture, kept parsed so that changing size re-draws rather than
		// re-reads, and the size it was last drawn at.
		mutable std::shared_ptr<void> mDrawing;   // Svg::Picture, held without the header
		mutable uint32_t    mDrawnWidth = 0;
		mutable uint32_t    mDrawnHeight = 0;

		// A drawing that moves, rasterised once into the frames of its loop.
		//
		// Not re-drawn per frame: the rasteriser walks every edge of every shape for every
		// sub-scanline, which is nothing once and real work sixty times a second. A loop
		// is short and an icon is small -- thirty frames of a 24px icon is seventy
		// kilobytes -- so playing it is picking a texture, and costs what showing a still
		// one costs.
		mutable std::vector<std::unique_ptr<Texture>> mFrames;
		mutable float mLoopSeconds = 0.0f;
		mutable float mPlayhead = 0.0f;

		// Reads the file, or the drawing, for `source`. Separate from ensureLoaded because
		// a drawing needs a size and a size is only known once the widget is placed.
		void ensureDrawn(uint32_t width, uint32_t height) const;
		bool isDrawing() const;

		// Whichever of the two this image is showing, or null.
		const Texture* active() const { return mShared ? mShared : mTexture.get(); }
	};
}
