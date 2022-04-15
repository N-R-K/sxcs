#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

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
	struct {
		uint cur         : 1;
		uint ungrab_ptr  : 1;
		uint ungrab_kb   : 1;
	} valid;
} x11;

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
usage(void)
{
	fprintf(stderr,
	        "usage: %s [options]\n"
	        "  -h, --help:             show usage\n"
	        "  -o, --one-shot:         quit after picking\n"
	        "  -q, --quit-on-keypress: quit on keypress\n"
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
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			usage();
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
	if (x11.valid.ungrab_kb)
		XUngrabKeyboard(x11.dpy, CurrentTime);
	if (x11.valid.ungrab_ptr)
		XUngrabPointer(x11.dpy, CurrentTime);
	if (x11.valid.cur)
		XFreeCursor(x11.dpy, x11.cur);
	if (x11.dpy != NULL)
		XCloseDisplay(x11.dpy);
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
		case MotionNotify: /* TODO: zoom in */
			do { /* don't act on stale events */
				if (XPending(x11.dpy) > 0) {
					XEvent next_ev, dummy;
					XPeekEvent(x11.dpy, &next_ev);
					discard = next_ev.type == MotionNotify;
					if (discard)
						XNextEvent(x11.dpy, &dummy);
				} else {
					break;
				}
			} while (discard);
			/* error(1, 0, "recieved MotionNotify event."); */
			break;
		default:
			/* error(0, 0, "recieved unknown event: `%d`", ev.type); */
			break;
		}
	}

	return 0;
}
