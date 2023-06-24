/*
 * Copyright (C) 2022-2023 NRK and contributors.
 *
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

#define _POSIX_C_SOURCE 200112L /* NOLINT */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>

#ifndef PROGNAME
	#define PROGNAME "sxcs"
#endif
#ifndef VERSION
	#define VERSION "v0.7.2-dirty"
#endif

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

#define R(X)             ( ((ulong)(X) & 0xFF0000) >> 16 )
#define G(X)             ( ((ulong)(X) & 0x00FF00) >>  8 )
#define B(X)             ( ((ulong)(X) & 0x0000FF) >>  0 )

#define FILTER_SEQ_FROM_ARRAY(X)  { X, ARRLEN(X) }

#define S(X)       str_c89_workaround((char *)(X), sizeof(X) - 1)

/* portable compiler attributes */
#ifndef __has_attribute
	#define __has_attribute(X) (0)
#endif
#if __has_attribute(format)
	#define ATTR_FMT(A, S, ARG) __attribute__ ((format (A, S, ARG)))
#else
	#define ATTR_FMT(A, S, ARG)
#endif
#if __has_attribute(noreturn)
	#define ATTR_NORETURN __attribute__ ((noreturn))
#else
	#define ATTR_NORETURN
#endif

#ifndef __has_builtin
	#define __has_builtin(X) (0)
#endif
#ifdef DEBUG
	#define ASSERT(X)              ((X) ? (void)0 : abort())
#elif __has_builtin(__builtin_unreachable)
	#define ASSERT(X)              ((X) ? (void)0 : __builtin_unreachable())
#else
	#define ASSERT(X)              ((void)0)
#endif

/*
 * types
 */

typedef unsigned int     uint;
typedef unsigned short   ushort;
typedef unsigned long    ulong;
typedef unsigned char    uchar;

typedef struct { uchar *s; ptrdiff_t len; } Str;

enum output {
	OUTPUT_NONE = 0,
	OUTPUT_HEX = 1 << 0,
	OUTPUT_RGB = 1 << 1,
	OUTPUT_HSL = 1 << 2,
	OUTPUT_ALL = OUTPUT_HEX | OUTPUT_RGB | OUTPUT_HSL
};

typedef struct {
	ushort h; /* only 9bits needed */
	uchar  s; /* only 7bits needed */
	uchar  l; /* only 7bits needed */
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
 * function prototype
 */

/*
 * zoom functions:
 *
 * The zoom functions are given a `XcursorImage` pointer where it must output
 * the zoomed image. As input, an `Image` pointer is given.
 * NOTE: In case of clipping (cursor at the edge of the screen) the input may
 * not be of expected size. `in->{cx,cy}` are the coordinates of the cursor
 * position and `in->wanted.{w,h}` are w/h if no clipping had occurred.
 * so the zoom function must ensure that the middle of the output maps to the
 * cx,cy of the input and it must fill any of the clipped area with transparent
 * pixel (0xff000000).
 */
/* TODO: add bicubic scaling */
static void nearest_neighbour(XcursorImage *out, const Image *in);
/*
 * filter functions:
 *
 * The filter functions are given a pointer to `XcursorImage` as input. There
 * is no output, the functions can modify it's input as it wants.
 */
/* TODO: add pixels_grid */
static void square(XcursorImage *img);
static void xhair(XcursorImage *img);
static void grid(XcursorImage *img);
static void circle(XcursorImage *img);

/*
 * static globals
 */

static struct {
	Display *dpy;
	Cursor cur;
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

#include "config.h"

static const FilterSeq *filter = &filter_default;

/*
 * function implementation
 */

ATTR_NORETURN ATTR_FMT(printf, 1, 2)
static void
fatal(const char *fmt, ...)
{
	va_list ap;

	fflush(stdout);
	fprintf(stderr, "%s: ", PROGNAME);

	va_start(ap, fmt);
	if (fmt != NULL)
		vfprintf(stderr, fmt, ap);
	va_end(ap);
	fwrite("\n", 1, 1, stderr);
	exit(1);
}

/* c89 no compound literal support */
static Str
str_c89_workaround(char *s, ptrdiff_t len)
{
	Str ret;
	ASSERT(s != NULL && len >= 0);
	ret.s = (uchar *)s;
	ret.len = len;
	return ret;
}

static Str
str_from_cstr(char *s)
{
	Str ret = {0};
	for (ret.s = (uchar *)s; ret.s != NULL && ret.s[ret.len] != '\0'; ++ret.len) {}
	return ret;
}

static int
str_eq(Str a, Str b)
{
	ptrdiff_t n = -1;
	ASSERT(a.len >= 0 && b.len >= 0);
	if (a.len == b.len) {
		for (n = 0; n < a.len && a.s[n] == b.s[n]; ++n) {}
	}
	return n == a.len;
}

static int
str_tok(Str *s, Str *t, uchar ch)
{
	ptrdiff_t l = s->len;
	ASSERT(l >= 0);
	t->s = s->s;
	for (t->len = 0; t->len < l && t->s[t->len] != ch; ++t->len) {}
	s->s   += t->len + (t->len < l);
	s->len -= t->len + (t->len < l);
	return l != 0;
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
	const int ltmp = (int)(((max + min) * 500L) / 255L);
	const int l = (ltmp / 10) + (ltmp % 10 >= 5);
	/* should work even if long == 32bits */
	long s = 0, h = 0;

	if (max != min) {
		const long d = max - min;
		const long M = (max * 1000L) / 255, m = (min * 1000L) / 255;
		if (l <= 50)
			s = ((M - m) * 1000L) / (M + m);
		else
			s = ((M - m) * 1000L) / (2000L - M - m);
		s = (s / 10) + (s % 10 >= 5);
		if (max == r) {
			h = ((g - b) * 1000L) / d + (g < b ? 6000L : 0L);
		} else if (max == g) {
			h = ((b - r) * 1000L) / d + 2000L;
		} else {
			h = ((r - g) * 1000L) / d + 4000L;
		}
		h *= 6;
		h = (h / 100) + (h % 100 >= 50);
		if (h < 0)
			h += 360;
	}

	ASSERT(h >= 0 && h <= 360);
	ret.h = (ushort)h;
	ASSERT(l >= 0 && l <= 100);
	ret.l = (uchar)l;
	ASSERT(s >= 0 && s <= 100);
	ret.s = (uchar)s;
	return ret;
}

/*
 * NOTE: calling XGetPixel is expensive. so manually extract the pixels
 * instead. it *should* work fine, but only tested it on my system. so it's
 * possible that this causes some problems, especially if the X server is
 * running on some funny config.
 */
static ulong
ximg_pixel_get(const XImage *img, int x, int y)
{
	const size_t off = ((size_t)y * (size_t)img->bytes_per_line) + ((size_t)x * 4);
	const uchar *const p = (uchar *)img->data + off;
	ASSERT(x >= 0); ASSERT(y >= 0);

	if (img->byte_order == MSBFirst) {
		return (ulong)p[0] << 24 |
		       (ulong)p[1] << 16 |
		       (ulong)p[2] <<  8 |
		       (ulong)p[3] <<  0;
	} else {
		return (ulong)p[3] << 24 |
		       (ulong)p[2] << 16 |
		       (ulong)p[1] <<  8 |
		       (ulong)p[0] <<  0;
	}
}

static ulong
get_pixel(int x, int y)
{
	ulong ret;

	if (cursor_img != NULL) {
		uint m = cursor_img->height / 2;
		ret = cursor_img->pixels[m * cursor_img->width + m];
		ret &= 0x00ffffff; /* cut off the alpha */
	} else {
		XImage *im = XGetImage(x11.dpy, x11.root.win, x, y, 1, 1, AllPlanes, ZPixmap);
		if (im == NULL)
			fatal("failed to get image");
		ret = ximg_pixel_get(im, 0, 0);
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
		fprintf(stdout, "hex:\t#%.6lX\t", pix);
	if (fmt & OUTPUT_RGB)
		fprintf(stdout, "rgb:\t%lu %lu %lu\t", R(pix), G(pix), B(pix));
	if (fmt & OUTPUT_HSL) {
		HSL tmp = rgb_to_hsl(pix);
		fprintf(stdout, "hsl:\t%u %u %u\t", tmp.h, tmp.s, tmp.l);
	}
	fwrite("\n", 1, 1, stdout);
	fflush(stdout);
	if (ferror(stdout))
		fatal("writing to stdout failed");
}

ATTR_NORETURN
static void
usage(void)
{
	char s[] =
		"usage: "PROGNAME" [options]\n"
		"See the manpage for more details.\n"
	;
	fwrite(s, 1, sizeof s - 1, stderr);
	exit(1);
}

ATTR_NORETURN
static void
version(void)
{
	char s[] =
		PROGNAME" "VERSION"\n\n"
		"Copyright (C) 2022-2023 NRK and contributors.\n"
		"License: GPLv3+ <https://gnu.org/licenses/gpl.html>.\n"
		"Upstream: <https://codeberg.org/NRK/sxcs>\n"
	;
	fwrite(s, 1, sizeof s - 1, stderr);
	exit(1);
}

static void
filter_parse(Str arg)
{
	static FilterFunc f_buf[16];
	static FilterSeq fs_buf = FILTER_SEQ_FROM_ARRAY(f_buf);

	Str tok;
	uint i, f_len = 0;

	if (arg.len == 0)
		fatal("--mag-filters: no argument provided");

	while (str_tok(&arg, &tok, ',')) {
		int found_match = 0;
		for (i = 0; i < ARRLEN(FILTER_TABLE); ++i) {
			if (str_eq(tok, FILTER_TABLE[i].str)) {
				if (f_len >= ARRLEN(f_buf))
					fatal("--mag-filters: too many filters");
				f_buf[f_len++] = FILTER_TABLE[i].f;
				found_match = 1;
				break;
			}
		}

		if (!found_match)
			fatal("invalid filter `%.*s`", (int)tok.len, tok.s);
	}

	ASSERT(arg.len == 0);
	fs_buf.len = f_len;
	filter = &fs_buf;
}

static Options
opt_parse(int argc, char *argv[])
{
	int i;
	Options ret = {0};
	int no_color = 0;

	for (i = 1; i < argc; ++i) {
		Str a = str_from_cstr(argv[i]);
		if (str_eq(a, S("--rgb")))
			ret.fmt |= OUTPUT_RGB;
		else if (str_eq(a, S("--hex")))
			ret.fmt |= OUTPUT_HEX;
		else if (str_eq(a, S("--hsl")))
			ret.fmt |= OUTPUT_HSL;
		else if (str_eq(a, S("--color-none")))
			no_color = 1;
		else if (str_eq(a, S("--one-shot")) || str_eq(a, S("-o")))
			ret.oneshot = 1;
		else if (str_eq(a, S("--quit-on-keypress")) || str_eq(a, S("-q")))
			ret.quit_on_keypress = 1;
		else if (str_eq(a, S("--mag-none")))
			ret.no_mag = 1;
		else if (str_eq(a, S("--mag-filters")))
			filter_parse(str_from_cstr(argv[++i]));
		else if (str_eq(a, S("--help")) || str_eq(a, S("-h")))
			usage();
		else if (str_eq(a, S("--version")))
			version();
		else
			fatal("unknown argument `%s`.", argv[i]);
	}

	if (ret.fmt == OUTPUT_NONE && !no_color)
		ret.fmt = OUTPUT_DEFAULT;

	return ret;
}

static void
nearest_neighbour(XcursorImage *out, const Image *in)
{
	uint x, y;
	float ocy = (float)out->height / 2.0f;
	float ocx = (float)out->width / 2.0f;
	float icy = (float)in->wanted.h / 2.0f;
	float icx = (float)in->wanted.w / 2.0f;

	for (y = 0; y < out->height; ++y) {
		for (x = 0; x < out->width; ++x) {
			float oy = ((float)y - ocy) / ocy;
			float ox = ((float)x - ocx) / ocx;
			int iy = ROUNDF((float)in->cy + (icy * oy));
			int ix = ROUNDF((float)in->cx + (icx * ox));
			ulong tmp;

			if ((iy < 0 || iy >= (int)in->h) || (ix < 0 || ix >= (int)in->w))
				tmp = 0xff000000;
			else
				tmp = ximg_pixel_get(in->im, ix, iy) | 0xff000000;
			out->pixels[y * out->width + x] = (XcursorPixel)tmp;
		}
	}
}

static void
square(XcursorImage *img)
{
	size_t i, k;
	const uint b = SQUARE_WIDTH;

	i = 0;
	while (i < img->width * b + b) /* draw the top border + 1 left side */
		img->pixels[i++] = SQUARE_COLOR;
	do {
		i += img->width - b * 2; /* skip the mid */
		for (k = 0; k < b*2; ++k)
			img->pixels[i++] = SQUARE_COLOR;
	} while (i < (img->height - b) * img->width);
	while (i < img->width * img->height) /* draw the rest */
		img->pixels[i++] = SQUARE_COLOR;
}

static void
xhair(XcursorImage *img)
{
	uint x, y;
	const uint c = img->height / 2;
	const uint b = XHAIR_SIZE;
	const uint bw = XHAIR_BORDER_WIDTH ;

	for (y = c - b; y <= c + b; ++y) {
		for (x = c - b; x <= c + b; ++x) {
			if (DIFF(x, c) > b - bw || DIFF(y, c) > b - bw)
				img->pixels[y * img->width + x] = XHAIR_COLOR;
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
		if (DIFF(c, y) % z == 0) {
			for (x = 0; x < img->width; ++x)
				img->pixels[y * img->width + x] = GRID_COLOR;
		} else for (x = c % z; x < img->width; x += z) {
			img->pixels[y * img->width + x] = GRID_COLOR;
		}
	}
}

static void
four_point_draw(XcursorImage *img, uint x, uint y, XcursorPixel col) /* naming is hard */
{
	uint w = img->width, h = img->height;
	ASSERT(x <= w/2); ASSERT(y <= h/2);
	img->pixels[y * w + x] = col;
	img->pixels[y * w + (w - x - 1)] = col;
	img->pixels[(h - y - 1) * w + x] = col;
	img->pixels[(h - y - 1) * w + (w - x - 1)] = col;
}

/* TODO: reduce jaggedness */
static void
circle(XcursorImage *img)
{
	uint x, y, h = img->height, w = img->width;
	uint r = CIRCLE_RADIUS;
	uint br = r - CIRCLE_WIDTH;
	uint c = h / 2;

	for (y = 0; y < h / 2 + (h & 1); ++y) {
		for (x = 0; x < w / 2 + (w & 1); ++x) {
			uint tx = c - x;
			uint ty = c - y;
			uint x2y2 = (tx * tx) + (ty * ty);

			if (x2y2 > (r * r)) { /* outside the circle border */
				if (CIRCLE_TRANSPARENT_OUTSIDE)
					four_point_draw(img, x, y, 0x0);
			} else if (x2y2 > (br * br)) { /* inside the circle border */
				four_point_draw(img, x, y, CIRCLE_COLOR);
			} else { /* inside the circle, nothing to do. move on to the next y */
				break;
			}
		}
	}
}

static void
magnify(const int x, const int y)
{
	const uint c = (uint)((float)MAG_SIZE / MAG_FACTOR);
	const int off = c / 2;
	uint i;
	Image img;
	Cursor new_cur;

	img.x = (uint)MAX(0, x - off);
	img.y = (uint)MAX(0, y - off);
	img.w = MIN(c, x11.root.w - img.x);
	img.h = MIN(c, x11.root.h - img.y);
	img.cx = x - (int)img.x;
	img.cy = y - (int)img.y;
	img.wanted.w = img.wanted.h = c;
	/* TODO: look into Shm extension to reduce allocation overhead. */
	img.im = XGetImage(
		x11.dpy, x11.root.win, (int)img.x, (int)img.y, img.w, img.h,
		AllPlanes, ZPixmap
	);
	if (img.im == NULL)
		fatal("failed to get image");
	if (img.im->bits_per_pixel != 32) /* ximg_pixel_get() depends on it */
		fatal("unexpected bits_per_pixel");
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

extern int
main(int argc, char *argv[])
{
	Options opt;
	struct { int x, y, valid; } old = {0};
	XEvent ev;
	Bool queued;
	int npending;

	opt = opt_parse(argc, argv);

	if ((x11.dpy = XOpenDisplay(NULL)) == NULL)
		fatal("failed to open x11 display");

	{ /* TODO: update the x11.root.{w,h} if root changes size */
		XWindowAttributes tmp;
		x11.root.win = DefaultRootWindow(x11.dpy);
		if (XGetWindowAttributes(x11.dpy, x11.root.win, &tmp) == 0)
			fatal("failed to get root window attributes");
		x11.root.h = (uint)tmp.height;
		x11.root.w = (uint)tmp.width;
	}

	{
		XVisualInfo q = {0}, *r;
		int d, dummy;

		q.visualid = XVisualIDFromVisual(DefaultVisual(x11.dpy, DefaultScreen(x11.dpy)));
		if ((r = XGetVisualInfo(x11.dpy, VisualIDMask, &q, &dummy)) == NULL)
			fatal("failed to obtain visual info");
		d = r->depth;
		XFree(r);
		if (d < 24)
			fatal("X server does not support truecolor");
	}

	if (opt.no_mag) {
		x11.cur = XCreateFontCursor(x11.dpy, XC_tcross);
		x11.valid.cur = 1;
	} else {
		cursor_img = XcursorImageCreate(MAG_SIZE, MAG_SIZE);
		if (cursor_img == NULL)
			fatal("failed to create cursor image");
		cursor_img->xhot = cursor_img->yhot = MAG_SIZE / 2;
	}

	{
		int tmp;

		x11.grab_mask = ButtonPressMask | PointerMotionMask;
		tmp = XGrabPointer(
			x11.dpy, x11.root.win, 0, x11.grab_mask, GrabModeAsync,
			GrabModeAsync, x11.root.win, x11.cur, CurrentTime
		);
		x11.valid.ungrab_ptr = tmp == GrabSuccess;
		if (!x11.valid.ungrab_ptr)
			fatal("failed to grab cursor");
	}

	if (opt.quit_on_keypress) {
		int tmp = XGrabKeyboard(
			x11.dpy, x11.root.win, 0,
			GrabModeAsync, GrabModeAsync, CurrentTime
		);
		x11.valid.ungrab_kb = tmp == GrabSuccess;
		if (!x11.valid.ungrab_kb)
			fatal("failed to grab keyboard");
	}

	{
		int i, sigs[] = { SIGINT, SIGTERM, SIGKILL /* one can try */ };
		for (i = 0; i < (int)ARRLEN(sigs); ++i)
			signal(sigs[i], sighandler);
	}

	for (queued = False, npending = 0; 1;) {
		Bool pending;
		struct pollfd pfd;

		pfd.fd = ConnectionNumber(x11.dpy);
		pfd.events = POLLIN;
		pending = queued || npending > 0 || (npending = XPending(x11.dpy)) > 0 ||
		          poll(&pfd, 1, MAX_FRAME_TIME) > 0;

		if (sig_recieved)
			exit(128 + sig_recieved);

		if (!pending) {
			if (!opt.no_mag && old.valid)
				magnify(old.x, old.y);
			continue;
		}

		if (!queued) {
			XNextEvent(x11.dpy, &ev);
			--npending;
		}
		queued = False;

		switch (ev.type) {
		case ButtonPress:
			switch (ev.xbutton.button) {
			case Button1:
				print_color(ev.xbutton.x_root, ev.xbutton.y_root, opt.fmt);
				if (opt.oneshot)
					goto out;
				break;
			case Button4:
				MAG_FACTOR *= MAG_STEP;
				break;
			case Button5:
				MAG_FACTOR = MAX(1.1f, MAG_FACTOR / MAG_STEP);
				break;
			default:
				goto out;
				break;
			}
			break;
		case MotionNotify:
			if (opt.no_mag)
				break;

			old.x = ev.xmotion.x_root;
			old.y = ev.xmotion.y_root;
			old.valid = 1;
			while (npending > 0 || (npending = XPending(x11.dpy)) > 0) {
				XNextEvent(x11.dpy, &ev);
				--npending;
				if (ev.type == MotionNotify) { /* don't act on stale events */
					old.x = ev.xmotion.x_root;
					old.y = ev.xmotion.y_root;
				} else {
					queued = True;
					break;
				}
			}
			magnify(old.x, old.y);
			break;
		case KeyPress:
			if (opt.quit_on_keypress)
				goto out;
			break;
		default:
			break;
		}
	}

out:
#ifdef DEBUG
	if (x11.valid.ungrab_kb)
		XUngrabKeyboard(x11.dpy, CurrentTime);
	if (x11.valid.ungrab_ptr)
		XUngrabPointer(x11.dpy, CurrentTime);
	if (cursor_img != NULL)
		XcursorImageDestroy(cursor_img);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
#endif
	if (x11.dpy != NULL)
		XCloseDisplay(x11.dpy);

	return 0;
}
