// mode7video.cpp : Defines the entry point for the console application.
//

//#include "stdafx.h"
#include "CImg.h"

using namespace cimg_library;

#define SCREEN_W			32
#define SCREEN_H			32

#define IMAGE_W				(src._width)
#define IMAGE_H				(src._height)

#define PIXEL_W				64
#define PIXEL_H				32

#define FRAME_WIDTH			(frame_width)
#define FRAME_HEIGHT		(frame_height)

#define NUM_FRAMES			frames			// 5367		// 5478
#define FRAME_SIZE			(SCREEN_W * FRAME_HEIGHT)

#define BYTES_PER_DELTA		3

#define SCREEN_MAX_SIZE		(SCREEN_W * SCREEN_H)

#define FILENAME			shortname
#define DIRECTORY			shortname

#define _ZERO_FRAME_PRESET	FALSE		// whether our zero frame is already setup for MODE 7

#define MAX_3(A,B,C)		((A)>(B)?((A)>(C)?(A):(C)):(B)>(C)?(B):(C))
#define MIN_3(A,B,C)		((A)<(B)?((A)<(C)?(A):(C)):(B)<(C)?(B):(C))

#define CLAMP(a,low,high)	((a) < (low) ? (low) : ((a) > (high) ? (high) : (a)))
#define THRESHOLD(a,t)		((a) >= (t) ? 255 : 0)
#define LO(a)				((a) % 256)
#define HI(a)				((a) / 256)

#define SAFE_SRC(x,y,c)		(((x)<0||(x)>=src._width)?0:(((y)<0||(y)>=src._height)?0:(src((x),(y),(c)))))
//#define SAFE_SRC(x,y,c)		(((x)<0||(x)>=src._width)?0:(((y)<0||(y)>=src._height)?0:(src((x),(y),(0)))))

static CImg<unsigned char> src;
static unsigned char mode7[SCREEN_MAX_SIZE];
static unsigned char prevmode7[SCREEN_MAX_SIZE];
static unsigned char delta[SCREEN_MAX_SIZE];

static bool global_use_geometric = true;

static int frame_width;
static int frame_height;

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

static unsigned char left_pixel[16] = {
	0x00,0x02,0x08,0x0A,0x20,0x22,0x28,0x2A,0x80,0x82,0x88,0x8A,0xA0,0xA2,0xA8,0xAA
};

static unsigned char right_pixel[16] = {
	0x00,0x01,0x04,0x05,0x10,0x11,0x14,0x15,0x40,0x41,0x44,0x45,0x50,0x51,0x54,0x55
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
	const int grey = cimg_option("-grey", 5, "Grey scale conversion mode = R/G/B/mean/luma");

	if (cimg_option("-h", false, 0)) std::exit(0);
	if (shortname == NULL)  std::exit(0);

	global_use_geometric = !error_lookup;

	char filename[256];
	char input[256];

	int totaldeltas = 0;
	int totalbytes = 0;
	int maxdeltas = 0;
	int resetframes = 0;
	int skipdeltas = 0;

	unsigned char *beeb = (unsigned char *) malloc(SCREEN_MAX_SIZE * NUM_FRAMES);
	unsigned char *ptr = beeb;

	int *delta_counts = (int *)malloc(sizeof(int) * (NUM_FRAMES+1));

	memset(mode7, 0, SCREEN_MAX_SIZE);
	memset(prevmode7, 0, SCREEN_MAX_SIZE);
	memset(delta, 0, SCREEN_MAX_SIZE);
	memset(delta_counts, 0, sizeof(int) * (NUM_FRAMES+1));

	FILE *file;

	// Blank MODE 7 gfx screen

	for (int i = 0; i < SCREEN_MAX_SIZE; i++)
	{
		prevmode7[i] = 0;
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
			pixel_width = SCREEN_W * 2;
			pixel_height = pixel_width * IMAGE_H / IMAGE_W;

			// Adjust to height
			if (pixel_height > PIXEL_H)
			{
				pixel_height = PIXEL_H;
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
		frame_height = pixel_height;

		if (verbose)
		{
			printf("frame=%dx%d ", frame_width, frame_height);
		}

		// Set everything to blank
		memset(mode7, 0, SCREEN_MAX_SIZE);

		for (int y = 0; y < frame_height; y++)
		{
			// Copy the resulting character data into MODE 7 screen

			for (int x = 0; x < FRAME_WIDTH; x++)
			{
				// Copy character chosen in this position for this state
#if 0
				int left_colour = match_closest_palette_colour(SAFE_SRC(2*x, y, 0), SAFE_SRC(2*x, y, 1), SAFE_SRC(2*x, y, 2));
				int right_colour = match_closest_palette_colour(SAFE_SRC(2*x+1, y, 0), SAFE_SRC(2*x+1, y, 1), SAFE_SRC(2*x+1, y, 2));
#else
				int left_colour = pixel_to_grey(grey, SAFE_SRC(2 * x, y, 0), SAFE_SRC(2 * x, y, 1), SAFE_SRC(2 * x, y, 2)) >> 4;
				int right_colour = pixel_to_grey(grey, SAFE_SRC(2 * x + 1, y, 0), SAFE_SRC(2 * x + 1, y, 1), SAFE_SRC(2 * x + 1, y, 2)) >> 4;
#endif

				mode7[(y * SCREEN_W) + x] = left_pixel[left_colour] | right_pixel[right_colour];
			}
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

		if (verbose)
		{
			printf("ptr=0x%x ", ptr - beeb);
		}

		if (numdeltas > FRAME_SIZE/ BYTES_PER_DELTA)
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
				if (mode7[i] != prevmode7[i])
				{
					unsigned char byte = mode7[i];			//  ^ prevmode7[i] for EOR with prev.

					// No pack
					*ptr++ = LO((i - previ));
					*ptr++ = HI((i - previ));
					*ptr++ = byte;

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

		memcpy(prevmode7, mode7, SCREEN_MAX_SIZE);

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
