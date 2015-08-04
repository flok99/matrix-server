// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2014 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "bdf-font.h"
#include "utf8-internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// The little question-mark box "�" for unknown code.
static const uint32_t kUnicodeReplacementCodepoint = 0xFFFD;

// Bitmap for one row. This limits the number of available columns.
// Make wider if running into trouble.
typedef uint32_t rowbitmap_t;

namespace rgb_matrix {
	struct Font::Glyph {
		int width, height;
		int y_offset;
		rowbitmap_t bitmap[0];  // contains 'height' elements.
	};

	Font::Font() : font_height_(-1) {}
	Font::~Font() {
		for (CodepointGlyphMap::iterator it = glyphs_.begin();
				it != glyphs_.end(); ++it) {
			free(it->second);
		}
	}

	// TODO: that might not be working for all input files yet.
	bool Font::LoadFont(const char *path) {
		if (!path || !*path) return false;
		FILE *f = fopen(path, "r");
		if (f == NULL)
			return false;
		printf("load font start\n");
		uint32_t codepoint;
		char buffer[1024];
		int dummy;
		Glyph tmp;
		Glyph *current_glyph = NULL;
		int row = 0;
		int x_offset = 0;
		int bitmap_shift = 0;
		while (fgets(buffer, sizeof(buffer), f)) {
			if (sscanf(buffer, "FONTBOUNDINGBOX %d %d %d %d",
						&dummy, &font_height_, &dummy, &base_line_) == 4) {
				base_line_ += font_height_;
			}
			else if (sscanf(buffer, "ENCODING %ud", &codepoint) == 1) {
				// parsed.
			}
			else if (sscanf(buffer, "BBX %d %d %d %d", &tmp.width, &tmp.height,
						&x_offset, &tmp.y_offset) == 4) {
				current_glyph = (Glyph*) malloc(sizeof(Glyph)
						+ tmp.height * sizeof(rowbitmap_t));
				*current_glyph = tmp;
				// We only get number of bytes large enough holding our width. We want
				// it always left-aligned.
				bitmap_shift =
					8 * (sizeof(rowbitmap_t) - ((current_glyph->width + 7) / 8)) + x_offset;
				row = -1;  // let's not start yet, wait for BITMAP
			}
			else if (strncmp(buffer, "BITMAP", strlen("BITMAP")) == 0) {
				row = 0;
			}
			else if (current_glyph && row >= 0 && row < current_glyph->height
					&& (sscanf(buffer, "%x", &current_glyph->bitmap[row]) == 1)) {
				current_glyph->bitmap[row] <<= bitmap_shift;
				row++;
			}
			else if (strncmp(buffer, "ENDCHAR", strlen("ENDCHAR")) == 0) {
				if (current_glyph && row == current_glyph->height) {
					free(glyphs_[codepoint]);  // just in case there was one.
					glyphs_[codepoint] = current_glyph;
					current_glyph = NULL;
				}
			}
		}
		fclose(f);
		printf("load font finished\n");
		return true;
	}

	const Font::Glyph *Font::FindGlyph(uint32_t unicode_codepoint) const {
		CodepointGlyphMap::const_iterator found = glyphs_.find(unicode_codepoint);
		if (found == glyphs_.end())
			return NULL;
		return found->second;
	}

	int Font::CharacterWidth(const uint32_t unicode_codepoint) const {
		const Glyph *g = FindGlyph(unicode_codepoint);

		if (!g)
			return 0;

		return g->width;
	}

	int Font::DrawGlyph(Canvas *c, int x_pos, int y_pos, const Color &color, uint32_t unicode_codepoint) const {
		const Glyph *g = FindGlyph(unicode_codepoint);
		if (g == NULL) g = FindGlyph(kUnicodeReplacementCodepoint);
		if (g == NULL) return 0;
		y_pos = y_pos - g->height - g->y_offset;
		for (int y = 0; y < g->height; ++y) {
			const rowbitmap_t row = g->bitmap[y];
			rowbitmap_t x_mask = 0x80000000;
			for (int x = 0; x < g->width; ++x, x_mask >>= 1) {
				if (row & x_mask) {
					c->SetPixel(x_pos + x, y_pos + y, color.r, color.g, color.b);
				}
			}
		}
		return g->width;
	}

	int Font::DrawGlyph(char *const p, int x_pos, int w, int y_pos, const Color &color, uint32_t unicode_codepoint, bool invert, bool underline) const {
		const Glyph *g = FindGlyph(unicode_codepoint);

		if (!g)
			return 0;

		y_pos = y_pos - g->height - g->y_offset;
		for (int y = 0; y < g->height; ++y) {
			rowbitmap_t x_mask = 0x80000000;
			rowbitmap_t row = g->bitmap[y];

			if (underline && y == g -> height - 1)
				row ^= 0xaaaaaaaa;

			if (invert)
				row ^= 0xffffffff;

			for (int x = 0; x < g->width; ++x, x_mask >>= 1) {
				if (row & x_mask) {
					int offset = (y_pos + y) * w * 3 + (x_pos + x) * 3;
					p[offset + 0] = color.r;
					p[offset + 1] = color.g;
					p[offset + 2] = color.b;
				}
			}
		}

		return g->width;
	}

	int Font::TextWidth(const Font &font, const char *text) {
		int x = 0;

		while (*text) {
			if (*text == '#')
			{
				text += 7;
				continue;
			}
			if (*text == '$')
			{
				if (text[1] == '$' || text[1] == '#')
					text += 1;
				else
					text += 2;

				continue;
			}

			const uint32_t cp = utf8_next_codepoint(text);
			x += CharacterWidth(cp);
		}

		return x;
	}

	int htv(char c)
	{
		c = toupper(c);

		if (c >= 'A')
			return c - 'A' + 10;

		return c - '0';
	}

	double hue_to_rgb(double m1, double m2, double h)
	{
		if (h < 0.0) h += 1.0;
		if (h > 1.0) h -= 1.0;
		if (6.0 * h < 1.0)
			return (m1 + (m2 - m1) * h * 6.0);
		if (2.0 * h < 1.0) 
			return m2; 
		if (3.0 * h < 2.0) 
			return (m1 + (m2 - m1) * ((2.0 / 3.0) - h) * 6.0);

		return m1;
	}

	void hls_to_rgb(double H, double L, double S, double *r, double *g, double *b)
	{
		if (S == 0)
			*r = *g = *b = L;
		else
		{
			double m2;

			if (L <=0.5)
				m2 = L*(1.0+S);
			else
				m2 = L+S-L*S;

			double m1 = 2.0 * L - m2;

			*r = hue_to_rgb(m1, m2, H + 1.0/3.0);
			*g = hue_to_rgb(m1, m2, H);
			*b = hue_to_rgb(m1, m2, H - 1.0/3.0);
		}
	}


	int Font::DrawText(char *buffer, const Font & font, int x, int w, const char *utf8_text) {
		const int start_x = x;

		Color color(255, 255, 255);

		int h = font.baseline();
		bool invert = false, underline = false, rnd_color = false, word_rnd_color = false;

		while (*utf8_text) {
			if (*utf8_text == '#')
			{
				color.r = (htv(utf8_text[1]) << 4) | htv(utf8_text[2]);
				color.g = (htv(utf8_text[3]) << 4) | htv(utf8_text[4]);
				color.b = (htv(utf8_text[5]) << 4) | htv(utf8_text[6]);

				if (color.r == 0 && color.g == 0 && color.b == 0)
					color.r = color.g = color.b = 0x22;

				utf8_text += 7;
				continue;
			}

			if (*utf8_text == '$')
			{
				utf8_text++;

				if (*utf8_text == '$' || *utf8_text == '#')
					goto just_draw;

				if (*utf8_text == 'i')
					invert = !invert;

				if (*utf8_text == 'u')
					underline = !underline;

				if (*utf8_text == 'c')
					rnd_color = !rnd_color;

				if (*utf8_text == 'C')
					word_rnd_color = !word_rnd_color;

				if (*utf8_text == 'F')
				{
					// handled elsewhere
				}

				utf8_text++;

				continue;
			}

just_draw:
			if (rnd_color || (word_rnd_color && *utf8_text == ' '))
			{
				double r, g, b;
				hls_to_rgb((double)(rand() % 32767) / 32767.0, 0.5, 1.0, &r, &g, &b);
				color.r = (int)(r * 255);
				color.g = (int)(g * 255);
				color.b = (int)(b * 255);
			}

			const uint32_t cp = utf8_next_codepoint(utf8_text);
			x += font.DrawGlyph(buffer, x, w, h, color, cp, invert, underline);
		}

		return x - start_x;
	}

	void Font::PutTextBitmap(Canvas *c, int buf_x, int w, const char *const buffer, bool repeat) {
		if (w <= 0)
			return;

		int disp_x = 0;
		while(disp_x < c -> width())
		{
			int target_w = std::min(w - buf_x, c -> width() - disp_x);

			for(int yc=0; yc<c -> height(); yc++)
			{
				for(int xc=0; xc<target_w; xc++)
				{
					int offset = yc * w * 3 + (buf_x + xc) * 3;

					c->SetPixel(xc + disp_x, yc, buffer[offset + 0], buffer[offset + 1], buffer[offset + 2]);
				}
			}

			if (!repeat)
				break;

			buf_x = 0;
			disp_x += target_w;
		}
	}
}
