#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#include "util.h"

/*
 * macros
 */

#define R(X)             (((unsigned long)(X) & 0xFF0000) >> 16)
#define G(X)             (((unsigned long)(X) & 0x00FF00) >>  8)
#define B(X)             (((unsigned long)(X) & 0x0000FF) >>  0)

/*
 * types
 */

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
	uint pad : 9;
} HSL;

typedef struct {
	uint oneshot           : 1; /* TODO: implement this */
	uint quit_on_keypress  : 1; /* TODO: implement this */
	uint help              : 1; /* TODO: implement this */
	enum output fmt;
} Options;

/*
 * static globals
 */

static struct {
	Display *dpy;
	Cursor cur;
	struct {
		Window win;
		uint w, h;
	} root;
} x11;

/*
 * function implementation
 */

static HSL
rgb_to_hsl(u32 col)
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

	im = XGetImage(x11.dpy, x11.root.win, x, y,
	               1, 1, /* w, h */
	               AllPlanes, ZPixmap);
	if (im == NULL)
		error(1, 0, "failed to get image");
	pix = XGetPixel(im, 0, 0);

	printf("color:");
	if (fmt & OUTPUT_HEX)
		printf("\thex: 0x%.6lX", pix);
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
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			ret.help = 1;
		else
			error(1, 0, "unknown argument `%s`.", argv[i]);
	}

	if (ret.fmt == 0)
		ret.fmt = ~0;

	return ret;
}

static void
cleanup(void)
{
	if (x11.dpy != NULL) {
		XFreeCursor(x11.dpy, x11.cur);
		XUngrabPointer(x11.dpy, CurrentTime);
		XCloseDisplay(x11.dpy);
	}
}

extern int
main(int argc, const char *argv[])
{
	Options opt;

	atexit(cleanup);

	opt = opt_parse(argc, argv);

	if ((x11.dpy = XOpenDisplay(NULL)) == NULL)
		error(1, 0, "failed to open x11 display");

	{
		XWindowAttributes tmp;
		x11.root.win = DefaultRootWindow(x11.dpy);
		XGetWindowAttributes(x11.dpy, x11.root.win, &tmp);
		x11.root.h = tmp.height;
		x11.root.w = tmp.width;
	}

	x11.cur = XCreateFontCursor(x11.dpy, XC_tcross);

	if (XGrabPointer(x11.dpy, x11.root.win, 0, ButtonPressMask | PointerMotionMask,
	                 GrabModeAsync, GrabModeAsync, x11.root.win, x11.cur,
	                 CurrentTime) != GrabSuccess)
	{
		error(1, 0, "failed to grab cursor");
	}

	while (1) {
		XEvent ev;

		switch (XNextEvent(x11.dpy, &ev), ev.type) {
		case ButtonPress:
			print_color(ev.xbutton.x_root, ev.xbutton.y_root, opt.fmt);
			exit(0);
			break;
		case MotionNotify: /* TODO */
			/* error(1, 0, "recieved MotionNotify event."); */
			break;
		default:
			error(1, 0, "recieved unknown event.");
			break;
		}
	}

	return 0;
}
