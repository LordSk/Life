#pragma once
#include <plugins/entity/entity.h>

#define TM_TT_TYPE__POSITION_TRACKING_COMPONENT "tm_position_tracking_component"
#define TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT TM_STATIC_HASH("tm_position_tracking_component", 0x1a03e3b9605ec002ULL)

enum {
	TRACKING_TYPE_BOUFFE = 0,
	TRACKING_TYPE_BESTIOLE,
	_TRACKING_TYPE_COUNT,
};

typedef struct PositionTrackingManager PositionTrackingManager;

typedef struct ClosestEntity
{
	tm_entity_t e;
	tm_vec2_t pos;
} ClosestEntity;

ClosestEntity PositionTrackingManager_GetClosestEntity(PositionTrackingManager* g, tm_vec3_t from, uint32_t type, tm_entity_t notthis);