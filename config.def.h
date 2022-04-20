enum {
	MAG_FACTOR = 2,
	MAG_WINDOW_SIZE = 128,
	MAG_END_END
};

static ZoomFunc zoom_func = nearest_neighbour;

enum { SQUARE_BORDER_WIDTH = 2 };

static const FilterFunc sq_zoom[] = {
	square_border, crosshair_square
};

static FilterSeq filter = FILTER_SEQ_FROM_ARRAY(sq_zoom);
