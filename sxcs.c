/*
 * This file is part of sxcs.
 *
 * sxcs is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * sxcs is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sxcs. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>

/*
 * macros
 */

#define ARRLEN(X)        (sizeof(X) / sizeof((X)[0]))
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#define UNUSED(X)        ((void)(X))

#define R(X)             (((unsigned long)(X) & 0xFF0000) >> 16)
#define G(X)             (((unsigned long)(X) & 0x00FF00) >>  8)
#define B(X)             (((unsigned long)(X) & 0x0000FF) >>  0)

#define TRANSFORM_SEQ_FROM_FUNC_ARRAY(X)  { X, ARRLEN(X) }

/*
 * types
 */

typedef unsigned int     uint;
typedef unsigned short   ushort;
typedef unsigned long    ulong;
typedef unsigned char    uchar;

enum output {
	OUTPUT_HEX = 1 << 0,
	OUTPUT_RGB = 1 << 1,
	OUTPUT_HSL = 1 << 2,
	OUTPUT_ALL = OUTPUT_HEX | OUTPUT_RGB | OUTPUT_HSL
};

/*
 * TODO: add grid around each pixel
 * TODO: add circle
 */
enum mag_type {
	MAG_NONE,
	MAG_CURSOR,
	MAG_END
};

typedef struct {
	uint h   : 9;
	uint s   : 7;
	uint l   : 7;
	uint pad : 9; /* cppcheck-suppress unusedStructMember */
} HSL;

typedef struct {
	uint oneshot           : 1;
	uint quit_on_keypress  : 1;
	enum mag_type mag;
	enum output fmt;
} Options;

typedef struct {
	XImage *im;
	uint x, y, w, h;
	int cx, cy;
	struct { uint w, h; } wanted;
} Image;

typedef void (*TransfromFunc)(XcursorImage *out, const XcursorImage *in);

typedef struct {
	const TransfromFunc *const f;
	uint len;
} TransfromSequence;

/*
 * Annotation for functions called atexit()
 * These functions are not allowed to call error(!0, ...) or exit().
 */
#define CLEANUP

/*
 * function prototype
 */

static void error(int exit_status, int errnum, const char *fmt, ...);
static HSL rgb_to_hsl(ulong col);
static void print_color(uint x, uint y, enum output fmt);
static void usage(void);
static Options opt_parse(int argc, const char *argv[]);
CLEANUP static void cleanup(void);
/* transformation functions */
static void square_zoomin(XcursorImage *out, const XcursorImage *in);
static void square_border(XcursorImage *out, const XcursorImage *in);

/*
 * static globals
 */

static struct {
	Display *dpy;
	Visual *vis;
	Cursor cur;
	Colormap cmap;
	int screen;
	int depth;
	uint w, h;
	uint grab_mask;
	struct {
		Window win;
		uint w, h;
	} root;
	struct {
		uint cur         : 1;
		uint ungrab_ptr  : 1;
		uint ungrab_kb   : 1;
	} valid;
} x11;

#include "config.h"

/*
 * function implementation
 */

static void
error(int exit_status, int errnum, const char *fmt, ...)
{
	va_list ap;

	fflush(stdout);
	fprintf(stderr, "%s: ", PROGNAME);
	va_start(ap, fmt);

	if (fmt)
		vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (errnum)
		fprintf(stderr, "%s%s", fmt ? ": " : "", strerror(errnum));
	fputc('\n', stderr);

	if (exit_status)
		exit(exit_status);
}

static HSL
rgb_to_hsl(ulong col)
{
	HSL ret = {0};
	const int r = R(col);
	const int g = G(col);
	const int b = B(col);
	const int max = MAX(MAX(r, g), b);
	const int min = MIN(MIN(r, g), b);
	const int l = ((max + min) * 50) / 255;
	int s = 0;
	long h = 0; /* should work even if long == 32bits */

	if (max != min) {
		const int d = max - min;
		s = (d * 100) / max;
		if (max == r) {
			h = ((g - b) * 1000) / d + (g < b ? 6000 : 0);
		} else if (max == g) {
			h = ((b - r) * 1000) / d + 2000;
		} else {
			h = ((r - g) * 1000) / d + 4000;
		}
		h *= 6;
		h /= 100;
		if (h < 0)
			h += 360;
	}

	ret.h = (uint)h;
	ret.l = l;
	ret.s = s;
	return ret;
}

static void
print_color(uint x, uint y, enum output fmt)
{
	XImage *im;
	ulong pix;

	im = XGetImage(x11.dpy, x11.root.win, x, y, 1, 1, AllPlanes, ZPixmap);
	if (im == NULL)
		error(1, 0, "failed to get image");
	pix = XGetPixel(im, 0, 0);

	printf("color:");
	if (fmt & OUTPUT_HEX)
		printf("\thex: #%.6lX", pix);
	if (fmt & OUTPUT_RGB)
		printf("\trgb: %lu %lu %lu", R(pix), G(pix), B(pix));
	if (fmt & OUTPUT_HSL) {
		HSL tmp = rgb_to_hsl(pix);
		printf("\thsl: %u %u %u", tmp.h, tmp.s, tmp.l);
	}
	printf("\n");
	fflush(stdout);

	XDestroyImage(im);
}

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [options]\n"
	        "  -h, --help:             show usage\n"
	        "  -o, --one-shot:         quit after picking\n"
	        "  -q, --quit-on-keypress: quit on keypress\n"
	        "      --mag-cursor:       magnifier follows the cursor (default)\n"
	        "      --mag-none:         disable magnifier\n"
	        "      --hex:              hex output\n"
	        "      --rgb:              rgb output\n"
	        "      --hsl:              hsl output\n",
	        PROGNAME);
	exit(1);
}

static Options
opt_parse(int argc, const char *argv[])
{
	int i;
	Options ret = {0};
	ret.mag = MAG_CURSOR;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--rgb") == 0)
			ret.fmt |= OUTPUT_RGB;
		else if (strcmp(argv[i], "--hex") == 0)
			ret.fmt |= OUTPUT_HEX;
		else if (strcmp(argv[i], "--hsl") == 0)
			ret.fmt |= OUTPUT_HSL;
		else if (strcmp(argv[i], "--one-shot") == 0 || strcmp(argv[i], "-o") == 0)
			ret.oneshot = 1;
		else if (strcmp(argv[i], "--quit-on-keypress") == 0 || strcmp(argv[i], "-q") == 0)
			ret.quit_on_keypress = 1;
		else if (strcmp(argv[i], "--mag-cursor") == 0)
			ret.mag = MAG_CURSOR;
		else if (strcmp(argv[i], "--mag-none") == 0)
			ret.mag = MAG_NONE;
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			usage();
		else
			error(1, 0, "unknown argument `%s`.", argv[i]);
	}

	if (ret.fmt == 0)
		ret.fmt = OUTPUT_ALL;

	return ret;
}

static void
square_zoomin(XcursorImage *out, const XcursorImage *in)
{
	uint x, y;

	for (y = 0; y < out->height; ++y) {
		for (x = 0; x < out->width; ++x) {
			float ox = (float)x / (float)out->width;
			float oy = (float)y / (float)out->height;
			uint ix = (float)in->width  * ox;
			uint iy = (float)in->height * oy;
			ulong tmp = in->pixels[iy * in->height + ix];
			out->pixels[y * out->height + x] = tmp | 0xff000000;
		}
	}
}

static void
square_border(XcursorImage *out, const XcursorImage *in)
{
	uint x, y;
	const uint b = MAG_BORDER_WIDTH;

	if (MAG_BORDER_WIDTH <= 0)
		return;

	UNUSED(in);
	for (y = 0; y < out->height; ++y) {
		for (x = 0; x < out->width; ++x) {
			if ((y < b || y + b >= out->height) ||
			    (x < b || x + b >= out->width))
			{
				out->pixels[y * out->height + x] = 0xffff0000;
			}
		}
	}
}

static XcursorImage *
img_to_xcurimg(const Image *img)
{
	uint x, y;
	XcursorImage *ret;
	float cy = (float)img->wanted.h / 2.0f;
	float cx = cy;

	ret = XcursorImageCreate(img->wanted.w, img->wanted.h);
	if (ret == NULL)
		return ret;

	for (y = 0; y < img->wanted.h; ++y) {
		for (x = 0; x < img->wanted.w; ++x) {
			float oy = ((float)y - cy) / cy;
			float ox = ((float)x - cx) / cx;
			int iy = img->cy + (int)(cy * oy);
			int ix = img->cx + (int)(cx * ox);
			ulong tmp;
			if ((iy < 0 || iy >= (int)img->h) || (ix < 0 || ix >= (int)img->w))
				tmp = 0xffffffff;
			else
				tmp = XGetPixel(img->im, ix, iy) | 0xff000000;
			ret->pixels[y * ret->width + x] = tmp;
		}
	}

	return ret;
}

static void
magnify(const int x, const int y)
{
	const int ms = MAG_WINDOW_SIZE / MAG_FACTOR;
	const int moff = ms / MAG_FACTOR;
	Image img;
	XcursorImage *in, *out[2];

	img.x = MAX(0, x - moff);
	img.y = MAX(0, y - moff);
	img.w = MIN(ms, x11.root.w - img.x);
	img.h = MIN(ms, x11.root.h - img.y);
	img.cx = x - img.x;
	img.cy = y - img.y;
	img.wanted.w = img.wanted.h = ms;
	img.im = XGetImage(x11.dpy, x11.root.win, img.x, img.y,
	                   img.w, img.h, AllPlanes, ZPixmap);
	if (img.im == NULL)
		error(1, 0, "failed to get image");
	in = img_to_xcurimg(&img);
	XDestroyImage(img.im);

	if (in == NULL)
		error(1, 0, "failed to create image");

	if (x11.valid.cur) {
		XFreeCursor(x11.dpy, x11.cur);
		x11.valid.cur = 0;
	}

	out[0] = XcursorImageCreate(MAG_WINDOW_SIZE, MAG_WINDOW_SIZE);
	out[1] = XcursorImageCreate(MAG_WINDOW_SIZE, MAG_WINDOW_SIZE);
	if (out[0] != NULL && out[1] != NULL) {
		uint i;
		void *finalout;

		out[0]->xhot = out[0]->yhot = MAG_WINDOW_SIZE / 2;
		out[1]->xhot = out[1]->yhot = MAG_WINDOW_SIZE / 2;

		for (i = 0; i < transform.len; ++i) {
			void *input  = i == 0 ? in :
			                finalout != out[0] ? out[0] : out[1];
			void *output = i == 0 ? out[0] :
			                finalout != out[0] ? out[1] : out[0];
			transform.f[i](output, input);
			finalout = output;
		}
		x11.cur = XcursorImageLoadCursor(x11.dpy, finalout);
		x11.valid.cur = 1;
		XChangeActivePointerGrab(x11.dpy, x11.grab_mask, x11.cur, CurrentTime);
	}

	if (out[0] != NULL)
		XcursorImageDestroy(out[0]);
	if (out[1] != NULL)
		XcursorImageDestroy(out[1]);
	XcursorImageDestroy(in);
}

CLEANUP static void
cleanup(void)
{
	if (x11.valid.ungrab_kb)
		XUngrabKeyboard(x11.dpy, CurrentTime);
	if (x11.valid.ungrab_ptr)
		XUngrabPointer(x11.dpy, CurrentTime);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
	if (x11.dpy != NULL)
		XCloseDisplay(x11.dpy);
}

/* TODO: maybe make the program viable as a standalone maginfier as well */
extern int
main(int argc, const char *argv[])
{
	Options opt;

	atexit(cleanup);

	opt = opt_parse(argc, argv);

	if ((x11.dpy = XOpenDisplay(NULL)) == NULL)
		error(1, 0, "failed to open x11 display");

	{ /* TODO: update the x11.root.{w,h} if root changes size */
		XWindowAttributes tmp;
		x11.root.win = DefaultRootWindow(x11.dpy);
		XGetWindowAttributes(x11.dpy, x11.root.win, &tmp);
		x11.root.h = tmp.height;
		x11.root.w = tmp.width;
		x11.screen = DefaultScreen(x11.dpy);
		x11.vis = DefaultVisual(x11.dpy, x11.screen);
		x11.depth = DefaultDepth(x11.dpy, x11.screen);
		x11.cmap = DefaultColormap(x11.dpy, x11.screen);
	}

	{
		XVisualInfo q = {0}, *r;
		int d, dummy;

		q.visualid = XVisualIDFromVisual(x11.vis);
		if ((r = XGetVisualInfo(x11.dpy, VisualIDMask, &q, &dummy)) == NULL)
			error(1, 0, "failed to obtain visual info");
		d = r->depth; /* cppcheck-suppress nullPointerRedundantCheck */
		XFree(r);
		if (d < 24)
			error(1, 0, "truecolor not supported");
	}

	if (!opt.mag) {
		x11.cur = XCreateFontCursor(x11.dpy, XC_tcross);
		x11.valid.cur = 1;
	}

	x11.grab_mask = ButtonPressMask | PointerMotionMask;
	x11.valid.ungrab_ptr = XGrabPointer(x11.dpy, x11.root.win, 0,
	                                    x11.grab_mask, GrabModeAsync, GrabModeAsync,
	                                    x11.root.win, x11.cur,
	                                    CurrentTime) == GrabSuccess;
	if (!x11.valid.ungrab_ptr)
		error(1, 0, "failed to grab cursor");

	if (opt.quit_on_keypress) {
		x11.valid.ungrab_kb = XGrabKeyboard(x11.dpy, x11.root.win, 0,
		                                    GrabModeAsync, GrabModeAsync,
		                                    CurrentTime) == GrabSuccess;
		if (!x11.valid.ungrab_kb)
			error(1, 0, "failed to grab keyboard");
	}

	while (1) {
		XEvent ev;
		Bool discard = False;

		switch (XNextEvent(x11.dpy, &ev), ev.type) {
		case ButtonPress:
			if (ev.xbutton.button == Button1)
				print_color(ev.xbutton.x_root, ev.xbutton.y_root, opt.fmt);
			if (ev.xbutton.button != Button1 || opt.oneshot)
				exit(0);
			break;
		case KeyPress:
			if (opt.quit_on_keypress)
				exit(1);
			break;
		case MotionNotify:
			do { /* don't act on stale events */
				if (XPending(x11.dpy) > 0) {
					XEvent next_ev;
					XPeekEvent(x11.dpy, &next_ev);
					discard = next_ev.type == MotionNotify;
					if (discard)
						XNextEvent(x11.dpy, &ev);
				} else {
					break;
				}
			} while (discard);
			if (opt.mag)
				magnify(ev.xbutton.x_root, ev.xbutton.y_root);
			break;
		default:
			/* error(0, 0, "recieved unknown event: `%d`", ev.type); */
			break;
		}
	}

	return 0; /* unreachable */
}
