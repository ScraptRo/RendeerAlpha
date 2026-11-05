#include <GraphicalObjects/Gui/Canvas.h>
#include <Core/Utf8.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Canvas, text: measuring it, wrapping it, and the glyph quads it is drawn with. The measure
// and the drawing are in one file because bold is a drawn effect that the measure has to
// agree with, and boldOffset is the one number they share.
namespace RDA {

	void Canvas::drawText(const char* text, glm::vec2 topLeft, uint32_t color, TextStyle font) {
		if (mFont && text) addText(topLeft.x, topLeft.y + baseline(font), text, color, font);
	}

	// Bold is drawn twice, this far apart. It scales with the size so a heading is
	// emboldened as much as a caption is, proportionally, and it never changes an
	// advance -- the second pass bleeds a pixel to the right and that is all, which is
	// what keeps a bold word in the same monospaced column as a regular one.
	static float boldOffset(float pixelHeight) {
		return std::max(1.0f, std::round(pixelHeight / 16.0f));
	}

	float Canvas::measureText(const char* text, TextStyle font) const {
		if (!mFont || !text) return 0.0f;
		const int index = mFont->indexForSize(font.size);
		float width = mFont->textWidth(text, index);
		// The bleed, once, so a bounding box drawn from this contains what is drawn.
		if (font.bold && width > 0.0f) width += boldOffset(mFont->pixelHeight(index));
		return width;
	}
	float Canvas::lineHeight(TextStyle font) const {
		return mFont ? mFont->lineAdvance(mFont->indexForSize(font.size)) : 16.0f;
	}
	float Canvas::baseline(TextStyle font) const {
		return mFont ? mFont->ascent(mFont->indexForSize(font.size)) : 12.0f;
	}

	std::vector<std::pair<size_t, size_t>> Canvas::wrapText(const std::string& text, float width,
	                                                     TextStyle font) const {
		std::vector<std::pair<size_t, size_t>> lines;
		if (!mFont) { lines.emplace_back(0, text.size()); return lines; }
		const int index = mFont->indexForSize(font.size);

		// One pass, character by character, remembering the last space. A word that does
		// not fit on a line of its own is broken rather than allowed to overflow: a long
		// path or hash has no space in it, and silently running off the edge is worse
		// than an ugly break.
		//
		// The offsets returned are byte offsets, because that is what a caller slices a
		// std::string with, but the walk advances a codepoint at a time -- so a break
		// never lands inside a character.
		size_t lineStart = 0;
		size_t lastSpace = std::string::npos;
		float  penX = 0.0f;
		float  widthAtSpace = 0.0f;

		size_t i = 0;
		while (i < text.size()) {
			if (text[i] == '\n') {
				lines.emplace_back(lineStart, i - lineStart);
				lineStart = i + 1;
				lastSpace = std::string::npos;
				penX = 0.0f;
				++i;
				continue;
			}
			uint32_t cp = 0;
			const size_t step = Utf8::decode(text.data() + i, text.data() + text.size(), cp);
			const float advance = mFont->advance(cp, index);
			if (cp == ' ') { lastSpace = i; widthAtSpace = penX; }

			if (penX + advance > width && i > lineStart) {
				if (lastSpace != std::string::npos && lastSpace > lineStart) {
					lines.emplace_back(lineStart, lastSpace - lineStart);
					lineStart = lastSpace + 1;
					penX -= widthAtSpace + mFont->advance(U' ', index);
				} else {
					lines.emplace_back(lineStart, i - lineStart);
					lineStart = i;
					penX = 0.0f;
				}
				lastSpace = std::string::npos;
			}
			penX += advance;
			i += step;
		}
		lines.emplace_back(lineStart, text.size() - lineStart);
		return lines;
	}

	void Canvas::addText(float penX, float baselineY, const char* text, uint32_t color,
	                  TextStyle font) {
		if (!mFont || !text) return;
		addTextRange(penX, baselineY, text, static_cast<int>(std::strlen(text)), color, font);
	}

	float Canvas::addTextRange(float penX, float baselineY, const char* text, int count,
	                        uint32_t color, TextStyle font) {
		if (!mFont || !text) return penX;
		const int index = mFont->indexForSize(font.size);
		const float bold = font.bold ? boldOffset(mFont->pixelHeight(index)) : 0.0f;
		setTexture(nullptr);
		// `count` is a byte count, and a glyph is a codepoint: the walk is over
		// characters even though the caller measured the string in bytes.
		const char* p = text;
		const char* end = text + count;
		while (p < end) {
			uint32_t cp = 0;
			p += Utf8::decode(p, end, cp);
			GlyphQuad q;
			if (mFont->quadFor(cp, penX, baselineY, q, index)) { // advances penX
				addQuad(q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, color);
				// The second pass is the whole of bold: one glyph, one pixel over.
				if (bold > 0.0f) {
					addQuad(q.x0 + bold, q.y0, q.x1 + bold, q.y1,
					        q.u0, q.v0, q.u1, q.v1, color);
				}
			}
		}
		return penX;
	}
}
