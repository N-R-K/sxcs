/*
 * Copyright (C) 2022-2026 NRK and contributors.
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
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrender.h>

/*
 * macros
 */

#ifndef PROGNAME
	#define PROGNAME "sxcs"
#endif

#define ARRLEN(X)        (sizeof(X) / sizeof((X)[0]))
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#define DIFF(A, B)       ((A) > (B) ? (A) - (B) : (B) - (A))
#define UNUSED(X)        ((void)(X))
/* not correct. but works fine for our usecase in this program */
#define ROUNDF(X)        ((int)((X) + 0.50f))

#define R(X)             ( ((uint)(X) & 0xFF0000) >> 16 )
#define G(X)             ( ((uint)(X) & 0x00FF00) >>  8 )
#define B(X)             ( ((uint)(X) & 0x0000FF) >>  0 )

#define FILTER_SEQ_FROM_ARRAY(X)  { X, ARRLEN(X) }

#ifdef __has_attribute
	#define ATTR(X)  __attribute(X)
#else
	#define ATTR(X)
#endif

#ifdef __GNUC__
	/* when debugging, use gcc/clang and compile with
	 * `-fsanitize=undefined -fsanitize-undefined-trap-on-error`
	 * it'll trap if an unreachable code-path is ever reached.
	 */
	#define ASSERT(X)  ((X) ? (void)0 : __builtin_unreachable())
#else
	#define ASSERT(X)  ((void)0)
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
	uint keyboard          : 1;
	enum output fmt;
} Options;

typedef struct {
	uint w, h;
	uint *pixels;
} Image;

typedef struct {
	uint x, y, w, h;
	int cx, cy;
	struct { uint w, h; } wanted; /* w, h if no clipping occurred */
} ImageInfo;

typedef void (*FilterFunc)(Image *img);
typedef void (*MagFunc)(Image *out, XImage *in, ImageInfo info);

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
 * The zoom functions are given an `Image` pointer where it must output ARGB32
 * pixels. The input is an `XImage`.
 * NOTE: In case of clipping (cursor at the edge of the screen) the input may
 * not be of expected size. `info.{cx,cy}` are the coordinates of the cursor
 * position and `info.wanted.{w,h}` are w/h if no clipping had occurred.
 * so the zoom function must ensure that the middle of the output maps to the
 * cx,cy of the input and it must fill any of the clipped area with transparent
 * pixel (0xff000000).
 */
/* TODO: add bicubic scaling */
static void nearest_neighbour(Image *out, XImage *in, ImageInfo info);
/*
 * filter functions:
 *
 * The filter functions are given a pointer to `XImage` as input. There
 * is no output, the functions can modify it's input as it wants.
 */
/* TODO: add pixels_grid */
static void square(Image *img);
static void xhair(Image *img);
static void grid(Image *img);
static void circle(Image *img);
static void icircle(Image *img);

/*
 * static globals
 */

static struct {
	Display *dpy;
	Cursor cur;
	Pixmap pix;
	GC gc;
	XImage cursor_img;
	XRenderPictFormat *pixfmt;
	Picture picture;
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

static volatile sig_atomic_t sig_recieved;

#include "config.h"

static const FilterSeq *filter = &filter_default;

/*
 * function implementation
 */

ATTR((noreturn, format(printf, 1, 2)))
static void
fatal(const char *fmt, ...)
{
	char prefix[] = PROGNAME ": ";
	va_list ap;

	fflush(stdout);
	fwrite(prefix, 1, sizeof prefix - 1, stderr);
	va_start(ap, fmt);
	if (fmt != NULL)
		vfprintf(stderr, fmt, ap);
	va_end(ap);
	fwrite("\n", 1, 1, stderr);
	exit(1);
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
rgb_to_hsl(uint col)
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
static uint
ximg_pixel_get(const XImage *img, int x, int y)
{
	const size_t off = ((size_t)y * (size_t)img->bytes_per_line) + ((size_t)x * 4);
	const uchar *const p = (uchar *)img->data + off;
	ASSERT(x >= 0); ASSERT(y >= 0);

	if (img->byte_order == MSBFirst) {
		return (uint)p[0] << 24 |
		       (uint)p[1] << 16 |
		       (uint)p[2] <<  8 |
		       (uint)p[3] <<  0;
	} else {
		return (uint)p[3] << 24 |
		       (uint)p[2] << 16 |
		       (uint)p[1] <<  8 |
		       (uint)p[0] <<  0;
	}
}

static uint
get_pixel(int x, int y)
{
	uint ret;

	if (x11.cursor_img.data != NULL) {
		uint m = x11.cursor_img.height / 2;
		ret = ximg_pixel_get(&x11.cursor_img, m, m);
	} else {
		XImage *im = XGetImage(x11.dpy, x11.root.win, x, y, 1, 1, AllPlanes, ZPixmap);
		if (im == NULL)
			fatal("failed to get image");
		ret = ximg_pixel_get(im, 0, 0);
		XDestroyImage(im);
	}
	ret &= 0x00ffffff; /* TODO: is cutting off alpha even needed anymore? */
	return ret;
}

static void
print_color(int x, int y, enum output fmt)
{
	uint pix;

	if (fmt == OUTPUT_NONE)
		return;

	pix = get_pixel(x, y);
	if (fmt & OUTPUT_HEX)
		fprintf(stdout, "hex:\t#%.6X\t", pix);
	if (fmt & OUTPUT_RGB)
		fprintf(stdout, "rgb:\t%u %u %u\t", R(pix), G(pix), B(pix));
	if (fmt & OUTPUT_HSL) {
		HSL tmp = rgb_to_hsl(pix);
		fprintf(stdout, "hsl:\t%u %u %u\t", tmp.h, tmp.s, tmp.l);
	}
	fwrite("\n", 1, 1, stdout);
	fflush(stdout);
	if (ferror(stdout))
		fatal("writing to stdout failed");
}

ATTR((noreturn))
static void
usage(void)
{
	char s[] =
		"usage: "PROGNAME" [options]\n"
		"See the manpage for more details.\n"
	;
	fwrite(s, 1, sizeof s - 1, stdout);
	fflush(stdout);
	exit(ferror(stdout) != 0);
}

ATTR((noreturn))
static void
version(void)
{
	char s[] =
		PROGNAME" v1.2.1\n\n"
		"Copyright (C) 2022-2026 NRK and contributors.\n"
		"License: GPLv3+ <https://gnu.org/licenses/gpl.html>.\n"
		"Upstream: <https://codeberg.org/NRK/sxcs>\n"
	;
	fwrite(s, 1, sizeof s - 1, stdout);
	fflush(stdout);
	exit(ferror(stdout) != 0);
}

static void
filter_parse(Str arg)
{
	static FilterFunc f_buf[16];
	static FilterSeq fs_buf;

	fs_buf.f = f_buf;
	fs_buf.len = 0;

	Str tok;

	if (arg.len == 0)
		fatal("--mag-filters: no argument provided");

	while (str_tok(&arg, &tok, ',')) {
		uint i;
		for (i = 0; i < ARRLEN(FILTER_TABLE); ++i) {
			if (str_eq(tok, FILTER_TABLE[i].str)) {
				if (fs_buf.len >= ARRLEN(f_buf))
					fatal("--mag-filters: too many filters");
				f_buf[fs_buf.len++] = FILTER_TABLE[i].f;
				break;
			}
		}
		if (i == ARRLEN(FILTER_TABLE))
			fatal("--mag-filters: unknown filter `%.*s`", (int)tok.len, tok.s);
	}

	ASSERT(arg.len == 0);
	filter = &fs_buf;
}

/* inspired by https://github.com/skeeto/scratch/blob/master/parsers/imgo.c */
typedef struct { char **argv, *cur, *flag; int len; } OptCtx;
#define OPT(O, SO, LO) ( ((O)->len == 1 && (SO) != 0x0 && (O)->flag[0] == (SO)) || \
	((LO) != 0 && (O)->len-1 == sizeof(LO)-1 && memcmp(LO, (O)->flag+1, (O)->len-1) == 0) )
static int
opt_next(OptCtx *o)
{
	if (o->cur == NULL || *o->cur == '\0') {
		if ((o->cur = *o->argv++) == NULL || *o->cur++ != '-' || *o->cur == '\0')
			return --o->argv, 0;
		if (*o->cur == '-') {
			o->flag = o->cur; o->cur = NULL;
			return o->len = (int)strlen(o->flag);
		}
	}
	o->flag = o->cur++;
	return o->len = 1;
}

static Options
opt_parse(int argc, char *argv[])
{
	Options ret = {0};
	int fmt_default = 1;
	OptCtx o[1] = {0};

	for (o->argv = argv + (argc > 0); opt_next(o);) {
		if      (OPT(o, 0x0, "rgb"))  ret.fmt |= OUTPUT_RGB;
		else if (OPT(o, 0x0, "hex"))  ret.fmt |= OUTPUT_HEX;
		else if (OPT(o, 0x0, "hsl"))  ret.fmt |= OUTPUT_HSL;
		else if (OPT(o, 0x0, "color-none"))  ret.fmt = fmt_default = 0;
		else if (OPT(o, 'o', "one-shot"))    ret.oneshot = 1;
		else if (OPT(o, 'q', "quit-on-keypress"))  ret.quit_on_keypress = 1;
		else if (OPT(o, 'k', "keyboard"))  ret.keyboard = 1;
		else if (OPT(o, 0x0, "mag-none"))  ret.no_mag = 1;
		else if (OPT(o, 0x0, "mag-filters"))  filter_parse(str_from_cstr(*o->argv++));
		else if (OPT(o, 'h', "help"))     usage();
		else if (OPT(o, 0x0, "version"))  version();
		else fatal("unknown argument `-%.*s`", (int)o->len, o->flag);
	}
	if (*o->argv)
		fatal("excess argument: `%s`", *o->argv);

	if (ret.fmt == OUTPUT_NONE && fmt_default)
		ret.fmt = OUTPUT_DEFAULT;

	if (ret.quit_on_keypress && ret.keyboard)
		fatal("--quit-on-keypress and --keyboard cannot be enabled at the same time");

	return ret;
}

static void
nearest_neighbour(Image *out, XImage *in, ImageInfo info)
{
	uint x, y;
	float ocy = (float)out->h / 2.0f;
	float ocx = (float)out->w / 2.0f;
	float icy = (float)info.wanted.h / 2.0f;
	float icx = (float)info.wanted.w / 2.0f;

	for (y = 0; y < out->h; ++y) {
		for (x = 0; x < out->w; ++x) {
			float oy = ((float)y - ocy) / ocy;
			float ox = ((float)x - ocx) / ocx;
			int iy = ROUNDF((float)info.cy + (icy * oy));
			int ix = ROUNDF((float)info.cx + (icx * ox));
			uint tmp;

			if ((iy < 0 || iy >= (int)info.h) || (ix < 0 || ix >= (int)info.w))
				tmp = 0xff000000;
			else
				tmp = ximg_pixel_get(in, ix, iy) | 0xff000000;
			out->pixels[y * out->w + x] = tmp;
		}
	}
}

static void
square(Image *img)
{
	size_t i, k;
	const uint b = SQUARE_WIDTH;

	i = 0;
	while (i < img->w * b + b) /* draw the top border + 1 left side */
		img->pixels[i++] = SQUARE_COLOR;
	do {
		i += img->w - b * 2; /* skip the mid */
		for (k = 0; k < b*2; ++k)
			img->pixels[i++] = SQUARE_COLOR;
	} while (i < (img->h - b) * img->w);
	while (i < img->w * img->h) /* draw the rest */
		img->pixels[i++] = SQUARE_COLOR;
}

static void
xhair(Image *img)
{
	uint x, y, *p = img->pixels;
	const uint c = img->h / 2;
	const uint b = XHAIR_SIZE;
	const uint bw = XHAIR_BORDER_WIDTH;

	for (y = c - b; y <= c + b; ++y) {
		for (x = c - b; x <= c + b; ++x) {
			if (DIFF(x, c) > b - bw || DIFF(y, c) > b - bw)
				p[y * img->w + x] = XHAIR_COLOR;
		}
	}
}

static void
grid(Image *img)
{
	uint x, y, *p = img->pixels;
	const uint z = GRID_SIZE;
	const uint c = (img->h / 2) + (z / 2);

	for (y = 0; y < img->h; ++y) {
		if (DIFF(c, y) % z == 0) {
			for (x = 0; x < img->w; ++x)
				p[y * img->w + x] = GRID_COLOR;
		} else for (x = c % z; x < img->w; x += z) {
			p[y * img->w + x] = GRID_COLOR;
		}
	}
}

static void
four_point_draw(Image *img, uint x, uint y, uint col) /* naming is hard */
{
	uint w = img->w, h = img->h, *p = img->pixels;
	ASSERT(x <= w/2); ASSERT(y <= h/2);
	p[y * w + x] = col;
	p[y * w + (w - x - 1)] = col;
	p[(h - y - 1) * w + x] = col;
	p[(h - y - 1) * w + (w - x - 1)] = col;
}

/* TODO: reduce jaggedness */
static void
circle_core(Image *img, int xcolor)
{
	uint x, y, h = img->h, w = img->w;
	int outline = xcolor && CIRCLE_WIDTH > 0;
	uint r = CIRCLE_RADIUS;
	uint br = r - CIRCLE_WIDTH;
	uint olr = r + outline;
	uint c = h / 2;
	uint col = xcolor ? (get_pixel(-1,-1) | 0xff000000) : CIRCLE_COLOR;
	uint ocol = rgb_to_hsl(col).l > 50 ? 0x80000000 : 0x80FFFFFF;

	for (y = 0; y < h / 2 + (h & 1); ++y) {
		for (x = 0; x < w / 2 + (w & 1); ++x) {
			uint tx = c - x;
			uint ty = c - y;
			uint x2y2 = (tx * tx) + (ty * ty);

			if (x2y2 > (olr * olr)) { /* outside the circle border */
				if (CIRCLE_TRANSPARENT_OUTSIDE)
					four_point_draw(img, x, y, 0x0);
			} else if (outline && x2y2 > (r * r)) { /* outside outline border */
				four_point_draw(img, x, y, ocol);
			} else if (x2y2 > (br * br)) { /* inside the circle border */
				four_point_draw(img, x, y, col);
			} else { /* inside the circle, nothing to do. move on to the next y */
				break;
			}
		}
	}
}

static void  circle(Image *img) { circle_core(img, 0); }
static void icircle(Image *img) { circle_core(img, 1); }

static void
magnify(const int x, const int y)
{
	const uint c = (uint)((float)MAG_SIZE / MAG_FACTOR) + 1;
	const int off = (c - 1) / 2;
	XImage *raw, *cim = &x11.cursor_img;
	int hot = cim->width / 2;
	uint i;
	Cursor new_cur;
	Image cimg = {0};
	ImageInfo info = {0};

	cimg.w = cim->width;
	cimg.h = cim->height;
	cimg.pixels = (uint *)cim->data;

	info.x = (uint)MAX(0, x - off);
	info.y = (uint)MAX(0, y - off);
	info.w = MIN(c, x11.root.w - info.x);
	info.h = MIN(c, x11.root.h - info.y);
	info.cx = x - (int)info.x;
	info.cy = y - (int)info.y;
	info.wanted.w = info.wanted.h = c;
	/* TODO: look into Shm extension to reduce transfer overhead. */
	raw = XGetImage(
		x11.dpy, x11.root.win, (int)info.x, (int)info.y, info.w, info.h,
		AllPlanes, ZPixmap
	);
	if (raw == NULL)
		fatal("failed to get image");
	if (raw->bits_per_pixel != 32 ||
	    raw->bytes_per_line != (raw->width * 4) ||
	    !(raw->depth == 24 || raw->depth == 32))
	{ /* ximg_pixel_get() depends on these */
		fatal("unexpected XImage format");
	}
	mag_func(&cimg, raw, info);
	XDestroyImage(raw);

	for (i = 0; i < filter->len; ++i)
		filter->f[i](&cimg);

	XPutImage(x11.dpy, x11.pix, x11.gc, cim, 0, 0, 0, 0, cim->width, cim->height);
	new_cur = XRenderCreateCursor(x11.dpy, x11.picture, hot, hot);
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
	struct { int x, y; } old = {0};
	XEvent ev;
	Bool queued;
	int npending;

	opt = opt_parse(argc, argv);

	if ((x11.dpy = XOpenDisplay(NULL)) == NULL)
		fatal("failed to open x11 display");

	{
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
		union { int i; char bytes[sizeof(int)]; } u = {1}; /* cppcheck-suppress unusedStructMember */
		XImage *cim = &x11.cursor_img;
		cim->width = cim->height = MAG_SIZE;
		cim->format = ZPixmap;
		cim->byte_order = u.bytes[0] == 1 ? LSBFirst : MSBFirst;
		cim->bitmap_unit = 32;
		cim->bitmap_bit_order = cim->byte_order;
		cim->bitmap_pad = 32;
		cim->depth = 32;
		cim->bits_per_pixel = 32;
		cim->red_mask   = 0xFF0000;
		cim->green_mask = 0x00FF00;
		cim->blue_mask  = 0x0000FF;
		cim->data = malloc((size_t)cim->width * cim->height * 4);
		if (cim->data == NULL)
			fatal("malloc failed: %s", strerror(errno));
		if (!XInitImage(cim))
			fatal("failed to initialize XImage");

		x11.pix = XCreatePixmap(
			x11.dpy, x11.root.win,
			cim->width, cim->height, 32
		);
		x11.gc = XCreateGC(x11.dpy, x11.pix, 0, NULL);
		x11.pixfmt = XRenderFindStandardFormat(x11.dpy, PictStandardARGB32);
		if (x11.pixfmt == NULL)
			fatal("failed to find standard ARGB32 format");
		x11.picture = XRenderCreatePicture(x11.dpy, x11.pix, x11.pixfmt, 0, NULL);
	}

	if (opt.quit_on_keypress || opt.keyboard) {
		/* when launched via dwm keybinding, it fails the grab since
		 * dwm has it grabbed already. listen for FocusChangeMask and
		 * keep retrying. */
		int res;
		XSelectInput(x11.dpy, x11.root.win, FocusChangeMask);
		do {
			res = XGrabKeyboard(
				x11.dpy, x11.root.win, 0,
				GrabModeAsync, GrabModeAsync, CurrentTime
			);
			if (res == AlreadyGrabbed)
				XNextEvent(x11.dpy, &ev);
		} while (res == AlreadyGrabbed);
		XSelectInput(x11.dpy, x11.root.win, 0x0);
		x11.valid.ungrab_kb = res == GrabSuccess;
		if (!x11.valid.ungrab_kb)
			fatal("failed to grab keyboard");
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

	{
		int i, sigs[] = { SIGINT, SIGTERM, SIGKILL /* one can try */ };
		for (i = 0; i < (int)ARRLEN(sigs); ++i)
			signal(sigs[i], sighandler);
	}

	if (!opt.no_mag) {
		Window tmpw; int tmpi; unsigned tmpu;
		XQueryPointer(
			x11.dpy, x11.root.win, &tmpw, &tmpw,
			&old.x, &old.y, &tmpi, &tmpi, &tmpu
		);
		magnify(old.x, old.y);
	}

	for (queued = False, npending = 0; 1;) {
		Bool pending = queued || npending > 0 ||
		               (npending = XPending(x11.dpy)) > 0;
		if (!pending) {
			struct pollfd pfd = {0};
			pfd.fd = ConnectionNumber(x11.dpy);
			pfd.events = POLLIN;
			poll(&pfd, 1, MAX_FRAME_TIME);
			pending = (npending = XPending(x11.dpy)) > 0;
		}

		if (sig_recieved)
			exit(128 + sig_recieved);

		if (!pending) {
			if (!opt.no_mag)
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
		case KeyPress: {
			KeySym k = None;
			int x = ev.xkey.x_root, y = ev.xkey.y_root;
			int delta = (ev.xkey.state & ControlMask) ? 1 :
			            ((ev.xkey.state & ShiftMask) ? 128 : 16);

			if (opt.quit_on_keypress)
				goto out;

			if (opt.keyboard) {
				char junk;
				XLookupString(&ev.xkey, &junk, 1, &k, NULL);
			}

			switch (k) {
			case XK_h: case XK_H: case XK_Left:  x -= delta; break;
			case XK_l: case XK_L: case XK_Right: x += delta; break;
			case XK_k: case XK_K: case XK_Up:    y -= delta; break;
			case XK_j: case XK_J: case XK_Down:  y += delta; break;
			case XK_q: case XK_Q: case XK_Escape: goto out; break;
			case XK_minus: case XK_KP_Subtract:
				MAG_FACTOR = MAX(1.1f, MAG_FACTOR / MAG_STEP);
				break;
			case XK_plus: case XK_KP_Add:
				MAG_FACTOR *= MAG_STEP;
				break;
			case XK_space:
				print_color(ev.xkey.x_root, ev.xkey.y_root, opt.fmt);
				if (opt.oneshot)
					goto out;
				break;
			default: break;
			}
			if (x != ev.xkey.x_root || y != ev.xkey.y_root)
				XWarpPointer(x11.dpy, None, x11.root.win, 0, 0, 0, 0, x, y);
		} break;
		default: break;
		}
	}

out:
#ifdef DEBUG
	if (x11.valid.ungrab_kb)
		XUngrabKeyboard(x11.dpy, CurrentTime);
	if (x11.valid.ungrab_ptr)
		XUngrabPointer(x11.dpy, CurrentTime);
	free(x11.cursor_img.data);
	if (x11.pix != None)
		XFreePixmap(x11.dpy, x11.pix);
	if (x11.gc != None)
		XFreeGC(x11.dpy, x11.gc);
	if (x11.picture != None)
		XRenderFreePicture(x11.dpy, x11.picture);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
#endif
	if (x11.dpy != NULL)
		XCloseDisplay(x11.dpy);

	return 0;
}
