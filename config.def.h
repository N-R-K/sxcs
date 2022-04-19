enum {
	MAG_FACTOR = 2,
	MAG_WINDOW_SIZE = 128,
	MAG_END_END
};

static const TransfromFunc sq_zoom[] = {
	square_zoomin
};

static TransfromSequence transform = TRANSFORM_SEQ_FROM_FUNC_ARRAY(sq_zoom);
