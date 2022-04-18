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
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xcomposite.h>

/*
 * macros
 */

#define ARRLEN(X)        (sizeof(X) / sizeof((X)[0]))
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))

#define R(X)             (((unsigned long)(X) & 0xFF0000) >> 16)
#define G(X)             (((unsigned long)(X) & 0x00FF00) >>  8)
#define B(X)             (((unsigned long)(X) & 0x0000FF) >>  0)

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
	OUTPUT_END
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
} Image;

typedef struct {
	Window win;
	int x, y;
} WinCor;

#include "config.h"

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
static void img_out_init(Image *img);
static Bool win_has_property(Window win, Atom atom);
static WinCor get_win_coordinates(int x, int y);
static XImage * img_create_from_cor(uint x, uint y, uint w, uint h);
static void img_magnify(Image *out, const Image *in);
static void magnify(const int x, const int y);
CLEANUP static void cleanup(void);

/*
 * static globals
 */

static struct {
	Display *dpy;
	Visual *vis;
	Window win;
	Cursor cur;
	GC gc;
	Colormap cmap;
	int screen;
	int depth;
	uint w, h;
	Pixmap pm;
	Picture pixpic;
	XRenderPictFormat *vfmt, *sfmt;
	struct {
		Window win;
		uint w, h;
	} root;
	struct {
		uint win         : 1;
		uint cur         : 1;
		uint pm          : 1;
		uint pixpic      : 1;
		uint gc          : 1;
		uint ungrab_ptr  : 1;
		uint ungrab_kb   : 1;
		uint unredirect  : 1;
	} valid;
} x11;

static Image img_out;

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

	ret.h = h;
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
	        "      --no-mag:           disable maginfier\n"
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
		else if (strcmp(argv[i], "--no-mag") == 0)
			ret.no_mag = 1;
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			usage();
		else
			error(1, 0, "unknown argument `%s`.", argv[i]);
	}

	if (ret.fmt == 0)
		ret.fmt = ~0;

	return ret;
}

/* this feels like a retarded thing to do...
 * do i really need an XImage?
 */
static void
img_out_init(Image *img)
{
	img->x = img->y = 0;
	img->w = img->h = MAGNIFY_WINDOW_SIZE;
	/* TODO: use XInitImage instead ? */
	img->im = XGetImage(x11.dpy, x11.root.win, img->x, img->y,
	                    img->w, img->h, AllPlanes, ZPixmap);
	if (img->im == NULL)
		error(1, 0, "failed to get image");
}

static Bool
win_has_property(Window win, Atom atom)
{
	Atom type_ret = None;
	uchar *prop_ret = NULL;
	int format_ret;
	ulong bytes_after, num_ret;

	XGetWindowProperty(x11.dpy, win, atom, 0, 0, False, AnyPropertyType,
	                   &type_ret, &format_ret, &num_ret,
	                   &bytes_after, &prop_ret);
	if (prop_ret != NULL)
		XFree(prop_ret);

	return type_ret != None;
}

static WinCor
get_win_coordinates(int x, int y)
{
	WinCor ret = {0};
	Window dummy, *childs = NULL;
	int i;
	uint nchild;
	static Atom wm_state = None;

	if (wm_state == None)
		wm_state = XInternAtom(x11.dpy, "WM_STATE", False);

	ret.win = x11.root.win;
	ret.x = x;
	ret.y = y;
	if (XQueryTree(x11.dpy, x11.root.win, &dummy, &dummy, &childs, &nchild) == 0)
		error(1, 0, "XQueryTree failed");
	/* TODO: this may not be actually correct... */
	for (i = (int)nchild - 1; i >= 0; --i) {
		XWindowAttributes tmp;
		XGetWindowAttributes(x11.dpy, childs[i], &tmp);
		if ((x > tmp.x && x < tmp.x + tmp.width) &&
		    (y > tmp.y && y < tmp.y + tmp.height) &&
		    tmp.map_state == IsViewable && tmp.class == InputOutput &&
		    win_has_property(childs[i], wm_state))
		{
			ret.win = childs[i];
			XTranslateCoordinates(x11.dpy, x11.root.win, ret.win,
			                      x, y, &ret.x, &ret.y, &dummy);
			break;
		}
	}
	XFree(childs);

	return ret;
}

/* FIXME: deal with overlapping windows */
static XImage *
img_create_from_cor(uint x, uint y, uint w, uint h)
{
	XImage *ret = NULL;
	XRenderPictureAttributes pattr;
	Picture pic;
	int alpha = x11.vfmt->type == PictTypeDirect && x11.vfmt->direct.alphaMask;
	WinCor dst;

	dst = get_win_coordinates(x, y);

	pattr.subwindow_mode = IncludeInferiors;
	pic = XRenderCreatePicture(x11.dpy, dst.win, x11.vfmt, CPSubwindowMode, &pattr);
	XRenderComposite(x11.dpy, alpha ? PictOpOver : PictOpSrc, pic, None,
	                 x11.pixpic, dst.x, dst.y, 0, 0, 0, 0, w, h);
	ret = XGetImage(x11.dpy, x11.pm, 0, 0, w, h, AllPlanes, ZPixmap);
	XRenderFreePicture(x11.dpy, pic);

	return ret;
}

/* FIXME: center properly when clipped */
static void
img_magnify(Image *out, const Image *in)
{
	uint x, y;

	for (y = 0; y < out->h; ++y) {
		for (x = 0; x < out->w; ++x) {
			float oy = (float)y / (float)out->h;
			float ox = (float)x / (float)out->w;
			uint iy = oy * in->h;
			uint ix = ox * in->w;
			ulong tmp = XGetPixel(in->im, ix, iy);
			XPutPixel(out->im, x, y, tmp);
		}
	}
}

/*
 * TODO: draw grid around each pixel
 * TODO: add circle output
 */
static void
magnify(const int x, const int y)
{
	const int ms = MAGNIFY_WINDOW_SIZE / ZOOM_FACTOR;
	const int moff = ms / ZOOM_FACTOR;
	XWindowChanges ch = {0};
	Image img;

	img.x = MAX(0, x - moff);
	img.y = MAX(0, y - moff);
	img.w = MIN(ms, x11.root.w - img.x);
	img.h = MIN(ms, x11.root.h - img.y);

	img.im = img_create_from_cor(img.x, img.y, img.w, img.h);
	if (img.im == NULL)
		error(1, 0, "failed to get image");
	img_magnify(&img_out, &img);
	XPutImage(x11.dpy, x11.win, x11.gc, img_out.im,
	          0, 0, 0, 0, img_out.w, img_out.h);
	XDestroyImage(img.im);

	ch.x = x - MAGNIFY_WINDOW_SIZE / 2;
	ch.y = y - MAGNIFY_WINDOW_SIZE / 2;
	XConfigureWindow(x11.dpy, x11.win, CWX | CWY, &ch);
}

CLEANUP static void
cleanup(void)
{
	if (img_out.im != NULL)
		XDestroyImage(img_out.im);
	if (x11.valid.unredirect)
		XCompositeUnredirectSubwindows(x11.dpy, x11.root.win, CompositeRedirectAutomatic);
	if (x11.valid.ungrab_kb)
		XUngrabKeyboard(x11.dpy, CurrentTime);
	if (x11.valid.ungrab_ptr)
		XUngrabPointer(x11.dpy, CurrentTime);
	if (x11.valid.gc)
		XFreeGC(x11.dpy, x11.gc);
	if (x11.valid.pixpic)
		XRenderFreePicture(x11.dpy, x11.pixpic);
	if (x11.valid.pm)
		XFreePixmap(x11.dpy, x11.pm);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
	if (x11.valid.win)
		XDestroyWindow(x11.dpy, x11.win);
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
	}

	if (!opt.no_mag) {
		int major, minor;

		major = 0; minor = 2;
		if (!XCompositeQueryVersion(x11.dpy, &major, &minor))
			error(1, 0, "need XComposite 0.2 or above");

		major = 0; minor = 0;
		if (!XRenderQueryVersion(x11.dpy, &major, &minor))
			error(1, 0, "need XRender");
	}

	if (!opt.no_mag) {
		XSetWindowAttributes attr;
		ulong attr_mask = 0;

		x11.w = x11.h = MAGNIFY_WINDOW_SIZE;
		x11.screen = DefaultScreen(x11.dpy);

		x11.vis = DefaultVisual(x11.dpy, x11.screen);
		x11.depth = DefaultDepth(x11.dpy, x11.screen);

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

		x11.cmap = XCreateColormap(x11.dpy, x11.root.win, x11.vis, AllocNone);
		attr.colormap = x11.cmap;
		attr_mask |= CWColormap;

		attr.event_mask = ExposureMask | KeyPressMask;
		attr_mask |= CWEventMask;

		attr.override_redirect = True;
		attr_mask |= CWOverrideRedirect;

		attr.background_pixel = BlackPixel(x11.dpy, x11.screen);
		attr_mask |= CWBackPixel;

		x11.win = XCreateWindow(x11.dpy, x11.root.win,
		                        -1000, -1000, x11.w, x11.h, 0, x11.depth,
		                        InputOutput, x11.vis, attr_mask, &attr);
		x11.valid.win = 1;

		x11.gc = XCreateGC(x11.dpy, x11.win, 0, None);
		x11.valid.gc = 1;

		XMapRaised(x11.dpy, x11.win);
	}

	if (!opt.no_mag) {
		x11.vfmt = XRenderFindVisualFormat(x11.dpy, x11.vis); /* TODO: free this? */
		x11.sfmt = XRenderFindStandardFormat(x11.dpy, PictStandardARGB32); /* TODO: same as above */
		x11.pm = XCreatePixmap(x11.dpy, x11.root.win,
		                       MAGNIFY_WINDOW_SIZE, MAGNIFY_WINDOW_SIZE,
		                       32);
		x11.valid.pm = 1;

		if (x11.vfmt == NULL || x11.sfmt == NULL)
			error(1, 0, "couldn't find format");

		x11.pixpic = XRenderCreatePicture(x11.dpy, x11.pm, x11.sfmt, None, NULL); /* TODO: error check? */
		x11.valid.pixpic = 1;

		XCompositeRedirectSubwindows(x11.dpy, x11.root.win, CompositeRedirectAutomatic);
		x11.valid.unredirect = 1;
		img_out_init(&img_out);
	}

	x11.cur = XCreateFontCursor(x11.dpy, XC_tcross);
	x11.valid.cur = 1;

	x11.valid.ungrab_ptr = XGrabPointer(x11.dpy, x11.root.win, 0,
	                                    ButtonPressMask | PointerMotionMask,
	                                    GrabModeAsync, GrabModeAsync,
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
			if (!opt.no_mag)
				magnify(ev.xbutton.x_root, ev.xbutton.y_root);
			break;
		default:
			/* error(0, 0, "recieved unknown event: `%d`", ev.type); */
			break;
		}
	}

	return 0;
}
