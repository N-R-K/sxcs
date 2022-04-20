static const float MAG_FACTOR = 2.0f;
static const uint MAG_WINDOW_SIZE = 128;

static const MagFunc mag_func = nearest_neighbour;

/* square_border() options */
static const uint SQUARE_BORDER_WIDTH = 2;
static const XcursorPixel SQUARE_BORDER_COLOR = 0xffff3838;

/* crosshair_square() options */
static const XcursorPixel CROSSHAIR_SQUARE_COLOR = 0xffff3838;

static const FilterFunc sq_zoom[] = {
	square_border, crosshair_square
};

static const FilterSeq filter = FILTER_SEQ_FROM_ARRAY(sq_zoom);
