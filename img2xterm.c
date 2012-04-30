/*
 *                THE STRONGEST PUBLIC LICENSE
 *                  Draft 1, November 2010
 *
 * Everyone is permitted to copy and distribute verbatim or modified
 * copies of this license document, and changing it is allowed as long
 * as the name is changed.
 *
 *                  THE STRONGEST PUBLIC LICENSE
 *   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
 *
 *  ⑨. This license document permits you to DO WHAT THE FUCK YOU WANT TO
 *      as long as you APPRECIATE CIRNO AS THE STRONGEST IN GENSOKYO.
 *
 * This program is distributed in the hope that it will be THE STRONGEST,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * USEFULNESS or FITNESS FOR A PARTICULAR PURPOSE.
 */

/* 
 * img2xterm -- convert images to 256 color block elements for use in cowfiles
 * written in the spirit of img2cow, with modified (bugfixed) code from
 * xterm256-conv2 by Wolfgang Frisch (xororand@frexx.de)
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <wand/MagickWand.h>

#ifndef NO_CURSES
#include <term.h>
#endif

#ifndef INFINITY
#include <float.h>
#define INFINITY DBL_MAX
#endif

enum {
	color_undef,
	color_transparent,
};

typedef struct _color_lab {
	double l;
	double a;
	double b;
} color_lab;

typedef struct _color_yiq {
	double y;
	double i;
	double q;
} color_yiq;

typedef struct _color_rgb8 {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} color_rgb8;

color_rgb8* rgbtable;
color_lab* labtable;
color_yiq* yiqtable;

const unsigned char valuerange[] = { 0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff };
unsigned long oldfg = color_undef, oldbg = color_undef;
int perceptive = 0, cowheader;
double chroma_weight = 1.0;

#ifndef NO_CURSES
int use_terminfo = 0;
char* ti_setb;
char* ti_setf;
char* ti_op;
#endif

color_lab srgb2lab(color_rgb8 rgb)
{
	double r = (double)rgb.r / 255.0;
	double g = (double)rgb.g / 255.0;
	double b = (double)rgb.b / 255.0;
	
	double rl = r <= 0.4045 ? r / 12.92 : pow((r + 0.055) / 1.055, 2.4);
	double gl = g <= 0.4045 ? g / 12.92 : pow((g + 0.055) / 1.055, 2.4);
	double bl = b <= 0.4045 ? b / 12.92 : pow((b + 0.055) / 1.055, 2.4);
	
	double x = (5067776.0/12288897.0) * rl + (4394405.0/12288897.0) * gl + ( 4435075.0/24577794.0) * bl;
	double y = ( 871024.0/4096299.0 ) * rl + (8788810.0/12288897.0) * gl + (  887015.0/12288897.0) * bl;
	double z = (  79184.0/4096299.0 ) * rl + (4394405.0/36866691.0) * gl + (70074185.0/73733382.0) * bl;
	
	double xn = x / (31271.0/32902.0);
	double yn = y;
	double zn = z / (35827.0/32902.0);
	
	double fxn = xn > (216.0 / 24389.0) ? pow(xn, 1.0 / 3.0)
	//	: (841.0 / 108.0) * xn + (4.0 / 29.0);
		: ((24389.0 / 27.0) * xn + 16.0) / 116.0;
	double fyn = yn > (216.0 / 24389.0) ? pow(yn, 1.0 / 3.0)
	//	: (841.0 / 108.0) * yn + (4.0 / 29.0);
		: ((24389.0 / 27.0) * yn + 16.0) / 116.0;
	double fzn = zn > (216.0 / 24389.0) ? pow(zn, 1.0 / 3.0)
	//	: (841.0 / 108.0) * zn + (4.0 / 29.0);
		: ((24389.0 / 27.0) * zn + 16.0) / 116.0;
	
	color_lab lab;
	
	lab.l = 116.0 * fyn - 16.0;
	lab.a = (500.0 * (fxn - fyn)) * chroma_weight;
	lab.b = (200.0 * (fyn - fzn)) * chroma_weight;
	
	return lab;
}

color_yiq srgb2yiq(color_rgb8 rgb)
{
	double r = (double)rgb.r / 255.0;
	double g = (double)rgb.g / 255.0;
	double b = (double)rgb.b / 255.0;
	
	color_yiq yiq;
	
	yiq.y =   0.299    * r +  0.587    * g +  0.144    * b;
	yiq.i =  (0.595716 * r + -0.274453 * g + -0.321263 * b) * chroma_weight;
	yiq.q =  (0.211456 * r + -0.522591 * g +  0.311135 * b) * chroma_weight;
	
	return yiq;
}

double cie2000(color_lab lab1, color_lab lab2)
{
	double c1 = sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
	double c2 = sqrt(lab2.a * lab2.a + lab2.b * lab2.b);

	double h1 = atan2(lab1.b, lab1.a);
	double h2 = atan2(lab2.b, lab2.a);

	double al = (lab1.l + lab2.l) * 0.5;
	double dl = lab2.l - lab1.l;

	double ac = (c1 + c2) * 0.5;
	double dc = c1 - c2;

	double ah = (h1 + h2) * 0.5;

	if (fabs(h1 - h2) > M_PI)
		ah += M_PI;

	double dh = h2 - h1;

	if (fabs(dh) > M_PI)
	{
		if (h2 <= h1)
			dh += M_PI * 2.0;
		else
			dh -= M_PI * 2.0;
	}

	dh = sqrt(c1 * c2) * sin(dh) * 2.0;

	double t = 1.0 - 0.17 * cos(ah - M_PI / 6.0) + 0.24 * cos(ah * 2.0) + 0.32 * cos(ah * 3.0 + (M_PI / 30)) - 0.20 * cos(ah * 4.0 - (7.0 / 20.0 * M_PI));
	double als = (al - 50.0) * (al - 50.0);
	double sl = 1.0 + 0.015 * als / sqrt(20.0 + als);

	double sc = 1.0 + 0.045 * ac;
	double sh = 1.0 + 0.015 * ac * t;

	double dt = ah / 25.0 - (11.0 / 180.0 * M_PI);
	dt = M_PI / 6.0 * exp(dt * -dt);

	double rt = pow(ac, 7);
	rt = sqrt(rt / (rt + 6103515625)) * sin(dt) * -2;

	dl /= sl;
	dc /= sc;
	dh /= sh;

	return sqrt(dl * dl + dc * dc + dh * dh + rt * dc * dh);
}

color_rgb8 xterm2rgb(unsigned char color)
{
	color_rgb8 rgb;
	
	if (color < 232)
	{
		color -= 16;
		rgb.r = valuerange[(color / 36) % 6];
		rgb.g = valuerange[(color / 6) % 6];
		rgb.b = valuerange[color % 6];
	}
	else
		rgb.r = rgb.g = rgb.b = 8 + (color - 232) * 10;
	
	return rgb;
}

unsigned char rgb2xterm_cie2000(color_rgb8 rgb)
{
	unsigned long i = 16;
	unsigned char ret = 0;
	double d, smallest_distance = INFINITY;
	color_lab lab = srgb2lab(rgb);
	
	for (; i < 256; i++)
	{
		d = cie2000(lab, labtable[i]);
		if (d < smallest_distance)
		{
			smallest_distance = d;
			ret = i;
		}
	}
	
	return ret;
}

unsigned char rgb2xterm_yiq(color_rgb8 rgb)
{
	unsigned long i = 16;
	unsigned char ret = 0;
	double d, smallest_distance = INFINITY;
	color_yiq yiq = srgb2yiq(rgb);
	
	for (; i < 256; i++)
	{
		d = (yiq.y - yiqtable[i].y) * (yiq.y - yiqtable[i].y) + 
			(yiq.i - yiqtable[i].i) * (yiq.i - yiqtable[i].i) + 
			(yiq.q - yiqtable[i].q) * (yiq.q - yiqtable[i].q);
		if (d < smallest_distance)
		{
			smallest_distance = d;
			ret = i;
		}
	}
	
	return ret;
}

unsigned char rgb2xterm(color_rgb8 rgb)
{
	unsigned long i = 16, d, smallest_distance = UINT_MAX;
	unsigned char ret = 0;
	
	for (; i < 256; i++)
	{
		d = (rgbtable[i].r - rgb.r) * (rgbtable[i].r - rgb.r) +
			(rgbtable[i].g - rgb.g) * (rgbtable[i].g - rgb.g) +
			(rgbtable[i].b - rgb.b) * (rgbtable[i].b - rgb.b);
		if (d < smallest_distance)
		{
			smallest_distance = d;
			ret = i;
		}
	}
	
	return ret;
}

void bifurcate(FILE* file, unsigned long color1, unsigned long color2, char* bstr)
{
	unsigned long fg = oldfg;
	unsigned long bg = oldbg;
	char* str = cowheader ? "\\N{U+2584}" : "\xe2\x96\x84";
	
	if (color1 == color2)
	{
		bg = color1;
		if (bstr && bg == color_transparent)
		{
			fg = color_undef;
			str = bstr;
		}
		else
			str = " ";
	}
	else
		if (color2 == color_transparent)
		{
			str = cowheader ? "\\N{U+2580}" : "\xe2\x96\x80";
			bg = color2;
			fg = color1;
		}
		else
		{
			bg = color1;
			fg = color2;
		}
	
#ifndef NO_CURSES
	if (use_terminfo)
	{
		if (bg != oldbg)
		{
			if (bg == color_transparent)
			{
				fputs(ti_op, file);
				oldfg = color_undef;
			}
			else
				fputs(tparm(ti_setb, bg), file);
		}
		
		if (fg != oldfg)
			fputs(tparm(ti_setf, fg), file);
	}
	else
#endif
	if (bg != oldbg)
	{
		if (bg == color_transparent)
			fputs(cowheader ? "\\e[49" : "\e[49", file);
		else
			fprintf(file, cowheader ? "\\e[48;5;%lu" : "\e[48;5;%lu", bg);
		
		if (fg != oldfg)
			if (fg == color_undef)
				fputs(";39m", file);
			else
				fprintf(file, ";38;5;%lum", fg);
		else
			fputc('m', file);
	}
	else if (fg != oldfg)
	{
		if (fg == color_undef)
			fputs(cowheader ? "\\e[39m" : "\e[39m", file);
		else
			fprintf(file, cowheader ? "\\e[38;5;%lum" : "\e[38;5;%lum", fg);
	}
	
	oldbg = bg;
	oldfg = fg;
	
	fputs(str, file);
}

unsigned long fillrow(PixelWand** pixels, unsigned long* row, unsigned long width)
{
	unsigned long i = 0, lastpx = 0;
	
	switch (perceptive)
	{
		case 0:
			for (; i < width; i ++)
			{
				if (PixelGetAlpha(pixels[i]) < 0.5)
					row[i] = color_transparent;
				else
				{
					color_rgb8 rgb;
					
					rgb.r = (unsigned long)(PixelGetRed(pixels[i]) * 255.0);
					rgb.g = (unsigned long)(PixelGetGreen(pixels[i]) * 255.0);
					rgb.b = (unsigned long)(PixelGetBlue(pixels[i]) * 255.0);
					
					row[i] = rgb2xterm(rgb);
					lastpx = i;
				}
			}
			break;
		case 1:
			for (; i < width; i ++)
			{
				if (PixelGetAlpha(pixels[i]) < 0.5)
					row[i] = color_transparent;
				else
				{
					color_rgb8 rgb;
					
					rgb.r = (unsigned long)(PixelGetRed(pixels[i]) * 255.0);
					rgb.g = (unsigned long)(PixelGetGreen(pixels[i]) * 255.0);
					rgb.b = (unsigned long)(PixelGetBlue(pixels[i]) * 255.0);
					
					row[i] = rgb2xterm_cie2000(rgb);
					lastpx = i;
				}
			}
			break;
		case 2:
			for (; i < width; i ++)
			{
				if (PixelGetAlpha(pixels[i]) < 0.5)
					row[i] = color_transparent;
				else
				{
					color_rgb8 rgb;
					
					rgb.r = (unsigned long)(PixelGetRed(pixels[i]) * 255.0);
					rgb.g = (unsigned long)(PixelGetGreen(pixels[i]) * 255.0);
					rgb.b = (unsigned long)(PixelGetBlue(pixels[i]) * 255.0);
					
					row[i] = rgb2xterm_yiq(rgb);
					lastpx = i;
				}
			}
			break;
	}
	
	return lastpx + 1;
}

void usage(int ret, const char* binname)
{
	fprintf(ret ? stderr: stdout,
"\
Usage: %s [ options ] [ infile ] [ outfile ]\n\n\
Convert bitmap images to 256 color block elements suitable for display in xterm\n\
and similar terminal emulators.\n\n\
Options:\n\
  -c, --cow                   generate a cowfile header\n\
                              (default behavior if invoked as img2cow)\n\
  -h, --help                  display this message\n"
#ifndef NO_CURSES
"\
  -i, --terminfo              use terminfo to set the colors of each block\n\
"
#endif
"\
  -l, --stem-length [length]  length of the speech bubble stem when generating\n\
                              cowfiles (default: 4)\n\
  -m, --margin [width]        add a margin to the left of the image\n\
  -p, --perceptive            use the CIE2000 color difference formula for\n\
                              color conversion instead of simple RGB linear\n\
                              distance\n\
  -s, --stem-margin [width]   margin for the speech bubble stem when generating\n\
                              cowfiles (default: 11)\n\
  -t, --stem-continue         continue drawing the speech bubble stem into the\n\
                              transparent areas of the image\n\
  -w, --chroma-weight [mult]  weighting for chroma in --perceptive and --yiq\n\
                              modes (default: 1)\n\
  -y, --yiq                   use linear distance in the YIQ color space\n\
                              instead of RGB for color conversion\n\
\n\
If the output file name is omitted the image is written to stdout. If both the\n\
input file name and the output file name are missing, img2xterm will act as a\n\
filter.\n\
\n\
Examples:\n\
  img2xterm -yw 2 image.png   display a bitmap image on the terminal using the\n\
                              YIQ color space for conversion with weighted\n\
                              chroma\n\
  img2xterm banner.png motd   save a bitmap image as a text file to display\n\
                              later\n\
  img2cow rms.png rms.cow     create a cowfile (assuming img2cow is a link to\n\
                              img2xterm)\n\
"
		, binname);
	exit(ret);
}

const char* basename2(const char* string)
{
	const char* ret = string;
	for (; *string; string++)
#if defined(WIN32) || defined(_WIN32)
		if (*string == '/' || *string == '\\')
#endif
		if (*string == '/')
			ret = string + 1;
	return ret;
}

int main(int argc, char** argv)
{
	const char stdin_str[] = "-", * infile = stdin_str, * outfile_str = NULL, * binname = *argv;
	char c;
	FILE* outfile = stdout;
	
	size_t width1, width2;
	unsigned long i, j, * row1, * row2, color1, color2, lastpx1, lastpx2, margin = 0;
	
	int background = 0;
	unsigned long stemlen = 4, stemmargin = 11;
	
	MagickWand* science;
	PixelIterator* iterator;
	PixelWand** pixels;
	
	cowheader = !memcmp(basename2(binname), "img2cow", 7);
	
	while (*++argv)
		if (**argv == '-')
		{
			while ((c = *++*argv))
				switch (c)
				{
					case '-':
						if (!strcmp("help", ++*argv))
							usage(0, binname);
						else if (!strcmp("cow", *argv))
							cowheader = 1;
						else if (!strcmp("stem-length", *argv))
						{
							if (!*++argv || !sscanf(*argv, "%lu", &stemlen))
								usage(1, binname);
						}
						else if (!strcmp("perceptive", *argv))
							perceptive = 1;
						else if (!strcmp("margin", *argv))
						{
							if (!*++argv || !sscanf(*argv, "%lu", &margin))
								usage(1, binname);
						}
						else if (!strcmp("stem-margin", *argv))
						{
							if (!*++argv || !sscanf(*argv, "%lu", &stemmargin))
								usage(1, binname);
						}
						else if (!strcmp("stem-continue", *argv))
							background = 1;
#ifndef NO_CURSES
						else if (!strcmp("terminfo", *argv))
							use_terminfo = 1;
#endif
						else if (!strcmp("chroma-weight", *argv))
						{
							if (!*++argv || !sscanf(*argv, "%lf", &chroma_weight))
								usage(1, binname);
						}
						else if (!strcmp("yiq", *argv))
							perceptive = 2;
						else
						{
							fprintf(stderr, "%s: unrecognised long option --%s\n", binname, *argv);
							usage(1, binname);
						}
						goto nextarg;
					case 'h':
						usage(0, binname);
						break;
					case 'c':
						cowheader = 1;
						break;
#ifndef NO_CURSES
					case 'i':
						use_terminfo = 1;
						break;
#endif
					case 'l':
						if (*++*argv || !*++argv || !sscanf(*argv, "%lu", &stemlen))
							usage(1, binname);
						goto nextarg;
					case 'm':
						if (*++*argv || !*++argv || !sscanf(*argv, "%lu", &margin))
							usage(1, binname);
						goto nextarg;
					case 'p':
						perceptive = 1;
						break;
					case 's':
						if (*++*argv || !*++argv || !sscanf(*argv, "%lu", &stemmargin))
							usage(1, binname);
						goto nextarg;
					case 't':
						background = 1;
						break;
					case 'w':
						if (*++*argv || !*++argv || !sscanf(*argv, "%lf", &chroma_weight))
							usage(1, binname);
						goto nextarg;
					case 'y':
						perceptive = 2;
						break;
					default:
						fprintf(stderr, "%s: unrecognised option -%c", binname, *--*argv);
						usage(1, binname);
				}
			nextarg:
			continue;
		}
		else if (infile == stdin_str)
			infile = *argv;
		else if (!outfile_str)
			outfile_str = *argv;
		else
			usage(1, binname);
	
	if (!cowheader && background == 1)
		background = 0;
	
#ifndef NO_CURSES
	if (use_terminfo)
	{
		if (setupterm(NULL, fileno(stdout), NULL))
			return 5;
		if (((ti_op = tigetstr("op")) == (void*)-1 &&
			(ti_op = tigetstr("sgr0")) == (void*)-1) ||
			(ti_setb = tigetstr("setb")) == (void*)-1 ||
			(ti_setf = tigetstr("setf")) == (void*)-1 ||
			!tparm(ti_setb, 255) ||
			!tparm(ti_setf, 255))
		{
			fprintf(stderr,
				"%s: terminal doesn't support required features\n", binname);
			return 5;
		}
	}
#endif
	
	MagickWandGenesis();
	atexit(MagickWandTerminus);
	science = NewMagickWand();
	
	if (!MagickReadImage(science, infile))
	{
		DestroyMagickWand(science);
		fprintf(stderr, "%s: couldn't open input file %s\n", binname, infile == stdin_str ? "<stdin>" : infile);
		return 3;
	}
	
	if (!(iterator = NewPixelIterator(science)))
	{
		DestroyMagickWand(science);
		fprintf(stderr, "%s: out of memory\n", binname);
		return 4;
	}
	
	if (outfile_str && !(outfile = fopen(outfile_str, "w")))
	{
		fprintf(stderr, "%s: couldn't open output file %s\n", binname, outfile_str);
		return 2;
	}
	
	switch (perceptive)
	{
		case 0:
			rgbtable = malloc(256 * sizeof(color_rgb8));
			for (i = 16; i < 256; i ++)
				rgbtable[i] = xterm2rgb(i);
			break;
		case 1:
			labtable = malloc(256 * sizeof(color_lab));
			for (i = 16; i < 256; i ++)
				labtable[i] = srgb2lab(xterm2rgb(i));
			break;
		case 2:
			yiqtable = malloc(256 * sizeof(color_yiq));
			for (i = 16; i < 256; i ++)
				yiqtable[i] = srgb2yiq(xterm2rgb(i));
			break;
	}
	
	if (cowheader)
	{
		fputs("binmode STDOUT, \":utf8\";\n$the_cow =<<EOC;\n", outfile);
		for (i = 0; i < stemlen; stemmargin ++, i ++)
		{
			for (j = 0; j < stemmargin; j ++)
				fputc(' ', outfile);
			fputs("$thoughts\n", outfile);
		}
	}
	
	pixels = PixelGetNextIteratorRow(iterator, &width1);
	while (pixels)
	{
		row1 = malloc(width1 * sizeof(unsigned long));
		lastpx1 = fillrow(pixels, row1, width1);
		
		if ((pixels = PixelGetNextIteratorRow(iterator, &width2)))
		{
			row2 = malloc(width2 * sizeof(unsigned long));
			lastpx2 = fillrow(pixels, row2, width2);
			if (lastpx2 > lastpx1)
				lastpx1 = lastpx2;
		}
		else
			row2 = NULL;
		
		for (i = 0; i < margin; i ++)
			if (background && i == stemmargin)
				bifurcate(outfile, color_transparent, color_transparent, "$thoughts");
			else
				fputc(' ', outfile);
		
		if (background == 1 && lastpx1 < stemmargin + 1)
			lastpx1 = stemmargin + 1;
		
		for (i = 0; i < lastpx1; i ++)
		{
			color1 = i < width1 ? row1[i] : color_transparent;
			color2 = i < width2 ? row2 ? row2[i] : color_transparent : color_transparent;
			if (background == 1)
			{
				if (i + margin == stemmargin)
				{
					if (color1 == color_transparent && color2 == color_transparent)
					{
						bifurcate(outfile, color1, color2, "$thoughts");
						continue;
					}
					else
						background = 0;
				}
				else if (i + margin == stemmargin + 1 && (color1 != color_transparent || color2 != color_transparent))
					background = 0;
			}
			bifurcate(outfile, color1, color2, NULL);
		}
		
		free(row1);
		if (row2)
			free(row2);
		
		if ((pixels = PixelGetNextIteratorRow(iterator, &width1)))
#ifndef NO_CURSES
			if (use_terminfo)
			{
				fprintf(outfile, "%s\n", ti_op);
				oldbg = color_transparent;
				oldfg = color_undef;
			}
			else
#endif
			if (oldbg != color_transparent)
			{
				fputs(cowheader ? "\\e[49m\n" : "\e[49m\n", outfile);
				oldbg = color_transparent;
			}
			else
				fputc('\n', outfile);
#ifndef NO_CURSES
		else if (use_terminfo)
			fprintf(outfile, "%s\n", ti_op);
#endif
		else if (oldbg == color_transparent)
			fputs(cowheader ? "\\e[39m\n" : "\e[39m\n", outfile);
		else
			fputs(cowheader ? "\\e[39;49m\n" : "\e[39;49m\n", outfile);
		
		stemmargin ++;
	}
	
	if (cowheader)
		fputs("\nEOC\n", outfile);
	
	DestroyPixelIterator(iterator);
	DestroyMagickWand(science);
	
	switch (perceptive)
	{
		case 0:
			free(rgbtable);
			break;
		case 1:
			free(labtable);
			break;
		case 2:
			free(yiqtable);
			break;
	}
	
	if (outfile != stdout)
		fclose(outfile);
	
	return 0;
}
