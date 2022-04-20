static const uint MAG_FACTOR = 2; /* TODO: make this a float */
static const uint MAG_WINDOW_SIZE = 128;

static const MagFunc mag_func = nearest_neighbour;

static const uint SQUARE_BORDER_WIDTH = 2;

static const FilterFunc sq_zoom[] = {
	square_border, crosshair_square
};

static const FilterSeq filter = FILTER_SEQ_FROM_ARRAY(sq_zoom);
