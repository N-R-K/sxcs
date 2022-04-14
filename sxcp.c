#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "util.h"

static struct {
	Display *dpy;
	Cursor cur;
	struct {
		Window win;
		uint w, h;
	} root;
} x11;

static void
cleanup(void)
{
	if (x11.dpy != NULL) {
		/* XFreeCursor(x11.dpy, x11.cur); */
		XUngrabPointer(x11.dpy, CurrentTime);
		XCloseDisplay(x11.dpy);
	}
}

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
		XImage *im;
		ulong pix;

		switch (XNextEvent(x11.dpy, &ev), ev.type) {
		case ButtonPress: /* TODO: more option on color output */
			im = XGetImage(x11.dpy, x11.root.win,
			               ev.xbutton.x_root, ev.xbutton.y_root, /* x, y */
			               1, 1, /* w, h */
			               AllPlanes, ZPixmap);
			if (im == NULL)
				error(1, 0, "failed to get image");
			pix = XGetPixel(im, 0, 0);
			printf("color: 0x%.6lX\n", pix);
			XDestroyImage(im);
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
