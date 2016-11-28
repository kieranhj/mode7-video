// mode7video.cpp : Defines the entry point for the console application.
//

//#include "stdafx.h"
#include "CImg.h"

using namespace cimg_library;

#define MODE7_COL0			151
#define MODE7_COL1			(sep ? MODE7_SEP_GFX : 32)
#define MODE7_BLANK			32
#define MODE7_WIDTH			40
#define MODE7_HEIGHT		25
#define MODE7_MAX_SIZE		(MODE7_WIDTH * MODE7_HEIGHT)

#define MODE7_GFX_COLOUR	144

#define MODE7_CONTIG_GFX	153
#define MODE7_SEP_GFX		154
#define MODE7_BLACK_BG		156
#define MODE7_NEW_BG		157
#define MODE7_HOLD_GFX		158
#define MODE7_RELEASE_GFX	159

#define IMAGE_W				(src._width)
#define IMAGE_H				(src._height)

#define MODE7_PIXEL_W		78
#define MODE7_PIXEL_H		75

#define FRAME_WIDTH			(frame_width)
#define FRAME_HEIGHT		(frame_height)

#define NUM_FRAMES			frames			// 5367		// 5478
#define FRAME_SIZE			(MODE7_WIDTH * FRAME_HEIGHT)

#define FRAME_FIRST_COLUMN	1				// (MODE7_WIDTH - FRAME_WIDTH)

#define FILENAME			shortname
#define DIRECTORY			shortname

#define _ZERO_FRAME_PRESET	FALSE		// whether our zero frame is already setup for MODE 7

#define MAX_STATE			(1U << 15)
#define GET_STATE(fg,bg,hold_mode,last_gfx_char,sep)	( (sep) << 14 | (last_gfx_char) << 7 | (hold_mode) << 6 | ((bg) << 3) | (fg))

#define IMAGE_X_FROM_X7(x7)	(((x7) - FRAME_FIRST_COLUMN) * 2)
#define IMAGE_Y_FROM_Y7(x7)	((y7) * 3)

#define MAX_3(A,B,C)		((A)>(B)?((A)>(C)?(A):(C)):(B)>(C)?(B):(C))
#define MIN_3(A,B,C)		((A)<(B)?((A)<(C)?(A):(C)):(B)<(C)?(B):(C))

#define CLAMP(a,low,high)	((a) < (low) ? (low) : ((a) > (high) ? (high) : (a)))
#define THRESHOLD(a,t)		((a) >= (t) ? 255 : 0)
#define LO(a)				((a) % 256)
#define HI(a)				((a) / 256)

#define SAFE_SRC(x,y,c)		(((x)<0||(x)>=src._width)?0:(((y)<0||(y)>=src._height)?0:(src((x),(y),(c)))))
//#define SAFE_SRC(x,y,c)		(((x)<0||(x)>=src._width)?0:(((y)<0||(y)>=src._height)?0:(src((x),(y),(0)))))

#define _USE_16_BIT_PACK	TRUE

#if _USE_16_BIT_PACK
#define BYTES_PER_DELTA		2
#else
#define BYTES_PER_DELTA		3
#endif

static CImg<unsigned char> src;
static unsigned char mode7[MODE7_MAX_SIZE];
static unsigned char prevmode7[MODE7_MAX_SIZE];
static unsigned char delta[MODE7_MAX_SIZE];

static int total_error_in_state[MAX_STATE][MODE7_WIDTH + 1];
static unsigned char char_for_xpos_in_state[MAX_STATE][MODE7_WIDTH + 1];
static unsigned char output[MODE7_WIDTH];

static bool global_use_hold = true;
static bool global_use_fill = true;
static bool global_use_sep = true;
static bool global_use_geometric = true;
static bool global_try_all = false;

static int global_sep_fg_factor = 128;

static int frame_width;
static int frame_height;

void clear_error_char_arrays(void)
{
	for (int state = 0; state < MAX_STATE; state++)
	{
		for (int x = 0; x <= MODE7_WIDTH; x++)
		{
			total_error_in_state[state][x] = -1;
			char_for_xpos_in_state[state][x] = 'X';
		}
	}
}

int get_state_for_char(unsigned char proposed_char, int old_state)
{
	int fg = old_state & 7;
	int bg = (old_state >> 3) & 7;
	int hold_mode = (old_state >> 6) & 1;
	unsigned char last_gfx_char = (old_state >> 7) & 0x7f;
	int sep = (old_state >> 14) & 1;

	if (global_use_fill)
	{
		if (proposed_char == MODE7_NEW_BG)
		{
			bg = fg;
		}

		if (proposed_char == MODE7_BLACK_BG)
		{
			bg = 0;
		}
	}

	if (proposed_char > MODE7_GFX_COLOUR && proposed_char < MODE7_GFX_COLOUR + 8)
	{
		fg = proposed_char - MODE7_GFX_COLOUR;
	}

	if (global_use_hold)
	{
		if (proposed_char == MODE7_HOLD_GFX)
		{
			hold_mode = true;
		}

		if (proposed_char == MODE7_RELEASE_GFX)
		{
			hold_mode = false;
			last_gfx_char = MODE7_BLANK;
		}

		if (proposed_char < 128)
		{
			last_gfx_char = proposed_char;
		}
	}
	else
	{
		hold_mode = false;
		last_gfx_char = MODE7_BLANK;
	}

	if (global_use_sep)
	{
		if (proposed_char == MODE7_SEP_GFX)
		{
			sep = true;
		}

		if (proposed_char == MODE7_CONTIG_GFX)
		{
			sep = false;
		}
	}

	return GET_STATE(fg, bg, hold_mode, last_gfx_char, sep);
}


int get_colour_from_rgb(unsigned char r, unsigned char g, unsigned char b)
{
	return (r ? 1 : 0) + (g ? 2 : 0) + (b ? 4 : 0);
}

#define GET_RED_FROM_COLOUR(c)		(c & 1 ? 255:0)
#define GET_GREEN_FROM_COLOUR(c)	(c & 2 ? 255:0)
#define GET_BLUE_FROM_COLOUR(c)		(c & 4 ? 255:0)

unsigned char pixel_to_grey(int mode, unsigned char r, unsigned char g, unsigned char b)
{
	switch (mode)
	{
	case 1:
		return r;

	case 2:
		return g;

	case 3:
		return b;

	case 4:
		return (unsigned char)((r + g + b) / 3);

	case 5:
		return (unsigned char)(0.2126f * r + 0.7152f * g + 0.0722f * b);

	default:
		return 0;
	}
}

// For each character cell on this line
// Do we have pixels or not?
// If we have pixels then need to decide whether is it better to replace this cell with a control code or use a graphic character
// If we don't have pixels then need to decide whether it is better to insert a control code or leave empty
// Possible control codes are: new fg colour, fill (bg colour = fg colour), no fill (bg colour = black), hold graphics (hold char = prev char), release graphics (hold char = empty)
// "Better" means that the "error" for the rest of the line (appearance on screen vs actual image = deviation) is minimised

// Hold graphics mode means use last known (used on the line) graphic character in place of space when emitting a control code (reset if using alphanumerics not graphics)
// Palette order = black - red - green - yellow - blue - magenta - cyan - white
// Brightness order = black - blue - red - magenta - green - cyan - yellow - white
// Hue order = red - yellow - green - cyan - blue - magenta - red

// Luma values
// B = 0 ~= 0
// B = 18 = 18 ~= 1x
// R = 54 = 18 + 36 ~= 3x
// M = 73 = 18 + 36 + 19 ~= 4x
// G = 182 = 18 + 36 + 19 + 109 ~= 10x
// C = 201 = 18 + 36 + 19 + 109 + 19 ~= 11x
// Y = 237 = 18 + 36 + 19 + 109 + 19 + 36 ~= 13x
// W = 255 = 18 + 36 + 19 + 109 + 19 + 36 + 18 ~= 14x

static int error_colour_vs_colour[8][8] = {

#if 0		// This maps error to luma when comparing colours against black/white
	{ 0, 3, 10, 13, 1, 4, 11, 14 },		// black
	{ 3, 0, 8, 4, 8, 4, 12, 11 },		// red
	{ 10, 8, 0, 4, 8, 12, 4, 4 },		// green
	{ 13, 4, 4, 0, 12, 8, 8, 1 },		// yellow
	{ 1, 8, 8, 12, 0, 4, 4, 13 },		// blue
	{ 4, 4, 12, 8, 4, 0, 8, 10 },		// magenta
	{ 11, 12, 4, 8, 4, 8, 0, 3 },		// cyan
	{ 14, 11, 4, 1, 13, 10, 3, 0 },		// white
#else		// This maps colours in brightness order when comparing against black/white
	{ 0, 2, 4, 6, 1, 3, 5, 7 },		// black
	{ 2, 0, 4, 2, 4, 2, 6, 5 },		// red
	{ 4, 4, 0, 2, 4, 6, 2, 3 },		// green
	{ 6, 2, 2, 0, 6, 4, 4, 1 },		// yellow
	{ 1, 4, 4, 6, 0, 2, 2, 6 },		// blue
	{ 3, 2, 6, 4, 2, 0, 4, 4 },		// magenta
	{ 5, 6, 2, 4, 2, 4, 0, 2 },		// cyan
	{ 7, 5, 3, 1, 6, 4, 2, 0 },		// white
#endif
};

int error_function(int screen_r, int screen_g, int screen_b, int image_r, int image_g, int image_b)
{
	if (global_use_geometric)
	{
		return (((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b))); // / (255 * 255);
	}
	else
	{
		// Use lookup
		return error_colour_vs_colour[get_colour_from_rgb(screen_r, screen_g, screen_b)][get_colour_from_rgb(image_r, image_g, image_b)];
	}
}

int get_error_for_screen_pixel(int x, int y, int screen_bit, int fg, int bg, bool sep)
{
	int screen_r, screen_g, screen_b;
	int image_r, image_g, image_b;

	// These are the pixels that will get written to the screen

	if (screen_bit)
	{
		if (sep)
		{
			screen_r = (global_sep_fg_factor * GET_RED_FROM_COLOUR(fg) + (255 - global_sep_fg_factor) * GET_RED_FROM_COLOUR(bg)) / 255;
			screen_g = (global_sep_fg_factor * GET_GREEN_FROM_COLOUR(fg) + (255 - global_sep_fg_factor) * GET_GREEN_FROM_COLOUR(bg)) / 255;
			screen_b = (global_sep_fg_factor * GET_BLUE_FROM_COLOUR(fg) + (255 - global_sep_fg_factor) * GET_BLUE_FROM_COLOUR(bg)) / 255;
		}
		else
		{
			screen_r = GET_RED_FROM_COLOUR(fg);
			screen_g = GET_GREEN_FROM_COLOUR(fg);
			screen_b = GET_BLUE_FROM_COLOUR(fg);
		}
	}
	else
	{
		screen_r = GET_RED_FROM_COLOUR(bg);
		screen_g = GET_GREEN_FROM_COLOUR(bg);
		screen_b = GET_BLUE_FROM_COLOUR(bg);
	}

	// These are the pixels in the image

	image_r = SAFE_SRC(x, y, 0);
	image_g = SAFE_SRC(x, y, 1);
	image_b = SAFE_SRC(x, y, 2);

	// Calculate the error between them

	return error_function(screen_r, screen_g, screen_b, image_r, image_g, image_b);
}

int get_error_for_screen_char(int x7, int y7, unsigned char screen_char, int fg, int bg, bool sep)
{
	int x = IMAGE_X_FROM_X7(x7);
	int y = IMAGE_Y_FROM_Y7(y7);

	int error = 0;

	error += get_error_for_screen_pixel(x, y, screen_char & 1, fg, bg, sep);

	error += get_error_for_screen_pixel(x + 1, y, screen_char & 2, fg, bg, sep);

	error += get_error_for_screen_pixel(x, y + 1, screen_char & 4, fg, bg, sep);

	error += get_error_for_screen_pixel(x + 1, y + 1, screen_char & 8, fg, bg, sep);

	error += get_error_for_screen_pixel(x, y + 2, screen_char & 16, fg, bg, sep);

	error += get_error_for_screen_pixel(x + 1, y + 2, screen_char & 64, fg, bg, sep);

	// For all six pixels in the character cell

	return error;
}

// Functions - get_error_for_char(int x7, int y7, unsigned char code, int fg, int bg, unsigned char hold_char)
int get_error_for_char(int x7, int y7, unsigned char proposed_char, int fg, int bg, bool hold_mode, unsigned char last_gfx_char, bool sep)
{
	// If proposed character >= 128 then this is a control code
	// If so then the hold char will be displayed on screen
	// Otherwise it will be our proposed character (pixels)

	unsigned char screen_char;

	if (hold_mode)
	{
		screen_char = (proposed_char >= 128) ? last_gfx_char : proposed_char;
	}
	else
	{
		screen_char = (proposed_char >= 128) ? MODE7_BLANK : proposed_char;
	}

	return get_error_for_screen_char(x7, y7, screen_char, fg, bg, sep);
}

unsigned char get_graphic_char_from_image(int x7, int y7, int fg, int bg, bool sep)
{
	int min_error = INT_MAX;
	int on_error, off_error;
	unsigned char min_char = 32;
	unsigned char direct_char = 0;

	int x = IMAGE_X_FROM_X7(x7);
	int y = IMAGE_Y_FROM_Y7(y7);

	// Try every possible combination of pixels to get lowest error

	on_error = get_error_for_screen_pixel(x, y, 1, fg, bg, sep);
	off_error = get_error_for_screen_pixel(x, y, 0, fg, bg, sep);
	min_char += (on_error < off_error ? 1 : 0);

	on_error = get_error_for_screen_pixel(x + 1, y, 1, fg, bg, sep);
	off_error = get_error_for_screen_pixel(x + 1, y, 0, fg, bg, sep);
	min_char += (on_error < off_error ? 2 : 0);

	on_error = get_error_for_screen_pixel(x, y + 1, 1, fg, bg, sep);
	off_error = get_error_for_screen_pixel(x, y + 1, 0, fg, bg, sep);
	min_char += (on_error < off_error ? 4 : 0);

	on_error = get_error_for_screen_pixel(x + 1, y + 1, 1, fg, bg, sep);
	off_error = get_error_for_screen_pixel(x + 1, y + 1, 0, fg, bg, sep);
	min_char += (on_error < off_error ? 8 : 0);

	on_error = get_error_for_screen_pixel(x, y + 2, 1, fg, bg, sep);
	off_error = get_error_for_screen_pixel(x, y + 2, 0, fg, bg, sep);
	min_char += (on_error < off_error ? 16 : 0);

	on_error = get_error_for_screen_pixel(x + 1, y + 2, 1, fg, bg, sep);
	off_error = get_error_for_screen_pixel(x + 1, y + 2, 0, fg, bg, sep);
	min_char += (on_error < off_error ? 64 : 0);

	return min_char;
}

int get_error_for_remainder_of_line(int x7, int y7, int fg, int bg, bool hold_mode, unsigned char last_gfx_char, bool sep)
{
	if (x7 >= MODE7_WIDTH)
		return 0;

	int state = GET_STATE(fg, bg, hold_mode, last_gfx_char, sep);

	if (total_error_in_state[state][x7] != -1)
		return total_error_in_state[state][x7];

	//	printf("get_error_for_remainder_of_line(%d, %d, %d, %d, %d, %d)\n", x7, y7, fg, bg, hold_char, prev_char);

	unsigned char graphic_char = global_try_all ? 'Y' : get_graphic_char_from_image(x7, y7, fg, bg, sep);
	int lowest_error = INT_MAX;
	unsigned char lowest_char = 'Z';

	// Possible characters are: 1 + 1 + 6 + 1 + 1 + 1 + 1 = 12 possibilities x 40 columns = 12 ^ 40 combinations.  That's not going to work :)
	// Possible states for a given cell: fg=0-7, bg=0-7, hold_gfx=6 pixels : total = 12 bits = 4096 possible states
	// Wait! What about prev_char as part of state if want to use hold graphics feature? prev_char=6 pixels so actually 18 bits = 262144 possible states
	// Not all of them can be visited as we cannot arbitrarily set the previous character or hold character but still needs a 40Mb array of ints! :S

	// Graphic char (if set)
	// Stay blank (if not)
	// Set graphic colour (colour != fg) x6
	// Fill (if bg != fg)
	// No fill (if bg != 0)
	// Hold graphics (if hold_mode == false)
	// Release graphics (if hold_mode == true)

	// Always try a blank first
	if (MODE7_BLANK)
	{
		int newstate = GET_STATE(fg, bg, hold_mode, MODE7_BLANK, sep);
		int error = get_error_for_char(x7, y7, MODE7_BLANK, fg, bg, hold_mode, MODE7_BLANK, sep);
		int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, MODE7_BLANK, sep);

		if (total_error_in_state[newstate][x7 + 1] == -1)
		{
			total_error_in_state[newstate][x7 + 1] = remaining;
			char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
		}

		error += remaining;

		if (error < lowest_error)
		{
			lowest_error = error;
			lowest_char = MODE7_BLANK;
		}
	}

	// If the background is black we could enable fill! - you idiot - can enable fill at any time if fg colour has changed since last time!
	if (global_use_fill)
	{
		if (bg != fg)
		{
			// Bg colour becomes fg colour immediately in this cell
			int newstate = GET_STATE(fg, fg, hold_mode, last_gfx_char, sep);
			int error = get_error_for_char(x7, y7, MODE7_NEW_BG, fg, fg, hold_mode, last_gfx_char, sep);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, fg, hold_mode, last_gfx_char, sep);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_NEW_BG;
			}
		}

		// If the background is not black we could disable fill!
		if (bg != 0)
		{
			// Bg colour becomes black immediately in this cell
			int newstate = GET_STATE(fg, 0, hold_mode, last_gfx_char, sep);
			int error = get_error_for_char(x7, y7, MODE7_BLACK_BG, fg, 0, hold_mode, last_gfx_char, sep);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, 0, hold_mode, last_gfx_char, sep);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_BLACK_BG;
			}
		}
	}

	// We could enter seperated graphics mode?
	if (global_use_sep)
	{
		if (!sep)
		{
			int newstate = GET_STATE(fg, bg, hold_mode, last_gfx_char, true);
			int error = get_error_for_char(x7, y7, MODE7_SEP_GFX, fg, bg, hold_mode, last_gfx_char, true);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, last_gfx_char, true);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_SEP_GFX;
			}
		}

		// We could go back to contiguous graphics...
		else
		{
			int newstate = GET_STATE(fg, bg, hold_mode, last_gfx_char, false);
			int error = get_error_for_char(x7, y7, MODE7_CONTIG_GFX, fg, bg, hold_mode, last_gfx_char, false);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, last_gfx_char, false);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_CONTIG_GFX;
			}
		}
	}

	// We could enter hold graphics mode!
	if (global_use_hold)
	{
		if (!hold_mode)
		{
			int newstate = GET_STATE(fg, bg, true, last_gfx_char, sep);
			int error = get_error_for_char(x7, y7, MODE7_HOLD_GFX, fg, bg, true, last_gfx_char, sep);			// hold control code does adopt last graphic character immediately
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, true, last_gfx_char, sep);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_HOLD_GFX;
			}
		}

		// We could exit hold graphics mode..
		else
		{
			int newstate = GET_STATE(fg, bg, false, MODE7_BLANK, sep);
			int error = get_error_for_char(x7, y7, MODE7_RELEASE_GFX, fg, bg, false, MODE7_BLANK, sep);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, false, MODE7_BLANK, sep);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_RELEASE_GFX;
			}
		}
	}

	for (int c = 1; c < 8; c++)
	{
		// We could change our fg colour!
		if (c != fg)
		{
			int newstate = GET_STATE(c, bg, hold_mode, last_gfx_char, sep);

			// The fg colour doesn't actually take effect until next cell - so any hold char here will be in current fg colour
			int error = get_error_for_char(x7, y7, MODE7_GFX_COLOUR + c, fg, bg, hold_mode, last_gfx_char, sep);			// old state

			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, c, bg, hold_mode, last_gfx_char, sep);			// new state

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = MODE7_GFX_COLOUR + c;
			}
		}
	}

	if (global_try_all)
	{
		// Try every possible graphic character...

		for (int i = 1; i < 64; i++)
		{
			graphic_char = (MODE7_BLANK) | (i & 0x1f) | ((i & 0x20) << 1);

			int newstate = GET_STATE(fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK, sep);
			int error = get_error_for_char(x7, y7, graphic_char, fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK, sep);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK, sep);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = graphic_char;
			}
		}
	}
	else
	{
		// Try our graphic character (if it's not blank)

		if (graphic_char != MODE7_BLANK)
		{
			int newstate = GET_STATE(fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK, sep);
			int error = get_error_for_char(x7, y7, graphic_char, fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK, sep);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK, sep);

			if (total_error_in_state[newstate][x7 + 1] == -1)
			{
				total_error_in_state[newstate][x7 + 1] = remaining;
				char_for_xpos_in_state[newstate][x7 + 1] = output[x7 + 1];
			}

			error += remaining;

			if (error < lowest_error)
			{
				lowest_error = error;
				lowest_char = graphic_char;
			}
		}
	}
	//	printf("(%d, %d) returning char=%d lowest error=%d\n", x7, y7, lowest_char, lowest_error);

	output[x7] = lowest_char;

	return lowest_error;
}

int match_closest_palette_colour(unsigned char r, unsigned char g, unsigned char b)
{
	int min_error = INT_MAX;
	int min_colour = -1;

	for (int c = 0; c < 8; c++)
	{
		unsigned char cr = GET_RED_FROM_COLOUR(c);
		unsigned char cg = GET_GREEN_FROM_COLOUR(c);
		unsigned char cb = GET_BLUE_FROM_COLOUR(c);

		int error = ((cr - r) * (cr - r)) + ((cg - g) * (cg - g)) + ((cb - b) * (cb - b));

		if (error < min_error)
		{
			min_error = error;
			min_colour = c;
		}
	}

	return min_colour;
}


int main(int argc, char **argv)
{
	cimg_usage("MODE 7 video convertor.\n\nUsage : mode7video [options]");
	const int frames = cimg_option("-n", 0, "Last frame number");
	const int start = cimg_option("-s", 1, "Start frame number");
	const char *const shortname = cimg_option("-i", (char*)0, "Input (directory / short name)");
	const char *const ext = cimg_option("-e", (char*)"png", "Image format file extension");
	const bool save = cimg_option("-save", false, "Save individual MODE7 frames");
	const bool sep = cimg_option("-sep", false, "Separated graphics by default");

	const int sat = cimg_option("-sat", 64, "Saturation threshold (below this colour is considered grey)");
	const int value = cimg_option("-val", 64, "Value threshold (below this colour is considered black)");
	const int black = cimg_option("-black", 64, "Black threshold (grey below this considered pure black - above is colour brightness ramp)");
	const int white = cimg_option("-white", 128, "White threshold (grey above this considered pure white - below is colour brightness ramp)");
	bool use_quant = cimg_option("-quant", false, "Quantise the input image to 3-bit MODE 7 palette using HSV params above");
	const bool no_hold = cimg_option("-nohold", false, "Disallow Hold Graphics control code");
	const bool no_fill = cimg_option("-nofill", false, "Disallow New Background control code");
	const bool use_sep = cimg_option("-usesep", false, "Enable Separated Graphics control code");
	const int sep_factor = cimg_option("-fore", 128, "Contribution factor of foreground vs background colour for separated graphics");
	const bool no_scale = cimg_option("-noscale", false, "Don't scale the image image to MODE 7 resolution");
	const bool simg = cimg_option("-test", false, "Save test images (quantised / scaled) before Teletext conversion");
	const bool inf = cimg_option("-inf", false, "Save inf file for output file");
	const bool verbose = cimg_option("-v", false, "Verbose output");
	const bool url = cimg_option("-url", false, "Spit out URL for edit.tf");
	const bool error_lookup = cimg_option("-lookup", false, "Use lookup table for colour error (default is geometric distance)");
	const bool try_all = cimg_option("-slow", false, "Calculate full line error for every possible graphics character (64x slower)");

	if (cimg_option("-h", false, 0)) std::exit(0);
	if (shortname == NULL)  std::exit(0);

	global_use_hold = !no_hold;
	global_use_fill = !no_fill;
	global_use_sep = use_sep;
	global_sep_fg_factor = sep_factor;

	global_use_geometric = !error_lookup;
	global_try_all = try_all;

	char filename[256];
	char input[256];

	int totaldeltas = 0;
	int totalbytes = 0;
	int maxdeltas = 0;
	int resetframes = 0;
	int skipdeltas = 0;

	unsigned char *beeb = (unsigned char *) malloc(MODE7_MAX_SIZE * NUM_FRAMES);
	unsigned char *ptr = beeb;

	int *delta_counts = (int *)malloc(sizeof(int) * (NUM_FRAMES+1));

	memset(mode7, 0, MODE7_MAX_SIZE);
	memset(prevmode7, 0, MODE7_MAX_SIZE);
	memset(delta, 0, MODE7_MAX_SIZE);
	memset(delta_counts, 0, sizeof(int) * (NUM_FRAMES+1));

	FILE *file;

	// Blank MODE 7 gfx screen

	for (int i = 0; i < MODE7_MAX_SIZE; i++)
	{
#if _ZERO_FRAME_PRESET
		switch (i % MODE7_WIDTH)
		{
		case 0:
			prevmode7[i] = MODE7_COL0;
			break;

		case 1:
			prevmode7[i] = MODE7_COL1;
			break;

		default:
			prevmode7[i] = MODE7_BLANK;
			break;
		}
#else
		prevmode7[i] = MODE7_BLANK;
#endif
	}

	for (int n = start; n <= NUM_FRAMES; n++)
	{
		sprintf(input, "%s\\frames\\%s-%d.%s", DIRECTORY, FILENAME, n, ext);
		src.assign(input);

		if (verbose)
		{
			printf("[%d/%d] ", n, NUM_FRAMES);
		}
		else
		{
			printf("\rFrame: %d/%d", n, NUM_FRAMES);
		}

		//
		// Colour conversion etc.
		//

		if (!use_quant)
		{
			if (verbose)
			{
				printf("-noquant ");
			}
		}
		else
		{
			if (verbose)
			{
				printf("-quant ");
			}

			// Convert to HSV

			cimg_forXY(src, x, y)
			{
				unsigned char R = SAFE_SRC(x, y, 0);
				unsigned char G = SAFE_SRC(x, y, 1);
				unsigned char B = SAFE_SRC(x, y, 2);

				unsigned char r, g, b;
				r = g = b = 0;

				unsigned char M = MAX_3(R, G, B);
				unsigned char m = MIN_3(R, G, B);

				unsigned char C = M - m;				// Chroma - black to white

				unsigned char Hc = 0;					// Hue - as BBC colour palette

				if (C != 0)
				{
					if (M == R)
					{
						int h = 255 * (G - B) / C;

						if (h > 127) Hc = 3;			// yellow
						else if (h < -128) Hc = 5;		// magenta
						else Hc = 1;					// red
					}
					else if (M == G)
					{
						int h = 255 * (B - R) / C;

						if (h > 127) Hc = 6;			// cyan
						else if (h < -128) Hc = 3;		// yellow
						else Hc = 2;					// green
					}
					else if (M == B)
					{
						int h = 255 * (R - G) / C;

						if (h > 127) Hc = 5;			// magenta
						else if (h < -128) Hc = 6;		// cyan
						else Hc = 2;					// blue
					}
				}

				unsigned char Y = (unsigned char)(0.2126f * R + 0.7152f * G + 0.0722f * B);		// Luma (screen brightess)

				unsigned char V = M;					// Value

				int S = 0;								// Saturation

				if (C != 0)
				{
					S = 255 * C / V;
				}

				// If saturation too low assume grey

				if (S < sat)
				{
					// Grey
					// Adjust colour palette for grey scale
					// Map value to colour ramp - change RAMP!

					unsigned char Gc = 0;
					int midpoint = (white - black) / 2;

					if (V < black)
						Gc = 0;
					else if (V < (black + midpoint))
						Gc = 4;			// blue
					else if (V < white)
						Gc = 6;			// cyan
					else
						Gc = 7;			// white		// could use yellow?

					r = GET_RED_FROM_COLOUR(Gc);
					g = GET_GREEN_FROM_COLOUR(Gc);
					b = GET_BLUE_FROM_COLOUR(Gc);
				}
				else
				{
					// Colour
					// If Value is too low then assume black

					if (V < value)
					{
						// Black
						r = g = b = 0;
					}
					else
					{
						// Not black = full colour

						int c = match_closest_palette_colour(R, G, B);

						r = GET_RED_FROM_COLOUR(c);
						g = GET_GREEN_FROM_COLOUR(c);
					b = GET_BLUE_FROM_COLOUR(c);
					}
				}

				src(x, y, 0) = r;
				src(x, y, 1) = g;
				src(x, y, 2) = b;
			}

			//
			// Save output of colour conversion for debug
			//

			if (simg)
			{
				if (verbose)
				{
					printf("-test '%s\\test\\%s-%d.quant.png' ", DIRECTORY, FILENAME, n);
				}

				sprintf(filename, "%s\\test\\%s-%d.quant.png", DIRECTORY, FILENAME, n);
				src.save(filename);
			}
		}

		//
		// Resize!
		//

		int pixel_width, pixel_height;

		if (no_scale)
		{
			if (verbose)
			{
				printf("pixels=%dx%d ", src._width, src._height);
			}

			pixel_width = src._width;
			pixel_height = src._height;
		}
		else
		{
			// Calculate frame size - adjust to width
			pixel_width = (MODE7_WIDTH - FRAME_FIRST_COLUMN) * 2;
			pixel_height = pixel_width * IMAGE_H / IMAGE_W;
			if (pixel_height % 3) pixel_height += (3 - (pixel_height % 3));

			// Adjust to height
			if (pixel_height > MODE7_PIXEL_H)
			{
				pixel_height = MODE7_PIXEL_H;
				pixel_width = pixel_height * IMAGE_W / IMAGE_H;

				if (pixel_width % 1) pixel_width++;

				// Need to handle reset of background if frame_width < MODE7_WIDTH
			}

			// Resize image to this size

			if (verbose)
			{
				printf("resize=%dx%d -> %dx%d ", src._width, src._height, pixel_width, pixel_height);
			}

			src.resize(pixel_width, pixel_height);

			// Save test images for debug

			if (simg)
			{
				if (verbose)
				{
					printf(" -test '%s\\test\\%s-%d.small.png' ", n, DIRECTORY, FILENAME, n);
				}

				sprintf(filename, "%s\\test\\%s-%d.small.png", DIRECTORY, FILENAME, n);
				src.save(filename);
			}
		}

		//
		// Conversion to MODE 7
		//

		int frame_error = 0;

		frame_width = pixel_width / 2;
		frame_height = pixel_height / 3;

		if (verbose)
		{
			printf("frame=%dx%d ", frame_width, frame_height);
		}

		// Set everything to blank
		memset(mode7, MODE7_BLANK, MODE7_MAX_SIZE);

		for (int y7 = 0; y7 < frame_height; y7++)
		{
			int y = IMAGE_Y_FROM_Y7(y7);

			// Reset state as starting new character row
			// State = fg colour + bg colour + hold character + prev character
			// For each character cell on this line
			// Do we have pixels or not?
			// If we have pixels then need to decide whether is it better to replace this cell with a control code or keep pixels
			// Possible control codes are: new fg colour, fill (bg colour = fg colour), no fill (bg colour = black), hold graphics (hold char = prev char), release graphics (hold char = empty)
			// "Better" means that the "error" for the rest of the line (appearance on screen vs actual image = deviation) is minimised

			// Clear our array of error values for each state & x position
			clear_error_char_arrays();

			int min_error = INT_MAX;
			int min_colour = 0;

			// Determine best initial state for line
			for (int fg = 7; fg > 0; fg--)
			{
				// What would our first character look like in this state?
				unsigned char first_char = get_graphic_char_from_image(FRAME_FIRST_COLUMN, y7, fg, 0, false);

				// What's the error for that character?
				int error = get_error_for_char(FRAME_FIRST_COLUMN, y7, first_char, fg, 0, false, MODE7_BLANK, false);

				// Find the lowest error corresponding to our possible start states
				if (error < min_error)
				{
					min_error = error;
					min_colour = fg;
			}
		}

			// This is our initial state of the line
			int state = GET_STATE(min_colour, 0, false, MODE7_BLANK, false);

			// Set this state before frame begins
			mode7[(y7 * MODE7_WIDTH) + (FRAME_FIRST_COLUMN - 1)] = MODE7_GFX_COLOUR + min_colour;

			// Kick off recursive error calculation with that state
			int error = get_error_for_remainder_of_line(FRAME_FIRST_COLUMN, y7, min_colour, 0, false, MODE7_BLANK, false);

			frame_error += error;

			// Store first character
			char_for_xpos_in_state[state][FRAME_FIRST_COLUMN] = output[FRAME_FIRST_COLUMN];

			// Copy the resulting character data into MODE 7 screen
			for (int x7 = FRAME_FIRST_COLUMN; x7 < (FRAME_FIRST_COLUMN + FRAME_WIDTH); x7++)
			{
				// Copy character chosen in this position for this state
				unsigned char best_char = char_for_xpos_in_state[state][x7];

				mode7[(y7 * MODE7_WIDTH) + (x7)] = best_char;

				// Update the state
				state = get_state_for_char(best_char, state);
			}

			// For when image is narrower than screen width

			if (FRAME_FIRST_COLUMN + FRAME_WIDTH < MODE7_WIDTH)
			{
				mode7[(y7 * MODE7_WIDTH) + FRAME_FIRST_COLUMN + FRAME_WIDTH] = MODE7_BLACK_BG;
			}

			// printf("\n");

			y += 2;
		}

		if (verbose)
		{
			printf("error=%d ", frame_error);
		}

//		for (int i = 0; i < 1000; i++)
//		{
//			printf("0x%x ", mode7[i]);
//			if (i % 40 == 39)printf("\n");
//		}

		if (n == start)
		{
			*ptr++ = LO(FRAME_SIZE);
			*ptr++ = HI(FRAME_SIZE);

			totalbytes += 2;
		}

		// How many deltas?
		int numdeltas = 0;
		int numdeltabytes = 0;

		for (int i = 0; i < FRAME_SIZE; i++)
		{
			if (mode7[i] == prevmode7[i])
			{
				delta[i] = 0;
			}
			else
			{
			//	printf("N=%d mode7[%d]=%x prev[%d]=%x\n", n, i, mode7[i], i, prevmode7[i]);
				delta[i] = mode7[i];
				numdeltas++;
			}
		}

		totaldeltas += numdeltas;
		if (numdeltas > maxdeltas) maxdeltas = numdeltas;
		delta_counts[n] = numdeltas;

		if (numdeltas > FRAME_SIZE/BYTES_PER_DELTA)
		{
			numdeltabytes = FRAME_SIZE;
			resetframes++;

			if (verbose)
			{
				printf("*RESET* (%x) ", ptr - beeb);
			}

			*ptr++ = 0;
			*ptr++ = 0xff;

			memcpy(ptr, mode7, FRAME_SIZE);
			ptr += FRAME_SIZE;
		}
		else
		{
			unsigned char *numdeltas_ptr = ptr;

			*ptr++ = LO(numdeltas);
			*ptr++ = HI(numdeltas);

			int previ = 0;

			for (int i = 0; i < FRAME_SIZE; i++)
			{
				if (delta[i] != 0)
				{
					unsigned char byte = mode7[i];			//  ^ prevmode7[i] for EOR with prev.

#if _USE_16_BIT_PACK
					unsigned short offset = (i - previ);		// could be up to 10 bits
					unsigned char data = 0;
					unsigned char flag = 1;

					if (byte < 128)
					{
						// Graphics character

						flag = 0;										// bit 9 is our control code flag
						data = (byte & 31) | ((byte & 64) >> 1);		// mask out bit 5, shift down bit 6
					}
					else if (byte > MODE7_GFX_COLOUR && byte < MODE7_GFX_COLOUR + 8)
					{
						// Colour code
						unsigned char code = 1;				// code 1 = colour
						unsigned char colour = byte - MODE7_GFX_COLOUR;

						data = (colour << 3) | code;
					}
					else if (byte >= MODE7_CONTIG_GFX && byte < 160)
					{
						unsigned char code = 2;				// code 2 = control
						unsigned char control = byte - (MODE7_CONTIG_GFX - 1);

						data = (control << 3) | code;
					}

					// Check if offset is 10-bits!

					if (offset > 0x1ff)
					{
						// Insert a control code to skip offset 511

						if (verbose)
						{
							printf("*SKIP* (%x) ", ptr - beeb);					// NEED TO GO BACK AND UPDATE #DELTAS IN STREAM ABOVE!
						}

						// This control code does nothing

						unsigned short skip = (0 < 10) | (1 << 9) | (0x1ff);
						*ptr++ = LO(skip);
						*ptr++ = HI(skip);

						// But does reduce our offset to 9-bits

						offset -= 0x1ff;

						// We added a delta
						numdeltas++;

						// Remember how many skips
						skipdeltas++;
					}

					// Pack our delta value into 16-bits

					unsigned short pack = (data << 10) | (flag << 9) | (offset);

					*ptr++ = LO(pack);
					*ptr++ = HI(pack);
#else
					// No pack
					*ptr++ = LO((i - previ));
					*ptr++ = HI((i - previ));
					*ptr++ = byte;
#endif

					previ = i;								// or 0 for offset from screen start
				}
			}

			// Poke the updated numdeltas value into our stream
			*numdeltas_ptr++ = LO(numdeltas);
			*numdeltas_ptr++ = HI(numdeltas);

			// Calculate number of bytes
			numdeltabytes = numdeltas * BYTES_PER_DELTA;
		}

		if (verbose)
		{
			printf("deltas=%d bytes=%d ", numdeltas, numdeltabytes);
		}

		totalbytes += 2 + numdeltabytes;

		if (save)
		{
			if (verbose)
			{
				printf("-save '%s\\bin\\%s-%d.bin' ", DIRECTORY, FILENAME, n);
			}

			sprintf(filename, "%s\\bin\\%s-%d.bin", DIRECTORY, FILENAME, n);
			file = fopen((const char*)filename, "wb");

			if (file)
			{
				fwrite(mode7, 1, FRAME_SIZE, file);
				fclose(file);
			}

			if (verbose)
			{
				printf("-save '%s\\bin\\%s-%d.delta.bin' ", DIRECTORY, FILENAME, n);
			}

			sprintf(filename, "%s\\delta\\%s-%d.delta.bin", DIRECTORY, FILENAME, n);
			file = fopen((const char*)filename, "wb");

			if (file)
			{
				fwrite(delta, 1, FRAME_SIZE, file);
				fclose(file);
			}
			
			if( inf )
			{
				if (verbose)
				{
					printf("-inf '%s\\bin\\%s-%d.bin.inf' ", DIRECTORY, FILENAME, n);
				}

				sprintf(filename, "%s\\inf\\%s-%d.bin.inf", DIRECTORY, FILENAME, n);
				file = fopen((const char*)filename, "wb");

				if (file)
				{
					char buffer[256];
					sprintf(buffer, "$.IMG%d\tFF7C00 FF7C00\n", n);
					fwrite(buffer, 1, strlen(buffer), file);
					fclose(file);
				}
			}
		}

		memcpy(prevmode7, mode7, MODE7_MAX_SIZE);

		if (verbose)
		{
			printf("\n");
		}
	}

	*ptr++ = 0xff;
	*ptr++ = 0xff;

	int total_frames = NUM_FRAMES - start + 1;
	printf("\ntotal frames = %d\n", total_frames);
	printf("frame size = %d\n", FRAME_SIZE);
	printf("total deltas = %d\n", totaldeltas);
	printf("total bytes = %d\n", totalbytes);
	printf("max deltas = %d\n", maxdeltas);
	printf("reset frames = %d\n", resetframes);
	printf("skip deltas = %d\n", skipdeltas);
	printf("deltas / frame = %f\n", totaldeltas / (float)total_frames);
	printf("bytes / frame = %f\n", totalbytes / (float)total_frames);
	printf("bytes / second = %f\n", 25.0f * totalbytes / (float)total_frames);
	printf("beeb size = %d bytes\n", ptr - beeb);

	sprintf(filename, "%s\\%s_beeb.bin", DIRECTORY, FILENAME);
	file = fopen((const char*)filename, "wb");

	if (file)
	{
		fwrite(beeb, 1, ptr-beeb, file);
		fclose(file);
	}

	free(beeb);
	free(delta_counts);

    return 0;
}
