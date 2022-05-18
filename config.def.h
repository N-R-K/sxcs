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

/* square_border() options */
static const uint SQUARE_BORDER_WIDTH = 2;
static const XcursorPixel SQUARE_BORDER_COLOR = 0xffff3838;

/* crosshair_square() options */
static const uint CROSSHAIR_SQUARE_SIZE = 5;
static const uint CROSSHAIR_SQUARE_BORDER_WIDTH = 2;
static const XcursorPixel CROSSHAIR_SQUARE_COLOR = 0xffff3838;

/* grid() options */
static const uint GRID_SIZE = 5 * 2; /* best kept 2x CROSSHAIR_SQUARE_SIZE */
static const XcursorPixel GRID_COLOR = 0xff3C3836;

/* circle_border() options */
static const uint CIRCLE_RADIUS = 192 / 2; /* best kept 0.5x MAG_SIZE */
static const uint CIRCLE_WIDTH = 2;
static const XcursorPixel CIRCLE_COLOR = 0xffff3838;
static const Bool CIRCLE_TRANSPARENT_OUTSIDE = True;

/* example filter sequences */
static const FilterFunc sq_cross[] = { square_border, crosshair_square };
static const FilterFunc sq_grid_cross[] = { grid, square_border, crosshair_square };
static const FilterFunc circle_grid_cross[] = { grid, circle, crosshair_square };

/* default filter sequence, overridden via cli arg `--mag-filters` */
static const FilterSeq filter_default = FILTER_SEQ_FROM_ARRAY(circle_grid_cross);

/* max time (in ms) allowed to go on without a redraw */
static const int MAX_FRAME_TIME = 16;

/* default output format, overridden via cli arg.
 * available options: OUTPUT_{NONE,HEX,RGB,HSL,ALL}
 * the options may be OR-ed together, e.g: `OUTPUT_RGB | OUTPUT_HSL`
 */
static const enum output OUTPUT_DEFAULT = OUTPUT_ALL;
