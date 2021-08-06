#pragma once
#include <foundation/api_types.h>

enum {
	WORLD_WIDTH = 1000,
	WORLD_HEIGHT = 1000,

	STARTING_BESTIOLE_COUNT = 1000,
	BOUFFE_COUNT = 5000
};

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

extern tm_tt_id_t* ptr_asset_bestiole;
extern tm_tt_id_t* ptr_asset_bouffe;

#define ENERY_DRAIN 5.0
#define ENERY_DRAIN_MOVE_MULTIPLIER 2.0
#define BESTIOLE_SPEED 5.0
