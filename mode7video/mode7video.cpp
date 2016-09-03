// mode7video.cpp : Defines the entry point for the console application.
//

//#include "stdafx.h"
#include "CImg.h"

using namespace cimg_library;

#define MODE7_COL0		151
#define MODE7_COL1		(sep ? 154 : 32)
#define MODE7_BLANK		32
#define MODE7_WIDTH		40
#define MODE7_HEIGHT	25
#define MODE7_MAX_SIZE	(MODE7_WIDTH * MODE7_HEIGHT)

#define JPG_W			src._width		// w				// 76
#define JPG_H			src._height		// h				// 57 = 4:3  // 42 = 16:9
#define ASPECT_RATIO	(JPG_W/JPG_H)

#define FRAME_CHAR_W	MODE7_WIDTH						// fit to screen width

#define FRAME_TX_W		(FRAME_CHAR_W*2)				// 80 texels across screen
#define FRAME_TX_H		(FRAME_TX_W*JPG_H/JPG_W)		// calculate screen height in texels

#define FRAME_CHAR_H	((FRAME_TX_H/3)>MODE7_HEIGHT?MODE7_HEIGHT:(FRAME_TX_H/3))				// calculate screen height in chars
#define FRAME_SIZE		(MODE7_WIDTH * FRAME_CHAR_H)	// calculate frame size in chars

#define CHAR_PIXEL_W	(JPG_W/FRAME_CHAR_W)				// how many image pixels per char?
#define CHAR_PIXEL_H	(JPG_H/FRAME_CHAR_H)				// how many image pixels per char?

#define TEX_PIXEL_W		(CHAR_PIXEL_W/2)		
#define TEX_PIXEL_H		(CHAR_PIXEL_H/3)

#define SEP_PIXEL_W		(TEX_PIXEL_W/3)					// how many image pixels per separated texel? 011011
#define SEP_PIXEL_H		(TEX_PIXEL_H/3)					// how many image pixels per separated texel? 110110110

#define NUM_FRAMES		frames			

#define FILENAME		shortname		// "bad"	// "grav"
#define DIRECTORY		shortname		// "bad"	// "grav"

#define _ZERO_FRAME_PRESET FALSE		// whether our zero frame is already setup for MODE 7

#define CLAMP(a,low,high)		((a) < (low) ? (low) : ((a) > (high) ? (high) : (a)))
#define THRESHOLD(a,t)			((a) >= (t) ? 255 : 0)
#define LO(a)					((a) % 256)
#define HI(a)					((a) / 256)


#define LHS 0
#define RHS 40
#define COSTBITS (1U << 7)

static int rhs_costs_in_state[COSTBITS][41];
static unsigned char char_for_xpos_in_state[COSTBITS][41];
static unsigned char output[40];

static CImg<unsigned char> src;


unsigned char pixel_to_grey(int mode, unsigned char r, unsigned char g, unsigned char b)
{
	switch (mode)
	{
	case 0:
		return r;

	case 1:
		return g;

	case 2:
		return b;

	case 4:
		return (unsigned char)(0.2126f * r + 0.7152f * g + 0.0722f * b);

	default:
		return (unsigned char)((r + g + b) / 3);
	}
}


// Works on images 40*12 x 25*20 resolution = 480 x 500

int
state_for_char(unsigned char schar, int oldstate)
{
	int fgcol = oldstate & 7, bgcol = (oldstate >> 3) & 7;
	int sepgraph = (oldstate >> 6) & 1;

	switch (schar)
	{
	case 0x91: case 0x92: case 0x93:
	case 0x94: case 0x95: case 0x96: case 0x97:
		fgcol = schar - 0x90;
		break;

	case 0x99:
		sepgraph = 0;
		break;

	case 0x9a:
		sepgraph = 1;
		break;

	case 0x9c:
		bgcol = 0;
		break;

	case 0x9d:
		bgcol = fgcol;
		break;

	default:
		return oldstate;
	}

	return fgcol | (bgcol << 3) | (sepgraph << 6);
}


int
choose_char(int row, int col, unsigned int state, int ctrl,
	unsigned char *choice)
{
	int fgcol = state & 7, bgcol = (state >> 3) & 7, sepgfx = (state >> 6) & 1;
	int x, y;
	int fr = (fgcol & 1) ? 76 : 0;
	int fg = (fgcol & 2) ? 151 : 0;
	int fb = (fgcol & 4) ? 28 : 0;
	int br = (bgcol & 1) ? 76 : 0;
	int bg = (bgcol & 2) ? 151 : 0;
	int bb = (bgcol & 4) ? 28 : 0;
	int cost_on[6] = { 0, 0, 0, 0, 0, 0 };
	int cost_off[6] = { 0, 0, 0, 0, 0, 0 };
	const int blockbit[6] = { 1, 2, 4, 8, 16, 64 };
	int i;
	int cost_total = 0;
	unsigned char makechar = 0;

	for (y = 0; y < CHAR_PIXEL_H; y++)
	{
		int yblk = (y < TEX_PIXEL_H) ? 0 : (y < (TEX_PIXEL_H*2)) ? 1 : 2;
		int yrow = row * CHAR_PIXEL_H + y;

//		unsigned int *img = image->pixels;
//		img += yrow * image->pitch / 4;

		for (x = 0; x < CHAR_PIXEL_W; x++)
		{
			int xblk = (x < TEX_PIXEL_W) ? 0 : 1;
			int xrow = col * CHAR_PIXEL_W + x;
			int ir, ig, ib;
			int sfr = fr, sfg = fg, sfb = fb;
			int block = yblk * 2 + xblk;

//			unsigned int ipix = img[xrow];
//			ir = (((ipix >> 16) & 0xff) * 76) >> 8;
//			ig = (((ipix >> 8) & 0xff) * 151) >> 8;
//			ib = ((ipix & 0xff) * 28) >> 8;

			// red pixel = src(xrow,yrow,0)
			// green pixel = src(xrow,yrow,1)
			// blue pixel = src(xrow,yrow,2)

			ir = (src(xrow, yrow, 0) * 76) >> 8;
			ig = (src(xrow, yrow, 1) * 151) >> 8;
			ib = (src(xrow, yrow, 2) * 28) >> 8;

			if (sepgfx
				&& ((x < SEP_PIXEL_W || (x >= TEX_PIXEL_W && x < TEX_PIXEL_W+SEP_PIXEL_W))
					|| ((y >= TEX_PIXEL_H-SEP_PIXEL_H && y < TEX_PIXEL_H) || (y >= (2 * TEX_PIXEL_H - SEP_PIXEL_H) && y < (2*TEX_PIXEL_H)) || y >= CHAR_PIXEL_H-SEP_PIXEL_H)))
			{
				sfr = br;
				sfg = bg;
				sfb = bb;
			}

			if (!ctrl)
				cost_on[block] += (ir - sfr) * (ir - sfr)
				+ (ig - sfg) * (ig - sfg)
				+ (ib - sfb) * (ib - sfb);
			cost_off[block] += (ir - br) * (ir - br)
				+ (ig - bg) * (ig - bg)
				+ (ib - bb) * (ib - bb);
		}
	}

	makechar = 0x20;

	for (i = 0; i < 6; i++)
		if ((cost_on[i] < cost_off[i]) && !ctrl)
		{
			cost_total += cost_on[i];
			makechar |= blockbit[i];
		}
		else
			cost_total += cost_off[i];

	if (choice)
		*choice = makechar;

	return cost_total;
}


void
clear_rhs_costs_in_state(void)
{
	int b, r;

	for (b = 0; b < COSTBITS; b++)
		for (r = 0; r <= 40; r++)
		{
			rhs_costs_in_state[b][r] = -1;
			char_for_xpos_in_state[b][r] = 'X';
		}
}


int
select_char(unsigned char *videoram, int row, int xpos, int state)
{
	unsigned char chars[] =
	{
		0x0,  /* Magic value: calculate best (graphics) character to use.  */
		0x9c, 0x9d,                                 /* fill/don't fill.  */
		0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,   /* colours.  */
		0x99, 0x9a                                  /* sep/contiguous gfx.  */
	};

	int tryno;
	int lowest_cost = INT_MAX;
	int lowest_char = 0;
	int fill;

	if (xpos == RHS)
		return 0;

	if (rhs_costs_in_state[state][xpos] != -1)
		return rhs_costs_in_state[state][xpos];

	for (tryno = 0; tryno < sizeof(chars); tryno++)
	{
		int cost, this_cost, rest_costs, newstate;
		unsigned char try_char = chars[tryno];

		newstate = state_for_char(try_char, state);

		/* Can't choose a graphics char if we've not selected a graphical
		foreground colour.  */
		if (try_char == 0x0 && (newstate & 7) == 0)
			continue;

		if (try_char == 0x0)
			this_cost = choose_char(row, xpos, newstate, 0, &try_char);
		else
			this_cost = choose_char(row, xpos, newstate, 1, 0);

		rest_costs = select_char(videoram, row, xpos + 1, newstate);

		cost = this_cost + rest_costs;

		if (rhs_costs_in_state[newstate][xpos + 1] == -1)
		{
			rhs_costs_in_state[newstate][xpos + 1] = rest_costs;
			char_for_xpos_in_state[newstate][xpos + 1] = output[xpos + 1];
		}

		if (cost < lowest_cost)
		{
			lowest_cost = cost;
			lowest_char = try_char;
		}
	}

	output[xpos] = lowest_char;

	/* ??? Fix output.  */
	if (xpos == LHS)
		for (fill = LHS + 1; fill < RHS; fill++)
		{
			state = state_for_char(output[fill - 1], state);
			output[fill] = char_for_xpos_in_state[state][fill];
		}

	return lowest_cost;
}



int main(int argc, char **argv)
{
	cimg_usage("MODE 7 video convertor.\n\nUsage : mode7video [options]");
//	const char *const geom = cimg_option("-g", "76x57", "Input size (ignored for now)");
	const int frames = cimg_option("-n", 0, "Last frame number");
	const int start = cimg_option("-s", 1, "Start frame number");
	const char *const shortname = cimg_option("-i", (char*)0, "Input (directory / short name)");
	const char *const ext = cimg_option("-e", (char*)"png", "Image format file extension");
	const int gmode = cimg_option("-g", 0, "Colour to greyscale conversion (0=red only, 1=green only, 2=blue only, 3=simple average, 4=luminence preserving");
	const int thresh = cimg_option("-t", 127, "B&W threshold value");
	const int dither = cimg_option("-d", 0, "Dither mode (0=none/threshold only, 1=floyd steinberg, 2=ordered 2x2, 3=ordered 3x3");
	const bool save = cimg_option("-save", false, "Save individual MODE7 frames");
	const bool load = cimg_option("-load", false, "Load individual MODE7 frames");
	const bool sep = cimg_option("-sep", false, "Separated graphics");
	const bool verbose = cimg_option("-v", false, "Verbose output");
//	const int cbr_frames = cimg_option("-cbr", 0, "CBR frames [experimental/unfinished]");

//	int w = 76, h = 57;
//	std::sscanf(geom, "%d%*c%d", &w, &h);

	if (cimg_option("-h", false, 0)) std::exit(0);
	if (shortname == NULL)  std::exit(0);

	CImg<unsigned char> src;

	char filename[256];
	char input[256];
	char buffer[256];

	unsigned char prevmode7[MODE7_MAX_SIZE];
	unsigned char delta[MODE7_MAX_SIZE];
	unsigned char mode7[MODE7_MAX_SIZE];

	int totaldeltas = 0;
	int totalbytes = 0;
	int maxdeltas = 0;
	int resetframes = 0;

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

		if (n == start)
		{
			printf("Frame size (pixels) = %d x %d\n", JPG_W, JPG_H);
			printf("Frame size (chars) = %d x %d\n", FRAME_CHAR_W, FRAME_CHAR_H);
			printf("Frame size (texels) = %d x %d\n", FRAME_TX_W, FRAME_TX_H);
			printf("Char size (pixels) = %d x %d\n", CHAR_PIXEL_W, CHAR_PIXEL_H);
			printf("Texel size (pixels) = %d x %d\n", TEX_PIXEL_W, TEX_PIXEL_H);
			printf("Separated texel (gap) size (pixels) = %d x %d\n", SEP_PIXEL_W, SEP_PIXEL_H);
		}

#if 0
		// Convert to greyscale from RGB

		cimg_forXY(src, x, y)
		{
			src(x, y, 0) = pixel_to_grey(gmode, src(x, y, 0), src(x, y, 1), src(x, y, 2));
		}

		// Dithering

		if (dither == 0)					// None / threshold
		{
			cimg_forXY(src, x, y)
			{
				src(x, y, 0) = THRESHOLD(src(x, y, 0), thresh);
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

				if( y < src._height - 1 )
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

					x+=2;
				}

				y+=2;
			}
		}


		cimg_forY(src, y)
		{
			int y7 = y / 3;
			mode7[y7 * MODE7_WIDTH] = MODE7_COL0; // graphic white
			mode7[1 + (y7 * MODE7_WIDTH)] = MODE7_COL1; // graphic white

			cimg_forX(src, x)
			{
				int x7 = x / 2;

			//	printf("(%d, %d) = (0x%x, 0x%x, 0x%x)\n", x, y, src(x, y, 0), src(x, y, 1), src(x, y, 2));

				mode7[(y7 * MODE7_WIDTH) + (x7 + (MODE7_WIDTH-FRAME_CHAR_W))] = 32															// bit 5 always set!
						+ (src(x, y, 0)				?  1 : 0)			// (x,y) = bit 0
						+ (src(x + 1, y, 0)			?  2 : 0)			// (x+1,y) = bit 1
						+ (src(x, y + 1, 0)			?  4 : 0)			// (x,y+1) = bit 2
						+ (src(x + 1, y + 1, 0)		?  8 : 0)			// (x+1,y+1) = bit 3
						+ (src(x, y + 2, 0)			? 16 : 0)			// (x,y+2) = bit 4
						+ (src(x + 1, y + 2, 0)		? 64 : 0);			// (x+1,y+2) = bit 6

				x++;
			}
			// printf("\n");

			y += 2;
		}
#else
bool convert = true;

		if (load)
		{
			sprintf(filename, "%s\\bin\\%s-%d.bin", DIRECTORY, FILENAME, n);
			file = fopen((const char*)filename, "rb");

			if (file)
			{
				fread(mode7, 1, FRAME_SIZE, file);
				fclose(file);
				convert = false;
			}
		}

		if(convert)
		{
			for (int y = 0; y < FRAME_CHAR_H; y++)
			{
				int rcost, x;

				clear_rhs_costs_in_state();

				rcost = select_char(mode7, y, LHS, 0);
				/*printf ("row %d, cost=%d\n", y, rcost);*/

				for (x = 0; x < FRAME_CHAR_W; x++)
					mode7[y * MODE7_WIDTH + x] = output[x];
			}
		}
#endif

//		for (int i = 0; i < 1000; i++)
//		{
//			printf("0x%x ", mode7[i]);
//			if (i % 40 == 39)printf("\n");
//		}

		if (n == 1)
		{
			*ptr++ = LO(FRAME_SIZE);
			*ptr++ = HI(FRAME_SIZE);

			totalbytes += 2;
		}

		// How many deltas?
		int numdeltas = 0;
		int numdeltabytes = 0;
		int numliterals = 0;
		int numlitbytes = 0;

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

		if (numdeltas > FRAME_SIZE/3)
		{
			numdeltabytes = FRAME_SIZE;
			resetframes++;

			if (verbose)
			{
				printf("*** RESET *** (%x)\n", ptr - beeb);
			}

			*ptr++ = 0;
			*ptr++ = 0xff;

			memcpy(ptr, mode7, FRAME_SIZE);
			ptr += FRAME_SIZE;
		}
		else
		{
			numdeltabytes = numdeltas * 3;

			*ptr++ = LO(numdeltas);
			*ptr++ = HI(numdeltas);

			int previ = 0;

			for (int i = 0; i < FRAME_SIZE; i++)
			{
				if (delta[i] != 0)
				{
					unsigned char byte = mode7[i];			//  ^ prevmode7[i] for EOR with prev.
#if 0

					unsigned short pack = byte & 31;		// remove bits 5 & 6

					pack |= (byte & 64) >> 1;				// shift bit 6 down
					pack = (i - previ) + (pack << 10);						// shift whole thing up 10 bits and add offset

					*ptr++ = LO(pack);
					*ptr++ = HI(pack);
#else
					*ptr++ = LO(i - previ);
					*ptr++ = HI(i - previ);
					*ptr++ = byte;
#endif
					previ = i;								// or 0 for offset from screen start
				}
			}
		}

		{
			int blanks = 0;

			for (int i = 0; i < FRAME_SIZE; i++)
			{
				if (delta[i] == 0 && blanks<255)
				{
					blanks++;
				}
				else
				{
					int m = i;
					while (delta[m] != 0 && m < FRAME_SIZE) m++;
					int literals = m - i;
					
					blanks = 0;

					numliterals++;

					numlitbytes += 2 + literals;

					i = m;

					// Terminate early if last literal
					while (delta[m] == 0 && m < FRAME_SIZE) m++;
					if (m == FRAME_SIZE) i = m;
				}
			}
		}

		if (verbose)
		{
			printf("Frame: %d  numdeltas=%d (%d) numliterals=%d (%d)\n", n, numdeltas, numdeltabytes, numliterals, numlitbytes);
		}
		else
		{
			printf("\rFrame: %d/%d", n, NUM_FRAMES);
		}

		totalbytes += 2 + numdeltabytes;

		if (save)
		{
			sprintf(filename, "%s\\bin\\%s-%d.bin", DIRECTORY, FILENAME, n);
			file = fopen((const char*)filename, "wb");

			if (file)
			{
				fwrite(mode7, 1, FRAME_SIZE, file);
				fclose(file);
			}

			sprintf(filename, "%s\\delta\\%s-%d.delta.bin", DIRECTORY, FILENAME, n);
			file = fopen((const char*)filename, "wb");

			if (file)
			{
				fwrite(delta, 1, FRAME_SIZE, file);
				fclose(file);
			}
			
			sprintf(filename, "%s\\inf\\%s-%d.bin.inf", DIRECTORY, FILENAME, n);
			file = fopen((const char*)filename, "wb");

			if (file)
			{
				sprintf(buffer, "$.FR%d\tFF7C00 FF7C00\n", n);
				fwrite(buffer, 1, strlen(buffer), file);
				fclose(file);
			}
			
		}

		memcpy(prevmode7, mode7, MODE7_MAX_SIZE);

	}

	*ptr++ = 0xff;
	*ptr++ = 0xff;

	printf("\ntotal frames = %d\n", NUM_FRAMES);
	printf("frame size = %d\n", FRAME_SIZE);
	printf("total deltas = %d\n", totaldeltas);
	printf("total bytes = %d\n", totalbytes);
	printf("max deltas = %d\n", maxdeltas);
	printf("reset frames = %d\n", resetframes);
	printf("deltas / frame = %f\n", totaldeltas / (float)NUM_FRAMES);
	printf("bytes / frame = %f\n", totalbytes / (float)NUM_FRAMES);
	printf("bytes / second = %f\n", 25.0f * totalbytes / (float)NUM_FRAMES);
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

