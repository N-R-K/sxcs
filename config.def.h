enum {
	MAG_FACTOR = 2,
	MAG_WINDOW_SIZE = 128,
	MAG_BORDER_WIDTH = 2,
	MAG_END_END
};

static const TransfromFunc sq_zoom[] = {
	square_zoomin, square_border, crosshair
};

static TransfromSequence transform = TRANSFORM_SEQ_FROM_FUNC_ARRAY(sq_zoom);
