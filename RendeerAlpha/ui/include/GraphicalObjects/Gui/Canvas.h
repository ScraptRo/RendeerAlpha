#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/MeshAsset.h>
#include <GraphicalSrc/FontAtlas.h>

namespace RDA {

	// The draw-list builder: where every rectangle, glyph and picture of a frame ends up.
	//
	// Everything the interface draws passes through here, which is why the clip, the texture and
	// the fade are state of the canvas rather than of any widget: a widget says what to draw and
	// the canvas stamps it with the clip rectangle, the texture and the opacity in force. It
	// knows nothing about input, focus or what a widget is. The one thing it is handed is the
	// font atlas, because a glyph is a quad and a solid fill samples the atlas' white texel.
	//
	// Owned by Gui, which forwards its public drawing calls here. Public so that Gui.h can hold
	// one by value; nothing outside Gui is meant to create one.
	class Canvas {
	public:
		// Binds the CPU-side font metrics (the atlas texture is the backend's concern).
		void setFont(const FontAtlas* font) { mFont = font; }
		const FontAtlas* font() const { return mFont; }

		// ---- the frame -----------------------------------------------------------------------
		// Every frame, walked or not: the clip stack back to nothing and the clip itself seeded to
		// the target. Seeded to the target rather than to "infinite": widgets read the current
		// clip as the area they are laid out inside, so the outermost one has to be a real size.
		void beginFrame(const glm::vec2& viewport);
		// A frame that walks the tree: the draw list starts empty and the fade starts at one.
		void beginWalk();
		// A frame that does not: truncate back to where the last walk finished. The geometry is
		// already in the buffers, so this costs three size assignments and no copying.
		void reuse(size_t vertices, size_t indices, size_t commands);
		const GuiDrawData& drawData() const { return mDraw; }
		// A hash of what the frame draws: FNV-1a over the vertex and index bytes plus each
		// command's clip, range and texture -- everything that decides what ends up on screen.
		uint64_t fingerprint() const;

		// ---- geometry -------------------------------------------------------------------------
		void addQuad(float x0, float y0, float x1, float y1,
		             float u0, float v0, float u1, float v1, uint32_t color);
		// The same quad with its corners given rather than derived, for geometry that is
		// not axis-aligned. Wound the same way, so it clips and fades like everything else.
		void addQuadPoints(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
		                   uint32_t color);
		void addRect(const Rect& r, uint32_t color);
		// Rounded fill; falls back to addRect when the radius rounds away to nothing.
		void addRectRounded(const Rect& r, uint32_t color, float radius);
		// A filled rect with an optional border, drawn as a border-colored shape with the
		// fill inset on top -- so one path covers square and rounded frames alike.
		void addFrame(const Rect& r, uint32_t fill, uint32_t border, float borderWidth, float radius);

		void drawRect(const Rect& rect, uint32_t color);
		void drawRectRounded(const Rect& rect, uint32_t color, float radius);
		void drawLine(glm::vec2 from, glm::vec2 to, uint32_t color, float width);
		bool drawMesh(const WidgetMesh& mesh, const Rect& rect, int from, int into, float t,
		              const uint32_t* colours, size_t colourCount, uint32_t base);
		void image(const Rect& rect, const Texture* texture, uint32_t tint);

		// ---- text -----------------------------------------------------------------------------
		// Measured and drawn here together because bold is a drawn effect that the measure has to
		// agree with.
		void addText(float penX, float baselineY, const char* text, uint32_t color,
		             TextStyle font = {});
		// Draws `count` characters starting at `text` and returns the pen X after the
		// run, so colored runs (syntax highlighting) chain without copying substrings.
		float addTextRange(float penX, float baselineY, const char* text, int count,
		                   uint32_t color, TextStyle font = {});
		void drawText(const char* text, glm::vec2 topLeft, uint32_t color, TextStyle font = {});
		float measureText(const char* text, TextStyle font = {}) const;
		float lineHeight(TextStyle font = {}) const;
		float baseline(TextStyle font = {}) const;
		std::vector<std::pair<size_t, size_t>> wrapText(const std::string& text, float width,
		                                                TextStyle font = {}) const;

		// ---- the state new geometry is stamped with ------------------------------------------
		void setTexture(const Texture* texture); // flush + switch the active texture
		// Closes the command in progress, so what is drawn next is a command of its own.
		void flushCmd();
		void pushClip(const glm::vec4& clip);
		void popClip();
		void pushClipRect(const Rect& rect);
		void popClipRect();
		Rect currentClipRect() const;

		void pushOpacity(float alpha);
		void popOpacity();
		float opacity() const { return mOpacity; }

	private:
		const FontAtlas*       mFont = nullptr;
		GuiDrawData            mDraw;
		std::vector<glm::vec4> mClipStack;
		glm::vec4              mCurrentClip{ 0.0f };
		const Texture*         mCurrentTexture = nullptr; // texture of the in-progress command
		uint32_t               mCmdStart = 0;             // first index of the in-progress command
		float                  mOpacity = 1.0f;           // what the alpha of new geometry is scaled by
		std::vector<float>     mOpacityStack;
	};
}
