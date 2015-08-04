// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Very simple graphics library to do simple things.
//
// Might be useful to consider using Cairo instead and just have an interface
// between that and the RGBMatrix. Well, this is a quick set of things to get
// started (and nicely self-contained).
#ifndef RPI_GRAPHICS_H
#define RPI_GRAPHICS_H

#include "led-matrix.h"

#include <map>
#include <stdint.h>

namespace rgb_matrix {
struct Color {
	Color(uint8_t rr, uint8_t gg, uint8_t bb) : r(rr), g(gg), b(bb) {}
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

// Font loading bdf files. If this ever becomes more types, just make virtual
// base class.
class Font {
	public:
		// Initialize font, but it is only usable after LoadFont() has been called.
		Font();
		~Font();

		bool LoadFont(const char *path);

		// Return height of font in pixels. Returns -1 if font has not been loaded.
		int height() const { return font_height_; }

		// Return baseline. Pixels from the topline to the baseline.
		int baseline() const { return base_line_; }

		// Return width of given character, or -1 if font is not loaded or character
		// does not exist.
		int CharacterWidth(const uint32_t unicode_codepoint) const;

		int TextWidth(const Font &font, const char *text);
		int DrawText(char *buffer, const Font & font, int x, int w, const char *utf8_text);
		void PutTextBitmap(Canvas *c, int buf_x, int w, const char *const buffer, bool repeat);

		// Draws the unicode character at position "x","y" with "color". The "y"
		// position is the baseline of the font.
		// If we don't have it in the font, draws the replacement character "�" if
		// available.
		// Returns how much we advance on the screen, which is the width of the
		// character or 0 if we didn't draw any chracter.
		int DrawGlyph(Canvas *c, int x_pos, int y_pos, const Color &color, uint32_t unicode_codepoint) const;
		int DrawGlyph(char *const p, int x_pos, int w, int y_pos, const Color &color, uint32_t unicode_codepoint, bool invert, bool underline) const;

	private:
		struct Glyph;
		typedef std::map<uint32_t, Glyph*> CodepointGlyphMap;

		const Glyph *FindGlyph(uint32_t codepoint) const;

		int font_height_;
		int base_line_;
		CodepointGlyphMap glyphs_;
};
}

#endif  // RPI_GRAPHICS_H
