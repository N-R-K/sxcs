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
	OUTPUT_END
};

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
	/* TODO: HSL output */
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
