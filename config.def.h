/* magnification factor. must be >0.0 */
static float MAG_FACTOR = 3.0f;
/* zoom in/out factor */
static const float MAG_STEP = 1.025f;
/* size of the magnifier */
static const uint MAG_SIZE = 192;

/* default scaling function */
static const MagFunc mag_func = nearest_neighbour;

/*
 * COLORS: All the colors here are in ARGB32 format, e.g 0xAARRGGBB.
 */

/* square() options */
static const uint SQUARE_WIDTH = 2;
static const XcursorPixel SQUARE_COLOR = 0xffff3838;

/* xhair() options */
static const uint XHAIR_SIZE = 5;
static const uint XHAIR_BORDER_WIDTH = 2;
static const XcursorPixel XHAIR_COLOR = 0xffff3838;

/* grid() options */
static const uint GRID_SIZE = 5 * 2; /* best kept 2x XHAIR_SIZE */
static const XcursorPixel GRID_COLOR = 0xff3C3836;

/* circle_border() options */
static const uint CIRCLE_RADIUS = 192 / 2; /* best kept 0.5x MAG_SIZE */
static const uint CIRCLE_WIDTH = 2;
static const XcursorPixel CIRCLE_COLOR = 0xffff3838;
static const Bool CIRCLE_TRANSPARENT_OUTSIDE = True;

/* example filter sequences */
static const FilterFunc sq_cross[] = { square, xhair };
static const FilterFunc sq_grid_cross[] = { grid, square, xhair };
static const FilterFunc circle_grid_cross[] = { grid, circle, xhair };

/* default filter sequence, overridden via cli arg `--mag-filters` */
static const FilterSeq filter_default = FILTER_SEQ_FROM_ARRAY(circle_grid_cross);

/* minimum delay between each draw, in milliseconds. */
static const long FRAME_TIME_MIN = 4;

/* default output format, overridden via cli arg.
 * available options: OUTPUT_{NONE,HEX,RGB,HSL,ALL}
 * the options may be OR-ed together, e.g: `OUTPUT_RGB | OUTPUT_HSL`
 */
static const enum output OUTPUT_DEFAULT = OUTPUT_ALL;

/* convenient macro for populating the filter table */
#define FILTER_TABLE_ENTRY(X) #X, sizeof (#X) - 1, X
/* table of filter functions, used by filter_parse() for mapping --mag-filters */
static const struct { const char *str; uint len; FilterFunc f; } FILTER_TABLE[] = {
	{ FILTER_TABLE_ENTRY(square) },
	{ FILTER_TABLE_ENTRY(xhair)  },
	{ FILTER_TABLE_ENTRY(grid)   },
	{ FILTER_TABLE_ENTRY(circle) },
};
