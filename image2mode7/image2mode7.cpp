// image2mode7.cpp : Defines the entry point for the console application.
//

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include "mode7.h"

int main(int argc, char **argv)
{
	cimg_usage("MODE 7 image convertor.\n\nUsage : image2mode7 [options]");
	const char *const input_name = cimg_option("-i", (char*)0, "Input filename");
	const char *const output_name = cimg_option("-0", "mode7.bin", "Output filename");
	const int gmode = cimg_option("-g", 0, "Colour to greyscale conversion (0=none, 1=red only, 2=green only, 3=blue only, 4=simple average, 5=luminence preserving");
	const int thresh = cimg_option("-t", 127, "B&W threshold value");
	const int dither = cimg_option("-d", 0, "Dither mode (0=none/threshold only, 1=floyd steinberg, 2=ordered 2x2, 3=ordered 3x3");
	const bool no_hold = cimg_option("-nohold", false, "Disallow Hold Graphics control code");
	const bool no_fill = cimg_option("-nofill", false, "Disallow New Background control code");
	const bool simg = cimg_option("-simg", false, "Save test image (post-quantisation) before Teletext conversion");
	const bool verbose = cimg_option("-v", false, "Verbose output");

	char filename[256];
	FILE *file;

	if (cimg_option("-h", false, 0)) std::exit(0);
	if (input_name == NULL)  std::exit(0);

	global_use_hold = !no_hold;
	global_use_fill = !no_fill;

	memset(mode7, 0, MODE7_MAX_SIZE);

	src.assign(input_name);

	// Convert to greyscale from RGB

	if (gmode)
	{
		cimg_forXY(src, x, y)
		{
			src(x, y, 0) = pixel_to_grey(gmode, src(x, y, 0), src(x, y, 1), src(x, y, 2));
			src(x, y, 1) = pixel_to_grey(gmode, src(x, y, 0), src(x, y, 1), src(x, y, 2));
			src(x, y, 2) = pixel_to_grey(gmode, src(x, y, 0), src(x, y, 1), src(x, y, 2));
		}
	}

	// Dithering

	if (dither == 0)					// None / threshold
	{
		cimg_forXY(src, x, y)
		{
			src(x, y, 0) = THRESHOLD(src(x, y, 0), thresh);
			src(x, y, 1) = THRESHOLD(src(x, y, 1), thresh);
			src(x, y, 2) = THRESHOLD(src(x, y, 2), thresh);
		}
	}
	else if (dither == 1)				// Floyd Steinberg
	{
		cimg_forXY(src, x, y)
		{
			int grey = src(x, y, 0);
			src(x, y, 0) = THRESHOLD(grey, 128);
			int error = grey - src(x, y, 0);

			if (x < src._width - 1)
			{
				grey = src(x + 1, y, 0) + error * 7 / 16;
				src(x + 1, y, 0) = CLAMP(grey, 0, 255);
			}

			if (y < src._height - 1)
			{
				if (x > 0)
				{
					grey = src(x - 1, y + 1, 0) + error * 3 / 16;
					src(x - 1, y + 1, 0) = CLAMP(grey, 0, 255);
				}

				grey = src(x, y + 1, 0) + error * 5 / 16;
				src(x, y + 1, 0) = CLAMP(grey, 0, 255);

				if (x < src._width - 1)
				{
					grey = src(x + 1, y + 1, 0) + error * 1 / 16;
					src(x + 1, y + 1, 0) = CLAMP(grey, 0, 255);
				}

			}
		}
	}
	else if (dither == 2)						// Ordered dither 2x2
	{
		cimg_forY(src, y)
		{
			cimg_forX(src, x)
			{
				int grey = src(x, y, 0) * 5 / 256;
				src(x, y, 0) = THRESHOLD(grey, 1);

				if (x < src._width - 1)
				{
					grey = src(x + 1, y, 0) * 5 / 256;
					src(x + 1, y, 0) = THRESHOLD(grey, 3);
				}

				if (y < src._height - 1)
				{
					grey = src(x, y + 1, 0) * 5 / 256;
					src(x, y + 1, 0) = THRESHOLD(grey, 4);

					if (x < src._width - 1)
					{
						grey = src(x + 1, y + 1, 0) * 5 / 256;
						src(x + 1, y + 1, 0) = THRESHOLD(grey, 2);
					}
				}
				x++;
			}
			y++;
		}
	}
	else if (dither == 3)						// Ordered dither 3x3
	{
		cimg_forY(src, y)
		{
			cimg_forX(src, x)
			{
				int grey = src(x, y, 0) * 10 / 256;
				src(x, y, 0) = THRESHOLD(grey, 1);

				if (x < src._width - 1)
				{
					grey = src(x + 1, y, 0) * 10 / 256;
					src(x + 1, y, 0) = THRESHOLD(grey, 8);
				}

				if (x < src._width - 2)
				{
					grey = src(x + 2, y, 0) * 10 / 256;
					src(x + 2, y, 0) = THRESHOLD(grey, 4);
				}

				if (y < src._height - 1)
				{
					grey = src(x, y + 1, 0) * 10 / 256;
					src(x, y + 1, 0) = THRESHOLD(grey, 7);

					if (x < src._width - 1)
					{
						grey = src(x + 1, y + 1, 0) * 10 / 256;
						src(x + 1, y + 1, 0) = THRESHOLD(grey, 6);
					}

					if (x < src._width - 2)
					{
						grey = src(x + 2, y + 1, 0) * 10 / 256;
						src(x + 2, y + 1, 0) = THRESHOLD(grey, 3);
					}
				}

				if (y < src._height - 2)
				{
					grey = src(x, y + 2, 0) * 10 / 256;
					src(x, y + 2, 0) = THRESHOLD(grey, 5);

					if (x < src._width - 1)
					{
						grey = src(x + 1, y + 2, 0) * 10 / 256;
						src(x + 1, y + 2, 0) = THRESHOLD(grey, 2);
					}

					if (x < src._width - 2)
					{
						grey = src(x + 2, y + 2, 0) * 10 / 256;
						src(x + 2, y + 2, 0) = THRESHOLD(grey, 9);
					}
				}

				x += 2;
			}

			y += 2;
		}
	}

	if (simg)
	{
		sprintf(filename, "%s.png", output_name);
		src.save(filename);
	}

	// Conversion to MODE 7

	cimg_forY(src, y)
	{
		int y7 = y / 3;
		mode7[y7 * MODE7_WIDTH] = MODE7_COL0; // graphic white
		mode7[1 + (y7 * MODE7_WIDTH)] = MODE7_COL1; // graphic white

		cimg_forX(src, x)
		{
			int x7 = (x / 2) + (MODE7_WIDTH - FRAME_WIDTH);

			// Just indicate pixels or not to speed up MODE 7 conversion
			mode7[(y7 * MODE7_WIDTH) + (x7)] = (get_colour_from_rgb(src(x, y, 0), src(x, y, 1), src(x, y, 2))
				|| get_colour_from_rgb(src(x + 1, y, 0), src(x + 1, y, 1), src(x + 1, y, 2))
				|| get_colour_from_rgb(src(x, y + 1, 0), src(x, y + 1, 1), src(x, y + 1, 2))
				|| get_colour_from_rgb(src(x + 1, y + 1, 0), src(x + 1, y + 1, 1), src(x + 1, y + 1, 2))
				|| get_colour_from_rgb(src(x, y + 2, 0), src(x, y + 2, 1), src(x, y + 2, 2))
				|| get_colour_from_rgb(src(x + 1, y + 2, 0), src(x + 1, y + 2, 1), src(x + 1, y + 2, 2))) ? 255 : 32;

			x++;
		}

		// Here we have:
		// pixel data turned into graphic characters in mode7 array - actually just an indication if pixels in cell
		// counts of each colour in a character cell - not useful?
		// proposed fg and bg colour per character cell - could be used as start of line control code?

		// Reset state as starting new character row
		// State = fg colour + bg colour + hold character + prev character
		// For each character cell on this line
		// Do we have pixels or not?
		// If we have pixels then need to decide whether is it better to replace this cell with a control code or keep pixels
		// If we don't have pixels then need to decide whether it is better to insert a control code or leave empty
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
			unsigned char first_char = get_graphic_char_from_image(FRAME_FIRST_COLUMN, y7, fg, 0);

			// What's the error for that character?
			int error = get_error_for_char(FRAME_FIRST_COLUMN, y7, first_char, fg, 0, false, MODE7_BLANK);

			// Find the lowest error corresponding to our possible start states
			if (error < min_error)
			{
				min_error = error;
				min_colour = fg;
			}
		}

		// This is our initial state of the line
		int state = GET_STATE(min_colour, 0, false, MODE7_BLANK);

		// Set this state before frame begins
		mode7[(y7 * MODE7_WIDTH) + (FRAME_FIRST_COLUMN - 1)] = MODE7_GFX_COLOUR + min_colour;

		// Kick off recursive error calculation with that state
		int error = get_error_for_remainder_of_line(FRAME_FIRST_COLUMN, y7, min_colour, 0, false, MODE7_BLANK);

		// Store first character
		char_for_xpos_in_state[state][FRAME_FIRST_COLUMN] = output[FRAME_FIRST_COLUMN];

		// Copy the resulting character data into MODE 7 screen
		for (int x7 = FRAME_FIRST_COLUMN; x7 < MODE7_WIDTH; x7++)
		{
			// Copy character chosen in this position for this state
			unsigned char best_char = char_for_xpos_in_state[state][x7];

			mode7[(y7 * MODE7_WIDTH) + (x7)] = best_char;

			// Update the state
			state = get_state_for_char(best_char, state);
		}

		// printf("\n");

		y += 2;
	}

	printf("frame size = %d\n", FRAME_SIZE);

	file = fopen(output_name, "w");

	if (file)
	{
		fwrite(mode7, 1, FRAME_SIZE, file);
		fclose(file);
	}

	/*
	sprintf(filename, "%s.inf",output_filename);
	file = fopen((const char*)filename, "wb");

	if (file)
	{
		sprintf(buffer, "$.BAD%d\tFF7C00 FF7C00\n", n);
		fwrite(buffer, 1, strlen(buffer), file);
		fclose(file);
	}
	*/
		
	return 0;
}

