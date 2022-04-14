#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "util.h"

#define R(X)             (((unsigned long)(X) & 0xFF0000) >> 16)
#define G(X)             (((unsigned long)(X) & 0x00FF00) >>  8)
#define B(X)             (((unsigned long)(X) & 0x0000FF) >>  0)

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

static void
cleanup(void)
{
	if (x11.dpy != NULL) {
		/* XFreeCursor(x11.dpy, x11.cur); */
		XUngrabPointer(x11.dpy, CurrentTime);
		XCloseDisplay(x11.dpy);
	}
}

/* TODO: accept arguments */
extern int
main(void)
{

	atexit(cleanup);

	if ((x11.dpy = XOpenDisplay(NULL)) == NULL)
		error(1, 0, "failed to open x11 display");

	{
		XWindowAttributes tmp;
		x11.root.win = DefaultRootWindow(x11.dpy);
		XGetWindowAttributes(x11.dpy, x11.root.win, &tmp);
		x11.root.h = tmp.height;
		x11.root.w = tmp.width;
	}
	/* TODO: error check this */
	XGrabPointer(x11.dpy, x11.root.win, 0, ButtonPressMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, x11.root.win, x11.cur, CurrentTime);

	/* TODO: cursor is fucked up right now */
	while (1) {
		XEvent ev;

		switch (XNextEvent(x11.dpy, &ev), ev.type) {
		case ButtonPress:
			print_color(ev.xbutton.x_root, ev.xbutton.y_root, ~0);
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
