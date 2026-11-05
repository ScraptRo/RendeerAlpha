#include "TextEdit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

// What a text field looks like: its frame, selection, text, caret, the completion on offer,
// the line numbers and the scroll bar, plus the syntax spans the text is coloured from. The
// order the draw list is filled in is the order these are called in, by Gui::textField.
namespace RDA::TextEdit {

	void highlight(Frame& f) {
		// --- syntax highlighting ---
		// Re-lex only when the text or the language actually changed; an idle editor
		// reuses the cached spans.
		// The element's grammar wins over the variant's, so one `code` variant carries
		// the palette and each field says what it is holding.
		const bool ownLanguage = f.extras && f.extras->language && *f.extras->language;
		f.lang = ownLanguage ? f.gui.syntax().forField(std::string(f.extras->language))
		                     : f.gui.syntax().forField(f.style.language);
		if (f.lang && f.versioned) {
			// Hashing the text to notice a change is itself a walk of the whole string,
			// so a caller that knows is asked rather than measured.
			if (f.textEdited || f.version != f.st.tokenVersion || f.lang != f.st.tokenLang) {
				SyntaxRegistry::tokenize(*f.lang, f.text, f.st.tokens);
				f.st.tokenVersion = f.version;
				f.st.tokenLang = f.lang;
			}
		} else if (f.lang) {
			size_t hash = std::hash<std::string>{}(f.text);
			if (hash != f.st.tokenHash || f.lang != f.st.tokenLang) {
				SyntaxRegistry::tokenize(*f.lang, f.text, f.st.tokens);
				f.st.tokenHash = hash;
				f.st.tokenLang = f.lang;
			}
		} else if (f.st.tokenLang) {
			f.st.tokens.clear();
			f.st.tokenLang = nullptr;
			f.st.tokenHash = 0;
		}
	}

	void drawBackground(Frame& f) {
		const float fieldRadius = f.gui.motion().value(f.wid ^ kMotionRadius, f.style.radius,
		                                               f.style.motion.seconds, f.style.motion.curve);
		f.addFrame(f.rect, f.style.background, f.style.border, f.style.borderWidth, fieldRadius);
		if (f.gutterW > 0.0f) f.addRect({ f.rect.x, f.rect.y, f.gutterW, f.rect.h }, f.style.gutter);
		if (f.style.highlightCurrentLine && f.focused) {
			float y = f.contentTop + f.caretLine * f.lineH - f.st.scrollY;
			f.addRect({ f.contentLeft - 4.0f, y, f.viewW + 8.0f, f.lineH }, f.style.currentLine);
		}
	}

	void drawPlaceholder(Frame& f) {
		if (f.text.empty() && !f.placeholder.empty()) {
			f.addText(f.contentLeft - f.st.scrollX, f.contentTop + f.gui.baseline(), f.placeholder.c_str(), f.style.placeholder);
		}
	}

	void drawSelection(Frame& f) {
		// Selection highlight, drawn under the text.
		//
		// How much of it is there. Asked for on every frame, selection or not: a
		// value nobody asks for is dropped, and one that had been dropped would put
		// the next selection on the screen at full strength instead of fading it in.
		//
		// Nothing fades while the pointer is drawing it out. A drag is the hand, and
		// a highlight that lagged the hand would trail the words being swept over.
		// The same reason a double-clicked word arrives solid: that is a pointer
		// gesture too. What fades is a selection asked for from the keyboard --
		// select-all, or shift and a movement key.
		//
		// There is no fade *out*. A selection going is the one moment you need to be
		// certain it has gone, because the next thing typed either replaces it or
		// does not; a highlight lingering over text about to be overwritten says the
		// opposite of what is true. The value still runs down to zero while nothing
		// is drawn from it, so the next selection starts from where this one left.
		float selShown = 1.0f;
		if (f.style.motion.seconds > 0.0f) {
			const uint32_t selKey = f.wid ^ kMotionFill;
			const float wanted = (f.st.boxMode || hasLinearSel(f)) ? 1.0f : 0.0f;
			if (f.active == f.wid) {
				f.gui.motion().reset(selKey, wanted);
				selShown = wanted;
			} else {
				selShown = f.gui.motion().value(selKey, wanted, f.style.motion.seconds,
				                                f.style.motion.curve);
			}
		}

		if ((f.st.boxMode || hasLinearSel(f)) && selShown > 0.004f) {
			int caretL, caretC; caretToLineCol(f.starts, f.st.caret, caretL, caretC);
			// The edge that moves is the caret's, and the caret is already being
			// eased -- so the highlight is drawn to the same value. Left to compute
			// its own, the caret glides to its new column while the block it bounds
			// jumps there, and for the length of the glide the caret sits inside its
			// own selection.
			//
			// Only along the line it is on: part-way through a move between lines the
			// drawn x belongs to a line it is passing over, not to either end. And
			// not in box mode, whose edges are a rectangle rather than a caret.
			const bool caretOnItsLine = std::fabs(f.drawnCaretY -
				static_cast<float>(caretL) * f.lineH) < 0.5f;
			const bool caretLeads = f.st.caret >= f.st.selectAnchor;
			int loLine, hiLine, colA, colB;
			if (f.st.boxMode) {
				loLine = (std::min)(f.st.boxAnchorLine, caretL); hiLine = (std::max)(f.st.boxAnchorLine, caretL);
				colA = (std::min)(f.st.boxAnchorCol, caretC);    colB = (std::max)(f.st.boxAnchorCol, caretC);
			} else {
				int selA = (std::min)(f.st.selectAnchor, f.st.caret), selB = (std::max)(f.st.selectAnchor, f.st.caret);
				caretToLineCol(f.starts, selA, loLine, colA);
				caretToLineCol(f.starts, selB, hiLine, colB);
			}
			for (int ln = (std::max)(f.firstLine, loLine); ln <= (std::min)(f.lastLine, hiLine); ++ln) {
				int ll = lineLength(f.starts, f.text, ln);
				int c0 = f.st.boxMode ? (std::min)(colA, ll) : ((ln == loLine) ? colA : 0);
				int c1 = f.st.boxMode ? (std::min)(colB, ll) : ((ln == hiLine) ? colB : ll);
				float x0 = caretXInLine(f, f.starts[ln], c0);
				float x1 = caretXInLine(f, f.starts[ln], c1);
				if (!f.st.boxMode && caretOnItsLine && ln == caretL) {
					if (caretLeads) x1 = f.drawnCaretX; else x0 = f.drawnCaretX;
				}
				float w = x1 - x0 + ((!f.st.boxMode && ln != hiLine) ? 4.0f : 0.0f); // newline sliver
				float y = f.contentTop + ln * f.lineH - f.st.scrollY;
				if (w > 0.0f) {
					f.addRect({ f.contentLeft + x0 - f.st.scrollX, y, w, f.lineH },
					          fadeTo(f.style.selection, selShown));
				}
			}
		}
	}

	void drawText(Frame& f) {
		// One line of text: a single run in `style.text`, or — with a language set —
		// a chain of runs colored by the cached tokens. Tokens are sorted and
		// non-overlapping, so a line is walked by advancing through the ones that
		// intersect it; a token may start before the line (a multi-line string or
		// block comment), which the clamping below handles.
		const uint32_t plainColor = f.lang ? f.style.syntax.color(TokenKind::Plain) : f.style.text;
		auto drawLine = [&](int lineStart, int len, float x, float y) {
			const int lineEnd = lineStart + len;
			if (!f.lang) {
				f.addTextRange(x, y, f.text.data() + lineStart, len, plainColor);
				return;
			}
			auto it = std::lower_bound(f.st.tokens.begin(), f.st.tokens.end(), lineStart,
				[](const Token& t, int value) { return t.end <= value; });
			float penX = x;
			int i = lineStart;
			while (i < lineEnd) {
				if (it == f.st.tokens.end() || it->begin >= lineEnd) {
					penX = f.addTextRange(penX, y, f.text.data() + i, lineEnd - i, plainColor);
					break;
				}
				if (it->begin > i) { // plain gap before the next token
					penX = f.addTextRange(penX, y, f.text.data() + i, it->begin - i, plainColor);
					i = it->begin;
				}
				const int runEnd = (std::min)(lineEnd, it->end);
				penX = f.addTextRange(penX, y, f.text.data() + i, runEnd - i, f.style.syntax.color(it->kind));
				i = runEnd;
				if (it->end <= lineEnd) ++it; else break; // token continues on the next line
			}
		};

		for (int ln = f.firstLine; ln <= f.lastLine; ++ln) {
			float y = f.contentTop + ln * f.lineH - f.st.scrollY + f.font->ascent();
			drawLine(f.starts[ln], lineLength(f.starts, f.text, ln), f.contentLeft - f.st.scrollX, y);
		}
	}

	void drawSuggestion(Frame& f) {
		// The completion on offer, ahead of the caret.
		//
		// Drawn from the caret rather than from the end of the line, because that is
		// what it completes -- and one line only: a multi-line ghost would overdraw
		// whatever is below it, and reserving height for text that is not in the
		// value is a layout problem for a later day.
		if (f.focused && f.offering) {
			const float gx = f.contentLeft + f.drawnCaretX - f.st.scrollX;
			const float gy = f.contentTop + f.drawnCaretY - f.st.scrollY;
			std::string oneLine(f.extras->suggestion);
			if (const size_t brk = oneLine.find('\n'); brk != std::string::npos) {
				oneLine.resize(brk);
			}
			f.addText(gx, gy + f.font->ascent(), oneLine.c_str(), f.style.suggestion);
		}
	}

	void drawCaret(Frame& f) {
		// caret
		//
		// The blink was a square wave: solid for half a second, gone for half a
		// second, and the change between them instant. It still keeps that time; what
		// it no longer does is arrive and leave in one frame, which is the difference
		// between a cursor and something flashing at you.
		float caretAlpha = (std::fmod(f.st.blink, 1.0f) < 0.5f) ? 1.0f : 0.0f;
		if (f.style.motion.seconds > 0.0f) {
			// Clamped: the fade cannot be longer than the half-second it is a fade of,
			// or the caret would never reach either end and would only ever pulse.
			const float fade = (std::min)(f.style.motion.seconds, 0.2f);
			const float phase = std::fmod(f.st.blink, 1.0f);
			if (phase < 0.5f - fade)      caretAlpha = 1.0f;
			else if (phase < 0.5f)        caretAlpha = (0.5f - phase) / fade;
			else if (phase < 1.0f - fade) caretAlpha = 0.0f;
			else                          caretAlpha = (phase - (1.0f - fade)) / fade;
		}

		if (f.focused && !f.composing && caretAlpha > 0.004f) {
			float cx = f.contentLeft + f.drawnCaretX - f.st.scrollX;
			float cy = f.contentTop + f.drawnCaretY - f.st.scrollY;
			f.addRect({ cx, cy + 1.0f, f.style.caretWidth, f.lineH - 2.0f },
			          fadeTo(f.style.caret, caretAlpha));
		}
	}

	void drawLineNumbers(Frame& f) {
		// Line numbers in the gutter (clipped to it, scrolled vertically only).
		if (f.gutterW > 0.0f && f.font) {
			f.pushClip(glm::vec4(f.rect.x, f.rect.y, f.rect.x + f.gutterW, f.rect.y + f.rect.h));
			int firstLine = (std::max)(0, static_cast<int>(f.st.scrollY / f.lineH));
			int lastLine = (std::min)(f.lineCount - 1, firstLine + static_cast<int>(f.rect.h / f.lineH) + 1);
			for (int ln = firstLine; ln <= lastLine; ++ln) {
				// Formatted into a stack buffer: line numbers are redrawn every frame, and
				// std::to_string would construct a string per visible line.
				char number[16];
				int digits = std::snprintf(number, sizeof(number), "%d", ln + 1);
				float w = f.font->textWidth(number, number + digits);
				float y = f.contentTop + ln * f.lineH - f.st.scrollY + f.font->ascent();
				f.addTextRange(f.rect.x + f.gutterW - 6.0f - w, y, number, digits, f.style.lineNumber);
			}
			f.popClip();
		}
	}

	void drawScrollBar(Frame& f) {
		// Scroll bar on the right strip. Asked for on every frame of a multiline field,
		// scrollable this frame or not, so that typing past the bottom fades one in
		// rather than making one appear.
		if (f.style.multiline) {
			const bool needed = f.maxScrollY > 0.0f;
			const float frac = needed ? (f.st.scrollY / f.maxScrollY) : 0.0f;
			const Rect real{ f.scrollTrack.x, f.scrollTrack.y + frac * f.trackRange,
			                 f.scrollTrack.w, f.thumbH };
			const bool thumbHot = (f.active == f.scrollId) ||
			                      (needed && real.contains(f.in.pointer));
			const Gui::ScrollBarLook bar = f.gui.scrollBar(f.wid ^ kMotionKnob, needed, thumbHot,
			                                               f.thumbH, f.scrollTrack.h, f.style);
			if (bar.presence > 0.004f) {
				f.gui.pushOpacity(bar.presence);
				f.addRect(f.scrollTrack, f.style.scrollTrack);
				const float span = (std::max)(0.0f, f.scrollTrack.h - bar.thumb);
				f.addRectRounded(Rect{ f.scrollTrack.x, f.scrollTrack.y + frac * span,
				                       f.scrollTrack.w, bar.thumb },
				                 bar.colour, f.scrollTrack.w * 0.5f);
				f.gui.popOpacity();
			}
		}
	}
}
