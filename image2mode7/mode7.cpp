// mode7video.cpp : Defines the entry point for the console application.
//

//#include "stdafx.h"
#include "mode7.h"

#define _ZERO_FRAME_PRESET	TRUE		// whether our zero frame is already setup for MODE 7

#define _COLOUR_DEBUG		FALSE

CImg<unsigned char> src;
unsigned char mode7[MODE7_MAX_SIZE];

static int total_error_in_state[MAX_STATE][MODE7_WIDTH + 1];
unsigned char char_for_xpos_in_state[MAX_STATE][MODE7_WIDTH + 1];
unsigned char output[MODE7_WIDTH];

bool global_use_hold = true;
bool global_use_fill = true;

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

	return GET_STATE(fg, bg, hold_mode, last_gfx_char);
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

// Functions - get_error_for_char(int x7, int y7, unsigned char code, int fg, int bg, unsigned char hold_char)
int get_error_for_char(int x7, int y7, unsigned char proposed_char, int fg, int bg, bool hold_mode, unsigned char last_gfx_char)
{
	int x = (x7 - (MODE7_WIDTH - FRAME_WIDTH)) * 2;
	int y = y7 * 3;
	int error = 0;

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

	unsigned char screen_r, screen_g, screen_b;
	unsigned char image_r, image_g, image_b;

	// These are the pixels that will get written to the screen

	screen_r = screen_char & 1 ? GET_RED_FROM_COLOUR(fg) : GET_RED_FROM_COLOUR(bg);
	screen_g = screen_char & 1 ? GET_GREEN_FROM_COLOUR(fg) : GET_GREEN_FROM_COLOUR(bg);
	screen_b = screen_char & 1 ? GET_BLUE_FROM_COLOUR(fg) : GET_BLUE_FROM_COLOUR(bg);

	// These are the pixels in the image

	image_r = src(x, y, 0);
	image_g = src(x, y, 1);
	image_b = src(x, y, 2);

	// Calculate the error between them

	error += ((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b));

	screen_r = screen_char & 2 ? GET_RED_FROM_COLOUR(fg) : GET_RED_FROM_COLOUR(bg);
	screen_g = screen_char & 2 ? GET_GREEN_FROM_COLOUR(fg) : GET_GREEN_FROM_COLOUR(bg);
	screen_b = screen_char & 2 ? GET_BLUE_FROM_COLOUR(fg) : GET_BLUE_FROM_COLOUR(bg);

	image_r = src(x+1, y, 0);
	image_g = src(x+1, y, 1);
	image_b = src(x+1, y, 2);

	error += ((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b));

	screen_r = screen_char & 4 ? GET_RED_FROM_COLOUR(fg) : GET_RED_FROM_COLOUR(bg);
	screen_g = screen_char & 4 ? GET_GREEN_FROM_COLOUR(fg) : GET_GREEN_FROM_COLOUR(bg);
	screen_b = screen_char & 4 ? GET_BLUE_FROM_COLOUR(fg) : GET_BLUE_FROM_COLOUR(bg);

	image_r = src(x, y+1, 0);
	image_g = src(x, y+1, 1);
	image_b = src(x, y+1, 2);

	error += ((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b));

	screen_r = screen_char & 8 ? GET_RED_FROM_COLOUR(fg) : GET_RED_FROM_COLOUR(bg);
	screen_g = screen_char & 8 ? GET_GREEN_FROM_COLOUR(fg) : GET_GREEN_FROM_COLOUR(bg);
	screen_b = screen_char & 8 ? GET_BLUE_FROM_COLOUR(fg) : GET_BLUE_FROM_COLOUR(bg);

	image_r = src(x+1, y + 1, 0);
	image_g = src(x+1, y + 1, 1);
	image_b = src(x+1, y + 1, 2);

	error += ((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b));

	screen_r = screen_char & 16 ? GET_RED_FROM_COLOUR(fg) : GET_RED_FROM_COLOUR(bg);
	screen_g = screen_char & 16 ? GET_GREEN_FROM_COLOUR(fg) : GET_GREEN_FROM_COLOUR(bg);
	screen_b = screen_char & 16 ? GET_BLUE_FROM_COLOUR(fg) : GET_BLUE_FROM_COLOUR(bg);

	image_r = src(x, y + 2, 0);
	image_g = src(x, y + 2, 1);
	image_b = src(x, y + 2, 2);

	error += ((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b));

	screen_r = screen_char & 64 ? GET_RED_FROM_COLOUR(fg) : GET_RED_FROM_COLOUR(bg);
	screen_g = screen_char & 64 ? GET_GREEN_FROM_COLOUR(fg) : GET_GREEN_FROM_COLOUR(bg);
	screen_b = screen_char & 64 ? GET_BLUE_FROM_COLOUR(fg) : GET_BLUE_FROM_COLOUR(bg);

	image_r = src(x + 1, y + 2, 0);
	image_g = src(x + 1, y + 2, 1);
	image_b = src(x + 1, y + 2, 2);

	error += ((screen_r - image_r) * (screen_r - image_r)) + ((screen_g - image_g) * (screen_g - image_g)) + ((screen_b - image_b) * (screen_b - image_b));

	// For all six pixels in the character cell

	return error;
}

unsigned char get_graphic_char_from_image(int x7, int y7, int fg, int bg)
{
	unsigned char current_char = mode7[(y7 * MODE7_WIDTH) + (x7)];
	int x = (x7 - FRAME_FIRST_COLUMN) * 2;
	int y = y7 * 3;
	unsigned char graphic_char = 0;

	if (current_char != MODE7_BLANK)
	{
		// We have some pixels in this cell
		// Calculate graphic character - if pixel == bg colour then off else on

		graphic_char = 32 +																				// bit 5 always set!
			+(get_colour_from_rgb(src(x, y, 0), src(x, y, 1), src(x, y, 2)) == bg ? 0 : 1)						// (x,y) = bit 0
			+ (get_colour_from_rgb(src(x + 1, y, 0), src(x + 1, y, 1), src(x + 1, y, 2)) == bg ? 0 : 2)					// (x+1,y) = bit 1
			+ (get_colour_from_rgb(src(x, y + 1, 0), src(x, y + 1, 1), src(x, y + 1, 2)) == bg ? 0 : 4)					// (x,y+1) = bit 2
			+ (get_colour_from_rgb(src(x + 1, y + 1, 0), src(x + 1, y + 1, 1), src(x + 1, y + 1, 2)) == bg ? 0 : 8)			// (x+1,y+1) = bit 3
			+ (get_colour_from_rgb(src(x, y + 2, 0), src(x, y + 2, 1), src(x, y + 2, 2)) == bg ? 0 : 16)				// (x,y+2) = bit 4
			+ (get_colour_from_rgb(src(x + 1, y + 2, 0), src(x + 1, y + 2, 1), src(x + 1, y + 2, 2)) == bg ? 0 : 64);			// (x+1,y+2) = bit 6
	}
	else
	{
		graphic_char = MODE7_BLANK;
	}

	return graphic_char;
}

int get_error_for_remainder_of_line(int x7, int y7, int fg, int bg, bool hold_mode, unsigned char last_gfx_char)
{
	if (x7 >= MODE7_WIDTH)
		return 0;

	int state = GET_STATE(fg, bg, hold_mode, last_gfx_char);

	if (total_error_in_state[state][x7] != -1)
		return total_error_in_state[state][x7];

//	printf("get_error_for_remainder_of_line(%d, %d, %d, %d, %d, %d)\n", x7, y7, fg, bg, hold_char, prev_char);

	unsigned char graphic_char = get_graphic_char_from_image(x7, y7, fg, bg);
	int lowest_error = INT_MAX;
	unsigned char lowest_char = 'Z';

	// Possible characters are: 1 + 1 + 6 + 1 + 1 + 1 + 1 = 12 possibilities x 40 columns = 12 ^ 40 combinations.  That's not going to work :)
	// Possible states for a given cell: fg=0-7, bg=0-7, hold_gfx=6 pixels : total = 12 bits = 4096 possible states
	// Wait! What about prev_char as part of state if want to use hold graphics feature? prev_char=6 pixels so actually 18 bits = 262144 possible states
	// Not all of them can be visited as we cannot arbitrarily set the previous character or hold character but still needs a 40Mb array of ints! :S

	// Graphic char (if set)
	// Stay blank (if not)
	// Set graphic colour (colour != fg) - 6
	// Fill (if bg != fg)
	// No fill (if bg != 0)
	// Hold graphics - TODO
	// Release graphics- TODO

	// If we have a graphic character or blank we could use this!
	if (graphic_char == MODE7_BLANK)
	{
		int newstate = GET_STATE(fg, bg, hold_mode, graphic_char);
		int error = get_error_for_char(x7, y7, graphic_char, fg, bg, hold_mode, graphic_char);
		int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, graphic_char);

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

	// If the background is black we could enable fill! - you idiot - can enable fill at any time if fg colour has changed since last time!
	if (global_use_fill)
	{
		if (bg != fg)
		{
			// Bg colour becomes fg colour immediately in this cell
			int newstate = GET_STATE(fg, fg, hold_mode, last_gfx_char);
			int error = get_error_for_char(x7, y7, MODE7_NEW_BG, fg, fg, hold_mode, last_gfx_char);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, fg, hold_mode, last_gfx_char);

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
			int newstate = GET_STATE(fg, 0, hold_mode, last_gfx_char);
			int error = get_error_for_char(x7, y7, MODE7_BLACK_BG, fg, 0, hold_mode, last_gfx_char);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, 0, hold_mode, last_gfx_char);

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

	// We could enter hold graphics mode!
	if (global_use_hold)
	{
		if (!hold_mode)
		{
			int newstate = GET_STATE(fg, bg, true, last_gfx_char);
			int error = get_error_for_char(x7, y7, MODE7_HOLD_GFX, fg, bg, true, last_gfx_char);			// hold control code does adopt last graphic character immediately
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, true, last_gfx_char);

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
			int newstate = GET_STATE(fg, bg, false, MODE7_BLANK);
			int error = get_error_for_char(x7, y7, MODE7_RELEASE_GFX, fg, bg, false, MODE7_BLANK);
			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, false, MODE7_BLANK);

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
			int newstate = GET_STATE(c, bg, hold_mode, last_gfx_char);

			// The fg colour doesn't actually take effect until next cell - so any hold char here will be in current fg colour
			int error = get_error_for_char(x7, y7, MODE7_GFX_COLOUR + c, fg, bg, hold_mode, last_gfx_char);			// old state

			int remaining = get_error_for_remainder_of_line(x7 + 1, y7, c, bg, hold_mode, last_gfx_char);			// new state

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

	if (graphic_char != MODE7_BLANK)
	{
		int newstate = GET_STATE(fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK);
		int error = get_error_for_char(x7, y7, graphic_char, fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK);
		int remaining = get_error_for_remainder_of_line(x7 + 1, y7, fg, bg, hold_mode, global_use_hold ? graphic_char : MODE7_BLANK);

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

//	printf("(%d, %d) returning char=%d lowest error=%d\n", x7, y7, lowest_char, lowest_error);

	output[x7] = lowest_char;

	return lowest_error;
}
