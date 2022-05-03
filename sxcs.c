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
#include <poll.h>
#include <signal.h>

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
#define DIFF(A, B)       ((A) > (B) ? (A) - (B) : (B) - (A))
#define UNUSED(X)        ((void)(X))
/* not correct. but works fine for our usecase in this program */
#define ROUNDF(X)        ((int)((X) + 0.50f))

#define R(X)             (((unsigned long)(X) & 0xFF0000) >> 16)
#define G(X)             (((unsigned long)(X) & 0x00FF00) >>  8)
#define B(X)             (((unsigned long)(X) & 0x0000FF) >>  0)

#define FILTER_SEQ_FROM_ARRAY(X)  { X, ARRLEN(X) }

/*
 * types
 */

typedef unsigned int     uint;
typedef unsigned short   ushort;
typedef unsigned long    ulong;
typedef unsigned char    uchar;

enum output {
	OUTPUT_NONE = 0,
	OUTPUT_HEX = 1 << 0,
	OUTPUT_RGB = 1 << 1,
	OUTPUT_HSL = 1 << 2,
	OUTPUT_ALL = OUTPUT_HEX | OUTPUT_RGB | OUTPUT_HSL
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
	uint no_mag            : 1;
	enum output fmt;
} Options;

typedef struct {
	XImage *im;
	uint x, y, w, h;
	int cx, cy;
	struct { uint w, h; } wanted; /* w, h if no clipping occurred */
} Image;

typedef void (*FilterFunc)(XcursorImage *img);
typedef void (*MagFunc)(XcursorImage *out, const Image *in);

typedef struct {
	const FilterFunc *f;
	uint len;
} FilterSeq;

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
static ulong get_pixel(int x, int y);
static void print_color(int x, int y, enum output fmt);
static void usage(void);
static void filter_parse(const char *s);
static Options opt_parse(int argc, const char *argv[]);
static void magnify(const int x, const int y);
static void sighandler(int sig);
CLEANUP static void cleanup(void);
/* TODO: add bicubic scaling */
/* zoom functions */
static void nearest_neighbour(XcursorImage *out, const Image *in);
/* filter functions */
static void square_border(XcursorImage *img);
static void crosshair_square(XcursorImage *img);
static void grid(XcursorImage *img);
static void circle(XcursorImage *img);

/*
 * static globals
 */

static struct {
	Display *dpy;
	Cursor cur;
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

static XcursorImage *cursor_img;

static volatile sig_atomic_t sig_recieved;

/* TODO: comment config.h more thoroughly and document the filter/zoom func */
#include "config.h"

static const FilterSeq *filter = &filter_default;

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
	ret.l = (uint)l;
	ret.s = (uint)s;
	return ret;
}

static ulong
get_pixel(int x, int y)
{
	ulong ret;

	if (cursor_img != NULL) {
		uint m = cursor_img->height / 2;
		ret = cursor_img->pixels[m * cursor_img->height + m];
		ret &= 0x00ffffff; /* cut off the alpha */
	} else {
		XImage *im;
		im = XGetImage(x11.dpy, x11.root.win, x, y, 1, 1, AllPlanes, ZPixmap);
		if (im == NULL)
			error(1, 0, "failed to get image");
		ret = XGetPixel(im, 0, 0);
		XDestroyImage(im);
	}

	return ret;
}

static void
print_color(int x, int y, enum output fmt)
{
	ulong pix;

	if (fmt == OUTPUT_NONE)
		return;

	pix = get_pixel(x, y);
	if (fmt & OUTPUT_HEX)
		printf("hex:\t#%.6lX\t", pix);
	if (fmt & OUTPUT_RGB)
		printf("rgb:\t%lu %lu %lu\t", R(pix), G(pix), B(pix));
	if (fmt & OUTPUT_HSL) {
		HSL tmp = rgb_to_hsl(pix);
		printf("hsl:\t%u %u %u\t", tmp.h, tmp.s, tmp.l);
	}
	printf("\n");
	fflush(stdout);
}

static void
usage(void)
{
	uint i;
	const char *const s[] = {
		"usage: "PROGNAME" [options]",
		"  -h, --help:             show usage",
		"  -o, --one-shot:         quit after picking",
		"  -q, --quit-on-keypress: quit on keypress",
		"      --mag-none:         disable magnifier",
		"      --mag-filters:      comma separated filter list",
		"      --color-none:       disable color output",
		"      --hex:              hex output",
		"      --rgb:              rgb output",
		"      --hsl:              hsl output",
		"available filters:",
		"    square_border:     draws a square border",
		"    crosshair_square:  draws a square crosshair",
		"    grid:              draws grid",
		"    circle:            draws a circle",
	};
	for (i = 0; i < ARRLEN(s); ++i)
		fprintf(stderr, "%s\n", s[i]);
	exit(1);
}

static void
filter_parse(const char *s)
{
	static FilterFunc f_buf[16];
	static FilterSeq fs_buf = FILTER_SEQ_FROM_ARRAY(f_buf);
	uint f_len = 0;

	struct { const char *str; FilterFunc f; } table[] = {
		{ "square_border", square_border },
		{ "crosshair_square", crosshair_square },
		{ "grid", grid },
		{ "circle", circle }
	};
	char tok_buf[256], *tok = NULL;

	if (s == NULL)
		error(1, 0, "invalid filter (null)");

	strncpy(tok_buf, s, sizeof(tok_buf)); /* cppcheck-suppress nullPointerRedundantCheck */
	tok_buf[sizeof(tok_buf) - 1] = '\0';

	tok = strtok(tok_buf, ",");
	while (tok != NULL) {
		uint i, found_match = 0;

		for (i = 0; i < ARRLEN(table); ++i) {
			if (strcmp(tok, table[i].str) == 0) {
				if (f_len >= ARRLEN(f_buf)) {
					error(1, 0, "too many filters."
					      "max aloud: %zu", ARRLEN(f_buf));
				}
				f_buf[f_len++] = table[i].f;
				found_match = 1;
				break;
			}
		}

		if (!found_match)
			error(1, 0, "invalid filter %s", tok);
		tok = strtok(NULL, ",");
	}

	fs_buf.len = f_len;
	filter = &fs_buf;
}

static Options
opt_parse(int argc, const char *argv[])
{
	int i;
	Options ret = {0};
	int no_color = 0;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--rgb") == 0)
			ret.fmt |= OUTPUT_RGB;
		else if (strcmp(argv[i], "--hex") == 0)
			ret.fmt |= OUTPUT_HEX;
		else if (strcmp(argv[i], "--hsl") == 0)
			ret.fmt |= OUTPUT_HSL;
		else if (strcmp(argv[i], "--color-none") == 0)
			no_color = 1;
		else if (strcmp(argv[i], "--one-shot") == 0 || strcmp(argv[i], "-o") == 0)
			ret.oneshot = 1;
		else if (strcmp(argv[i], "--quit-on-keypress") == 0 || strcmp(argv[i], "-q") == 0)
			ret.quit_on_keypress = 1;
		else if (strcmp(argv[i], "--mag-none") == 0)
			ret.no_mag = 1;
		else if (strcmp(argv[i], "--mag-filters") == 0)
			filter_parse(argv[++i]);
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			usage();
		else
			error(1, 0, "unknown argument `%s`.", argv[i]);
	}

	if (ret.fmt == OUTPUT_NONE && !no_color)
		ret.fmt = OUTPUT_DEFAULT;

	return ret;
}

/* FIXME: this is kinda fucked if MAG_FACTOR < 2.0 */
static void
nearest_neighbour(XcursorImage *out, const Image *in)
{
	uint x, y;
	float ocy = (float)out->height / 2.0f;
	float ocx = (float)out->width / 2.0f;
	float icy = (float)in->wanted.h / MAG_FACTOR;
	float icx = (float)in->wanted.w / MAG_FACTOR;

	for (y = 0; y < out->height; ++y) {
		for (x = 0; x < out->width; ++x) {
			float oy = ((float)y - ocy) / ocy;
			float ox = ((float)x - ocx) / ocx;
			float iyf = (float)in->cy + (icy * oy);
			float ixf = (float)in->cx + (icx * ox);
			int iy = ROUNDF(iyf);
			int ix = ROUNDF(ixf);
			ulong tmp;

			if ((iy < 0 || iy >= (int)in->h) || (ix < 0 || ix >= (int)in->w))
				tmp = 0xff000000;
			else
				tmp = XGetPixel(in->im, ix, iy) | 0xff000000;
			out->pixels[y * out->height + x] = (XcursorPixel)tmp;
		}
	}
}

static void
square_border(XcursorImage *img)
{
	uint x, y;
	const uint b = SQUARE_BORDER_WIDTH;

	for (y = 0; y < img->height; ++y) {
		for (x = 0; x < img->width; ++x) {
			if ((y < b || y + b >= img->height) ||
			    (x < b || x + b >= img->width))
			{
				img->pixels[y * img->height + x] = SQUARE_BORDER_COLOR;
			}
		}
	}
}

static void
crosshair_square(XcursorImage *img)
{
	uint x, y;
	const uint c = (img->height / 2);
	const uint b = CROSSHAIR_SQUARE_SIZE;
	const uint bw = CROSSHAIR_SQUARE_BORDER_WIDTH;

	for (y = c - b; y <= c + b; ++y) {
		for (x = c - b; x <= c + b; ++x) {
			if (DIFF(x, c) > b - bw || DIFF(y, c) > b - bw)
				img->pixels[y * img->height + x] = CROSSHAIR_SQUARE_COLOR;
		}
	}
}

static void
grid(XcursorImage *img)
{
	uint x, y;
	const uint z = GRID_SIZE;
	const uint c = (img->height / 2) + (z / 2);

	for (y = 0; y < img->height; ++y) {
		for (x = 0; x < img->width; ++x) {
			if (DIFF(c, x) % z == 0 || DIFF(c, y) % z == 0) {
				img->pixels[y * img->height + x] = GRID_COLOR;
			}
		}
	}
}

static void
circle(XcursorImage *img)
{
	int x, y;
	int r = (int)CIRCLE_RADIUS;
	int br = r - (int)CIRCLE_WIDTH;
	int c = img->height / 2;

	for (y = 0; y < (int)img->height; ++y) {
		for (x = 0; x < (int)img->width; ++x) {
			int tx = x - c;
			int ty = y - c;

			if ((tx * tx) + (ty * ty) <= (r * r) &&
			    (tx * tx) + (ty * ty) > (br * br))
			{
				img->pixels[y * (int)img->height + x] = CIRCLE_COLOR;
			} else if (CIRCLE_TRANSPARENT_OUTSIDE &&
			           (tx * tx) + (ty * ty) > (r * r))
			{
				img->pixels[y * (int)img->height + x] = 0x0;
			}
		}
	}
}

static void
magnify(const int x, const int y)
{
	const uint ms = (uint)((float)MAG_WINDOW_SIZE / MAG_FACTOR);
	const int moff = (int)((float)ms / MAG_FACTOR);
	Image img;
	uint i;
	Cursor new_cur;

	img.x = (uint)MAX(0, x - moff);
	img.y = (uint)MAX(0, y - moff);
	img.w = MIN(ms, x11.root.w - img.x);
	img.h = MIN(ms, x11.root.h - img.y);
	img.cx = x - (int)img.x;
	img.cy = y - (int)img.y;
	img.wanted.w = img.wanted.h = ms;
	img.im = XGetImage(x11.dpy, x11.root.win, (int)img.x, (int)img.y,
	                   img.w, img.h, AllPlanes, ZPixmap);
	if (img.im == NULL)
		error(1, 0, "failed to get image");
	mag_func(cursor_img, &img);
	XDestroyImage(img.im);

	for (i = 0; i < filter->len; ++i)
		filter->f[i](cursor_img);
	new_cur = XcursorImageLoadCursor(x11.dpy, cursor_img);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
	x11.cur = new_cur;
	x11.valid.cur = 1;
	XChangeActivePointerGrab(x11.dpy, x11.grab_mask, x11.cur, CurrentTime);
}

static void
sighandler(int sig)
{
	sig_recieved = sig_recieved ? sig_recieved : sig;
}

CLEANUP static void
cleanup(void)
{
	if (x11.valid.ungrab_kb)
		XUngrabKeyboard(x11.dpy, CurrentTime);
	if (x11.valid.ungrab_ptr)
		XUngrabPointer(x11.dpy, CurrentTime);
	if (cursor_img != NULL)
		XcursorImageDestroy(cursor_img);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
	if (x11.dpy != NULL)
		XCloseDisplay(x11.dpy);
}

extern int
main(int argc, const char *argv[])
{
	Options opt;
	uint started = 0;

	atexit(cleanup);

	opt = opt_parse(argc, argv);

	if ((x11.dpy = XOpenDisplay(NULL)) == NULL)
		error(1, 0, "failed to open x11 display");

	{ /* TODO: update the x11.root.{w,h} if root changes size */
		XWindowAttributes tmp;
		x11.root.win = DefaultRootWindow(x11.dpy);
		XGetWindowAttributes(x11.dpy, x11.root.win, &tmp);
		x11.root.h = (uint)tmp.height;
		x11.root.w = (uint)tmp.width;
	}

	{
		XVisualInfo q = {0}, *r;
		int d, dummy, screen;
		Visual *vis;

		screen = DefaultScreen(x11.dpy);
		vis = DefaultVisual(x11.dpy, screen);
		q.visualid = XVisualIDFromVisual(vis);
		if ((r = XGetVisualInfo(x11.dpy, VisualIDMask, &q, &dummy)) == NULL)
			error(1, 0, "failed to obtain visual info");
		d = r->depth; /* cppcheck-suppress nullPointerRedundantCheck */
		XFree(r);
		if (d < 24)
			error(1, 0, "truecolor not supported");
	}

	if (opt.no_mag) {
		x11.cur = XCreateFontCursor(x11.dpy, XC_tcross);
		x11.valid.cur = 1;
	} else {
		cursor_img = XcursorImageCreate(MAG_WINDOW_SIZE, MAG_WINDOW_SIZE);
		if (cursor_img == NULL)
			error(1, 0, "failed to create image");
		cursor_img->xhot = cursor_img->yhot = MAG_WINDOW_SIZE / 2;
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

	{
		int i, sigs[] = { SIGINT, SIGTERM, SIGKILL /* one can try */ };
		for (i = 0; i < (int)ARRLEN(sigs); ++i)
			signal(sigs[i], sighandler);
	}

	while (1) {
		XEvent ev;
		Bool discard = False, pending;
		struct pollfd pfd;

		pfd.fd = ConnectionNumber(x11.dpy);
		pfd.events = POLLIN;
		pending = XPending(x11.dpy) > 0 || poll(&pfd, 1, MAX_FRAME_TIME) > 0;

		if (sig_recieved)
			exit(sig_recieved);

		/* TODO: rather than updating at certain interval,
		 * try to check if the window below changed or not
		 */
		if (!pending) {
			if (!opt.no_mag && started) /* TODO: figure out the random crashes */
				magnify(ev.xbutton.x_root, ev.xbutton.y_root);
			continue;
		}

		switch (XNextEvent(x11.dpy, &ev), ev.type) {
		case ButtonPress:
			switch (ev.xbutton.button) {
			case Button4:
				MAG_FACTOR *= MAG_STEP;
				break;
			case Button5:
				MAG_FACTOR = MAX(2.0f, MAG_FACTOR / MAG_STEP);
				break;
			case Button1:
				print_color(ev.xbutton.x_root, ev.xbutton.y_root, opt.fmt);
				if (!opt.oneshot)
					break;
				/* fallthrough */
			default:
				exit(0);
			}
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
			if (!opt.no_mag)
				magnify(ev.xbutton.x_root, ev.xbutton.y_root);
			started = 1;
			break;
		default:
			/* error(0, 0, "recieved unknown event: `%d`", ev.type); */
			break;
		}
	}

	return 0; /* unreachable */
}
