















#ifndef QUIRC_INTERNAL_H_
#define QUIRC_INTERNAL_H_

#include "quirc.h"

#define QUIRC_PIXEL_WHITE	0
#define QUIRC_PIXEL_BLACK	1
#define QUIRC_PIXEL_REGION	2

#ifndef QUIRC_MAX_REGIONS
#define QUIRC_MAX_REGIONS	254
#endif
#define QUIRC_MAX_CAPSTONES	32
#define QUIRC_MAX_GRIDS		8

#define QUIRC_PERSPECTIVE_PARAMS	8

#if QUIRC_MAX_REGIONS < UINT8_MAX
typedef uint8_t quirc_pixel_t;
#elif QUIRC_MAX_REGIONS < UINT16_MAX
typedef uint16_t quirc_pixel_t;
#else
#error "QUIRC_MAX_REGIONS > 65534 is not supported"
#endif

struct quirc_region {
	struct quirc_point	seed;
	int			count;
	int			capstone;
};

struct quirc_capstone {
	int			ring;
	int			stone;

	struct quirc_point	corners[4];
	struct quirc_point	center;
	double			c[QUIRC_PERSPECTIVE_PARAMS];

	int			qr_grid;
};

struct quirc_grid {

	int			caps[3];


	int			align_region;
	struct quirc_point	align;


	struct quirc_point	tpep[3];
	int			hscan;
	int			vscan;


	int			grid_size;
	double			c[QUIRC_PERSPECTIVE_PARAMS];
};

struct quirc {
	uint8_t			*image;
	quirc_pixel_t		*pixels;
	int			w;
	int			h;

	int			num_regions;
	struct quirc_region	regions[QUIRC_MAX_REGIONS];

	int			num_capstones;
	struct quirc_capstone	capstones[QUIRC_MAX_CAPSTONES];

	int			num_grids;
	struct quirc_grid	grids[QUIRC_MAX_GRIDS];
};





#define QUIRC_MAX_VERSION     40
#define QUIRC_MAX_ALIGNMENT   7

struct quirc_rs_params {
	int             bs;
	int             dw;
	int             ce;
};

struct quirc_version_info {
	int				data_bytes;
	int				apat[QUIRC_MAX_ALIGNMENT];
	struct quirc_rs_params          ecc[4];
};

extern const struct quirc_version_info quirc_version_db[QUIRC_MAX_VERSION + 1];

#endif
