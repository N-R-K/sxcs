static const float MAG_FACTOR = 2.0f;
static const uint MAG_WINDOW_SIZE = 128;

static const MagFunc mag_func = nearest_neighbour;

/* square_border() options */
static const uint SQUARE_BORDER_WIDTH = 2;
static const XcursorPixel SQUARE_BORDER_COLOR = 0xffff3838;

/* crosshair_square() options */
static const uint CROSSHAIR_SQUARE_SIZE = 4;
static const uint CROSSHAIR_SQUARE_BORDER_WIDTH = 2;
static const XcursorPixel CROSSHAIR_SQUARE_COLOR = 0xffff3838;

/* grid() options */
static const uint GRID_SIZE = 8; /* best kept 2x CROSSHAIR_SQUARE_SIZE */
static const XcursorPixel GRID_COLOR = 0xff3C3836;

static const FilterFunc sq_cross[] = {
	square_border, crosshair_square
};

static const FilterFunc sq_grid_cross[] = {
	grid, square_border, crosshair_square
};

static const FilterSeq filter = FILTER_SEQ_FROM_ARRAY(sq_grid_cross);

static const int FRAMETIME = 16; /* ms before forced redraw */
