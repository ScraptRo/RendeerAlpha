#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <Runtime/Client.h>
#include <cstdint>
#include <string>

// The surface a scripted application sees.
//
// This is the API a JavaScript developer writes against, so it is designed for them
// rather than mirrored from the C++ side. It builds an ordinary GuiDrawData, which the
// runtime client then sends — a script produces exactly what a C++ client produces, and
// neither is privileged.
//
// ---- what a script looks like ----
//
//     rda.onDraw = function (ui) {
//         ui.rect(0, 0, ui.width, ui.height, 0x1C202Cff);
//         ui.rect(24, 24, ui.width - 48, 56, 0x3A5CA8ff);
//         ui.text("Hello", 40, 44, 0xE8EAF0ff);
//
//         if (ui.pointerDown && ui.hit(24, 24, 120, 32)) {
//             ui.rect(24, 24, 120, 32, 0xF0A050ff);
//         }
//     };
//
// ---- the decisions behind it ----
//
// *Immediate calls, not a widget tree.* A tree is what someone arriving from Electron
// expects, and it is where this should end up. Immediate is the smaller honest first
// step: it is what the client shim already produces, so nothing has to be invented to
// support it, and a tree can be built on top later without changing the wire format or
// this file's meaning. Starting with a tree would mean designing a layout system before
// anything had ever been drawn from a script.
//
// *Colours are 0xRRGGBBAA.* That is what a web developer writes. The GPU wants them
// packed the other way round (R in the low byte), so the swizzle happens here, once, in
// the binding — rather than every script carrying the engine's byte order around.
//
// *Measurement is synchronous and always available.* A client has no atlas, but the
// runtime sends the metrics with the surface, so ui.measure() can answer immediately.
// Without it every script would hardcode pixel widths and break on a different font,
// which is the single most likely way a scripted UI goes wrong.
//
// *Coordinates are pixels from the top-left of the window*, matching the rest of the
// GUI. No layout units, no scaling: a script that wants those can build them.
namespace RDA::Script {

	// Builds one frame's geometry from a script's calls, then hands it over.
	//
	// One instance lives for the life of the application; begin() and end() bracket each
	// frame. It owns no GPU resource and knows nothing about the runtime beyond the
	// metrics it is given, which is what keeps it testable without a device.
	class DrawApi {
	public:
		// The font the runtime will rasterise with, so measure() can answer. Handed over
		// once the surface is ready, since that is when the metrics arrive.
		void setFont(const Runtime::RuntimeClient::FontMetrics* font) { mFont = font; }
		// The surface size a script reads as ui.width / ui.height. Whatever the runtime
		// granted, not what was asked for.
		void setSurfaceSize(float width, float height) { mWidth = width; mHeight = height; }
		// This frame's input, in the window's own coordinates.
		void setInput(const GuiInput& input) { mInput = input; }

		float width() const { return mWidth; }
		float height() const { return mHeight; }

		// ---- frame ----
		// Clears the geometry and opens a frame. Everything drawn lands in one batch
		// until an image is drawn, which starts another — batching is the binding's
		// concern, not the script's.
		void begin();
		// Closes the last batch. The result is valid until the next begin().
		void end();
		const GuiDrawData& draw() const { return mDraw; }
		// Changes whenever the geometry does, so an unchanged frame is not resent. This
		// is what carries the retained-cache saving into scripted applications.
		uint64_t version() const { return mVersion; }

		// ---- drawing ----
		// `color` is 0xRRGGBBAA, as a web developer would write it.
		void rect(float x, float y, float w, float h, uint32_t color);
		// Draws `text` with its top-left at (x, y). Returns the advance width, so a
		// script can lay out a row without measuring separately.
		float text(const std::string& text, float x, float y, uint32_t color);
		// An image the script uploaded earlier, by the id it chose.
		void image(float x, float y, float w, float h, uint32_t textureId,
		           uint32_t tint = 0xFFFFFFFFu);
		// Restricts drawing to a rectangle until popClip(). Nesting intersects, so a
		// script cannot widen a clip its caller set.
		void pushClip(float x, float y, float w, float h);
		void popClip();

		// ---- measurement ----
		float measure(const std::string& text) const;
		float lineHeight() const;

		// ---- input ----
		// Read as ui.pointerX / ui.pointerY / ui.pointerDown / ui.pressed / ui.released.
		float pointerX() const { return mInput.pointer.x; }
		float pointerY() const { return mInput.pointer.y; }
		bool  pointerDown() const { return mInput.down; }
		bool  pressed() const { return mInput.pressed; }
		bool  released() const { return mInput.released; }
		float scroll() const { return mInput.scroll; }
		const std::string& typed() const { return mInput.typed; }
		// Whether the pointer is inside a rectangle — the one piece of hit testing every
		// script needs, and easy to get subtly wrong with half-open ranges.
		bool hit(float x, float y, float w, float h) const;

	private:
		// 0xRRGGBBAA as written in a script, to the R8G8B8A8 the vertex wants.
		static uint32_t packColor(uint32_t rgba);
		// Opens a new command when the texture or clip changes, since a draw command is
		// one texture and one clip.
		void flush();
		void setTexture(uint32_t textureId);

		GuiDrawData mDraw;
		const Runtime::RuntimeClient::FontMetrics* mFont = nullptr;
		GuiInput mInput;
		float mWidth = 0.0f;
		float mHeight = 0.0f;

		uint32_t mCmdStart = 0;      // first index of the command being built
		uint32_t mTexture = 0;       // 0 = the atlas, so solid quads and text share a batch
		glm::vec4 mClip{ 0.0f };
		std::vector<glm::vec4> mClipStack;
		uint64_t mVersion = 0;
	};
}
