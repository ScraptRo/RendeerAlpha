#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/FontAtlas.h>
#include <string>
#include <vector>

namespace RDA {

	// Pack RGBA (0..255) into the R8G8B8A8_UNORM layout the UI vertex expects. constexpr
	// so it can be a default argument.
	constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
		return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
	}

	struct Rect {
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		bool contains(glm::vec2 p) const {
			return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h;
		}
	};

	// The pointer state the GUI reacts to, expressed in the target's pixel space. On a
	// window this is the mouse; for an in-world panel it is the raycast hit. The GUI
	// never touches the raw window mouse — this struct is the decoupling seam.
	struct GuiInput {
		glm::vec2 pointer{ 0.0f };
		bool  down = false;      // button currently held
		bool  pressed = false;   // went down this frame
		bool  released = false;  // came up this frame
		float scroll = 0.0f;
	};

	struct GuiVertex {
		glm::vec2 pos;
		glm::vec2 uv;
		uint32_t  color; // R8G8B8A8_UNORM, see rgba()
	};

	// A run of indices sharing one clip rect. Clip is (x0, y0, x1, y1) in pixels.
	struct GuiDrawCmd {
		glm::vec4 clip;
		uint32_t  indexOffset;
		uint32_t  indexCount;
	};

	struct GuiDrawData {
		std::vector<GuiVertex>  vertices;
		std::vector<uint16_t>   indices;
		std::vector<GuiDrawCmd> commands;
		void clear() { vertices.clear(); indices.clear(); commands.clear(); }
	};

	// Immediate-mode GUI, one per window. Widgets are re-declared every frame; the only
	// state that persists between frames is interaction (hot/active keyed by a stable
	// id) and the reused draw buffers. That id system is deliberate: it drives hot/
	// active now and is the same identity the retained tree will key nodes on later.
	class Gui {
	public:
		// Binds the CPU-side font metrics (the atlas texture is the backend's concern).
		void init(const FontAtlas* font) { mFont = font; }

		// Frame boundaries — driven by the engine, around the app's onUpdate.
		void begin(const GuiInput& input);
		void end();

		// ---- widgets (immediate) ----
		void beginPanel(const char* id, const Rect& rect);
		void endPanel();
		void label(const char* text, glm::vec2 pos, uint32_t color = rgba(230, 230, 235));
		bool button(const char* id, const char* text, const Rect& rect);

		const GuiDrawData& drawData() const { return mDraw; }

	private:
		uint32_t hashId(const char* str) const;   // FNV-1a
		uint32_t scopedId(const char* id) const;   // combine with the panel scope

		void addQuad(float x0, float y0, float x1, float y1,
		             float u0, float v0, float u1, float v1, uint32_t color);
		void addRect(const Rect& r, uint32_t color);
		void addText(float penX, float baselineY, const char* text, uint32_t color);

		void pushClip(const glm::vec4& clip);
		void popClip();
		void flushCmd();

		const FontAtlas* mFont = nullptr;
		GuiInput         mInput;
		GuiDrawData      mDraw;

		uint32_t mHot = 0;     // id under the pointer this frame
		uint32_t mActive = 0;  // id the pointer went down on (persists across frames)

		std::vector<glm::vec4> mClipStack;
		std::vector<uint32_t>  mScopeStack;
		glm::vec4              mCurrentClip{ 0.0f };
		uint32_t               mCmdStart = 0; // first index of the in-progress command
	};
}
