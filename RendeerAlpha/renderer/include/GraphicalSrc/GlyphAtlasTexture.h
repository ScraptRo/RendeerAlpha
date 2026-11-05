#pragma once
#include <GraphicalObjects/Texture.h>

namespace RDA {

	class FontAtlas;

	// The GPU half of the font atlas: the R8 image the GUI samples, made from the bitmap a
	// FontAtlas has baked and kept up to date as it learns glyphs.
	//
	// The atlas is CPU-only -- it bakes, measures and marks rows dirty -- and this is the
	// only thing that turns any of it into a Vulkan object. Whoever owns the atlas owns one
	// of these beside it and calls uploadPending() once a frame; GuiRenderer does, and binds
	// texture() in the descriptor set it writes once at start-up.
	class GlyphAtlasTexture {
	public:
		// Makes the image and sends the whole bitmap. `atlas` must have been baked. The
		// rows it had marked dirty are in the image now, so they are taken. False, and
		// logged, when either step fails.
		bool create(FontAtlas& atlas);

		// Sends whatever the atlas has baked on demand since the last call, as one
		// rectangle.
		//
		// Asked for by the renderer once a frame rather than done when a glyph is baked:
		// baking happens while the interface is being walked, a frame that met twenty new
		// characters would otherwise be twenty stalls, and the descriptor set is written
		// once at start-up -- so nothing else would ever ask for the texture again.
		//
		// Not const, and the atlas is not either: sending the rows is what clears them.
		void uploadPending(FontAtlas& atlas);

		const Texture& texture() const { return mTexture; }
		bool isValid() const { return mTexture.isValid(); }
		void destroy() { mTexture.destroy(); }

	private:
		Texture mTexture;
	};
}
