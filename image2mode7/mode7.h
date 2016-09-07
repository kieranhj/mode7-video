#pragma once

#include "CImg.h"
using namespace cimg_library;

#define MODE7_COL0			151
#define MODE7_COL1			32
#define MODE7_BLANK			32
#define MODE7_WIDTH			40
#define MODE7_HEIGHT		25
#define MODE7_MAX_SIZE		(MODE7_WIDTH * MODE7_HEIGHT)

#define JPG_W				src._width		// w				// 76
#define JPG_H				src._height		// h				// 57 = 4:3  // 42 = 16:9
#define FRAME_WIDTH			(JPG_W/2)
#define FRAME_HEIGHT		(JPG_H/3)
#define FRAME_SIZE			(MODE7_WIDTH * FRAME_HEIGHT)
#define FRAME_FIRST_COLUMN	(MODE7_WIDTH - FRAME_WIDTH)

#define MODE7_BLACK_BG		156
#define MODE7_NEW_BG		157
#define MODE7_HOLD_GFX		158
#define MODE7_RELEASE_GFX	159
#define MODE7_GFX_COLOUR	144

#define CLAMP(a,low,high)	((a) < (low) ? (low) : ((a) > (high) ? (high) : (a)))
#define THRESHOLD(a,t)		((a) >= (t) ? 255 : 0)

#define MAX_STATE (1U << 14)
#define GET_STATE(fg,bg,hold_mode,last_gfx_char)	((last_gfx_char) << 7 | (hold_mode) << 6 | ((bg) << 3) | (fg))

void clear_error_char_arrays(void);
int get_state_for_char(unsigned char proposed_char, int old_state);
int get_colour_from_rgb(unsigned char r, unsigned char g, unsigned char b);
unsigned char pixel_to_grey(int mode, unsigned char r, unsigned char g, unsigned char b);
int get_error_for_char(int x7, int y7, unsigned char proposed_char, int fg, int bg, bool hold_mode, unsigned char last_gfx_char);
unsigned char get_graphic_char_from_image(int x7, int y7, int fg, int bg);
int get_error_for_remainder_of_line(int x7, int y7, int fg, int bg, bool hold_mode, unsigned char last_gfx_char);

extern CImg<unsigned char> src;
extern unsigned char mode7[MODE7_MAX_SIZE];
extern unsigned char output[MODE7_WIDTH];
extern unsigned char char_for_xpos_in_state[MAX_STATE][MODE7_WIDTH + 1];

extern bool global_use_hold;
extern bool global_use_fill;
