#pragma once
#include <plugins/entity/entity.h>

typedef struct PositionTrackingManager PositionTrackingManager;

tm_vec3_t PositionTrackingManager_GetClosestEntity(tm_vec3_t from, tm_entity_t e);