#include "position.h"
#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/temp_allocator.h>
#include <foundation/math.inl>
#include <foundation/localizer.h>
#include <foundation/the_truth.h>
#include <foundation/error.h>
#include <foundation/camera.h>
#include <foundation/profiler.h>

#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/the_machinery_shared/component_interfaces/editor_ui_interface.h>
#include <plugins/render_utilities/primitive_drawer.h>

// APIs
static struct tm_logger_api *tm_logger_api;
static struct tm_entity_api *tm_entity_api;
static struct tm_temp_allocator_api *tm_temp_allocator_api;
static struct tm_transform_component_api *tm_transform_component_api;
static struct tm_the_truth_api* tm_the_truth_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_error_api* tm_error_api;
static struct tm_primitive_drawer_api* tm_primitive_drawer_api;
static struct tm_profiler_api* tm_profiler_api;
// -------------------------------------

typedef float f32;
typedef double f64;
#define ASSERT(cond) TM_FATAL_ASSERT_FORMAT(cond, tm_error_api->log, #cond)

#define TM_TT_TYPE__POSITION_TRACKING_COMPONENT "tm_position_tracking_component"
#define TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT TM_STATIC_HASH("tm_position_tracking_component", 0x1a03e3b9605ec002ULL)

enum {
	TM_TT_PROP__POSITION_TRACKING_COMPONENT__TYPE, // uint32_t
};

enum {
	TRACKING_TYPE_BOUFFE = 0,
	TRACKING_TYPE_BESTIOLE,
	_TRACKING_TYPE_COUNT,
};

static const char *tracking_type_names[] = {
	TM_LOCALIZE_LATER("Bouffe"),
	TM_LOCALIZE_LATER("Bestiole"),
};

typedef struct tm_position_tracking_component_t
{
	uint32_t type;
} tm_position_tracking_component_t;

enum {
	WORLD_WIDTH = 1000,
	WORLD_HEIGHT = 1000,
	
	CELL_CAPACITY = 4096,
	BLOCK_CAPACITY = 8192,
	BLOCK_ENTITY_CAPACITY = 64,
	DEPTH_MAX = 6
};

typedef struct EntityPositionPair
{
	uint32_t type;
	f32 x, y;
} EntityPositionPair;

typedef struct EntityBlock
{
	uint32_t count;
	EntityPositionPair list[BLOCK_ENTITY_CAPACITY];
	struct EntityBlock* next;
} EntityBlock;

typedef struct Cell
{
	struct Cell* children[4];
	EntityBlock* block;
} Cell;

typedef struct PositionTrackingManager
{
	tm_entity_context_o* ctx;
	tm_allocator_i allocator;

	EntityBlock blocks[BLOCK_CAPACITY];
	EntityBlock* freeBlock;
	Cell cells[CELL_CAPACITY];
	int cellCount;
} PositionTrackingManager;

static void PositionTrackingManager_Init(PositionTrackingManager* g);
static void PositionTrackingManager_Clear(PositionTrackingManager* g);
static void PositionTrackingManager_AddToCell(PositionTrackingManager* g, Cell* cell, const tm_rect_t rect, uint32_t type, tm_vec3_t entityPos, int depth);
static void PositionTrackingManager_AddToLeaf(PositionTrackingManager* g, Cell* cell, tm_rect_t rect, uint32_t type, tm_vec3_t entityPos, int depth);

static void PositionTrackingManager_Init(PositionTrackingManager* g)
{
	memset(g, 0x0, sizeof(*g));
	PositionTrackingManager_Clear(g);
}

static void PositionTrackingManager_Clear(PositionTrackingManager* g)
{
	memset(g->blocks, 0x0, sizeof(g->blocks));
	g->freeBlock = g->blocks;
	for(EntityBlock* b = g->blocks + 1; b != g->blocks + BLOCK_CAPACITY; ++b) {
		(b-1)->next = b;
	}
	memset(g->cells, 0x0, sizeof(g->cells));
	g->cellCount = 1;
}

static void PositionTrackingManager_AddToCell(PositionTrackingManager* g, Cell* cell, const tm_rect_t rect, uint32_t type, tm_vec3_t entityPos, int depth)
{
	ASSERT(!cell->children[0]);

	if(cell->block == 0x0) {
		ASSERT(g->freeBlock);
		cell->block = g->freeBlock;
		g->freeBlock = g->freeBlock->next;
		cell->block->next = 0x0;
		cell->block->count = 1;
		cell->block->list[0] = (EntityPositionPair){ type, entityPos.x, entityPos.z };
	}
	else {
		EntityBlock* block = cell->block;
		while(block->next) block = block->next;

		//ASSERT(entityPos.x >= rect.x && entityPos.x < rect.x + rect.w);
		//ASSERT(entityPos.z >= rect.y && entityPos.z < rect.y + rect.h);
		block->list[block->count++] = (EntityPositionPair){ type, entityPos.x, entityPos.z };

		if(depth < DEPTH_MAX) {
			// split
			if(block->count == BLOCK_ENTITY_CAPACITY) {
				TM_PROFILER_BEGIN_LOCAL_SCOPE(Split);

				ASSERT(g->cellCount+4 <= CELL_CAPACITY);
				const int firstID = g->cellCount;
				g->cellCount += 4;
				cell->children[0] = &g->cells[firstID];
				cell->children[1] = &g->cells[firstID+1];
				cell->children[2] = &g->cells[firstID+2];
				cell->children[3] = &g->cells[firstID+3];

				for(int i = 0; i < BLOCK_ENTITY_CAPACITY; i++) {
					const EntityPositionPair* pair = &block->list[i];
					int ix = (int)((pair->x - rect.x) / (rect.w/2.f));
					int iz = (int)((pair->y - rect.y) / (rect.h/2.f));
					ASSERT(ix == 0 || ix == 1);
					ASSERT(iz == 0 || iz == 1);
					PositionTrackingManager_AddToCell(
						g,
						cell->children[iz * 2 + ix],
						(tm_rect_t){
							.x = rect.x + rect.w/2.f * ix,
							.y = rect.y + rect.h/2.f * iz,
							.w = rect.w / 2.f,
							.h = rect.h / 2.f,
						},
						pair->type,
						(tm_vec3_t){ pair->x, 0, pair->y },
						depth + 1
					);
				}

				cell->block->next = g->freeBlock;
				g->freeBlock = cell->block;
				cell->block = 0x0;

				TM_PROFILER_END_LOCAL_SCOPE(Split);
			}
		}
		else {
			if(block->count == BLOCK_ENTITY_CAPACITY) {
				TM_PROFILER_BEGIN_LOCAL_SCOPE(AddBlock);

				ASSERT(g->freeBlock);
				block->next = g->freeBlock;
				g->freeBlock = g->freeBlock->next;
				block->next->next = 0x0;
				block->next->count = 0;

				TM_PROFILER_END_LOCAL_SCOPE(AddBlock);
			}
		}
	}
}

static void PositionTrackingManager_AddToLeaf(PositionTrackingManager* g, Cell* cell, tm_rect_t rect, uint32_t type, tm_vec3_t entityPos, int depth)
{
	const tm_vec3_t pos = entityPos;

	while(cell->children[0]) {
		int ix = (int)((pos.x - rect.x) / (rect.w/2.f));
		int iz = (int)((pos.z - rect.y) / (rect.h/2.f));
		ASSERT(ix == 0 || ix == 1);
		ASSERT(iz == 0 || iz == 1);

		rect = (tm_rect_t){
			.x = rect.x + rect.w/2.f * ix,
			.y = rect.y + rect.h/2.f * iz,
			.w = rect.w / 2.f,
			.h = rect.h / 2.f,
		};

		depth++;
		cell = cell->children[iz * 2 + ix];
	}

	PositionTrackingManager_AddToCell(g, cell, rect, type, entityPos, depth);
}

static void PositionTrackingManager_PlaceEntity(PositionTrackingManager* g, uint32_t type, tm_vec3_t entityPos)
{
	const f32 minWorldX = -WORLD_WIDTH/2;
	const f32 minWorldZ = -WORLD_HEIGHT/2;
	const f32 maxWorldX = WORLD_WIDTH/2;
	const f32 maxWorldZ = WORLD_HEIGHT/2;
	ASSERT(entityPos.x > minWorldX);
	ASSERT(entityPos.x < maxWorldX);
	ASSERT(entityPos.z > minWorldZ);
	ASSERT(entityPos.z < maxWorldZ);

	PositionTrackingManager_AddToLeaf(g, g->cells, (tm_rect_t){ minWorldX, minWorldZ, WORLD_WIDTH, WORLD_HEIGHT }, type, entityPos, 0);
}

static void DebugDrawCell(struct tm_primitive_drawer_buffer_t *pbuf, struct tm_primitive_drawer_buffer_t *vbuf, Cell* cell, tm_rect_t rect)
{
	const tm_vec3_t vertices[8] = {
		(tm_vec3_t) { rect.x, 0, rect.y },
		(tm_vec3_t) { rect.x + rect.w, 0, rect.y },
			
		(tm_vec3_t) { rect.x, 0, rect.y + rect.h },
		(tm_vec3_t) { rect.x + rect.w, 0, rect.y + rect.h },
			
		(tm_vec3_t) { rect.x, 0, rect.y },
		(tm_vec3_t) { rect.x, 0, rect.y + rect.h },
			
		(tm_vec3_t) { rect.x + rect.w, 0, rect.y },
		(tm_vec3_t) { rect.x + rect.w, 0, rect.y + rect.h },
	};
	
	tm_primitive_drawer_api->stroke_lines(pbuf, vbuf, tm_mat44_identity(), vertices, 4, (tm_color_srgb_t){ 255, 0, 0, 255 },
		1, TM_PRIMITIVE_DRAWER_DEPTH_TEST_DISABLED);

	if(cell->children[0]) {
		DebugDrawCell(pbuf, vbuf, cell->children[0], (tm_rect_t){ rect.x, rect.y, rect.w/2.f, rect.h/2.f });
		DebugDrawCell(pbuf, vbuf, cell->children[1], (tm_rect_t){ rect.x + rect.w/2.f, rect.y, rect.w/2.f, rect.h/2.f });
		DebugDrawCell(pbuf, vbuf, cell->children[2], (tm_rect_t){ rect.x, rect.y + rect.h/2.f, rect.w/2.f, rect.h/2.f });
		DebugDrawCell(pbuf, vbuf, cell->children[3], (tm_rect_t){ rect.x + rect.w/2.f, rect.y + rect.h/2.f, rect.w/2.f, rect.h/2.f });
	}
#if 1
	else {
		EntityBlock* block = cell->block;
		while(block) {
			for(uint32_t i = 0; i < block->count; i++) {
				const EntityPositionPair* pair = &block->list[i];

				const tm_vec3_t tri[3] = {
					(tm_vec3_t) { pair->x - 0.5f, 0, pair->y - 0.5f },
					(tm_vec3_t) { pair->x + 0.5f, 0, pair->y - 0.5f },
					(tm_vec3_t) { pair->x, 0, pair->y + 0.5f },
				};

				const uint32_t indices[3] = { 0, 1, 2 };

				const tm_color_srgb_t colors[_TRACKING_TYPE_COUNT] = {
					(tm_color_srgb_t){ 80, 255, 80, 255 },
					(tm_color_srgb_t){ 80, 80, 255, 255 },
				};

				tm_primitive_drawer_api->stroke_triangles(pbuf, vbuf, tm_mat44_identity(), tri, 3, indices, 3,
					colors[pair->type],
					1, TM_PRIMITIVE_DRAWER_DEPTH_TEST_DISABLED);
			}

			block = block->next;
		}
	}
#endif
}

static void dbgdraw_quad_tree(tm_component_manager_o *manager, tm_entity_t e[], const void *data[], uint32_t n,
	struct tm_primitive_drawer_buffer_t *pbuf, struct tm_primitive_drawer_buffer_t *vbuf,
	tm_allocator_i *allocator, const tm_camera_t *camera, tm_rect_t viewport)
{
	TM_PROFILER_BEGIN_FUNC_SCOPE();

	PositionTrackingManager* man = (PositionTrackingManager*)manager;
	tm_primitive_drawer_api->reserve(pbuf, 4 * 8192 * 1024, vbuf, 4 * 8192 * 1024, allocator);
	DebugDrawCell(pbuf, vbuf, man->cells, (tm_rect_t){ -WORLD_WIDTH/2, -WORLD_HEIGHT/2, WORLD_WIDTH, WORLD_HEIGHT });

	TM_PROFILER_END_FUNC_SCOPE();
}

static const char* component__category(void)
{
	return TM_LOCALIZE("Life");
}

static tm_ci_editor_ui_i* editor_aspect = &(tm_ci_editor_ui_i){
	.category = component__category
};

static void truth__create_types(struct tm_the_truth_o* tt)
{
	tm_the_truth_property_definition_t custom_component_properties[] = {
		[TM_TT_PROP__POSITION_TRACKING_COMPONENT__TYPE] = {
			.name = "type",
			.type = TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T,
			.editor = TM_THE_TRUTH__EDITOR__UINT32_T__ENUM,
			.enum_editor = (tm_the_truth_editor_enum_t) {
				.count = _TRACKING_TYPE_COUNT,
				.names = tracking_type_names
			}
		}
	};
	
	const tm_tt_type_t custom_component_type = tm_the_truth_api->create_object_type(tt,
		TM_TT_TYPE__POSITION_TRACKING_COMPONENT,
		custom_component_properties,
		TM_ARRAY_COUNT(custom_component_properties));

	const tm_tt_id_t default_object = tm_the_truth_api->quick_create_object(tt,
		TM_TT_NO_UNDO_SCOPE,
		TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT,
		TM_TT_PROP__POSITION_TRACKING_COMPONENT__TYPE, TRACKING_TYPE_BOUFFE,
		-1);

	tm_the_truth_api->set_default_object(tt, custom_component_type, default_object);
	tm_the_truth_api->set_aspect(tt, custom_component_type, TM_CI_EDITOR_UI, editor_aspect);
}

static bool component__load_asset(tm_component_manager_o* man, tm_entity_t e, void* c_vp, const tm_the_truth_o* tt, tm_tt_id_t asset)
{
	tm_position_tracking_component_t* c = c_vp;
	const tm_the_truth_object_o* asset_r = tm_tt_read(tt, asset);
	c->type = tm_the_truth_api->get_uint32_t(tt, asset_r, TM_TT_PROP__POSITION_TRACKING_COMPONENT__TYPE);
	return true;
}

static void component__manager_destroy(tm_component_manager_o* m)
{
	TM_LOG("component__manager_destroy");

	PositionTrackingManager* manager = (PositionTrackingManager*)m;

	tm_entity_context_o* ctx = manager->ctx;
	tm_allocator_i allocator = manager->allocator;
	tm_free(&allocator, manager, sizeof(*manager));
	tm_entity_api->destroy_child_allocator(ctx, &allocator);
}

static void component__create(struct tm_entity_context_o* ctx)
{
	TM_LOG("component__create");

	tm_allocator_i allocator;
	tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__POSITION_TRACKING_COMPONENT, &allocator);
	PositionTrackingManager* manager = tm_alloc(&allocator, sizeof(PositionTrackingManager));
	TM_FATAL_ASSERT(manager, tm_error_api->log);

	PositionTrackingManager_Init(manager);
	manager->ctx = ctx;
	manager->allocator = allocator;

	tm_component_i component = {
		.name = TM_TT_TYPE__POSITION_TRACKING_COMPONENT,
		.bytes = sizeof(struct tm_position_tracking_component_t),
		.load_asset = component__load_asset,
		.manager = (tm_component_manager_o*)manager,
		.destroy = component__manager_destroy,
		.debug_draw = dbgdraw_quad_tree
	};
	
	tm_entity_api->register_component(ctx, &component);
}

// Runs on (position_tracking_component, transform_component)
static void engine_build_quad_tree(tm_engine_o* inst, tm_engine_update_set_t* data)
{
	TM_PROFILER_BEGIN_FUNC_SCOPE();

	struct tm_entity_context_o* ctx = (struct tm_entity_context_o*)inst;
	PositionTrackingManager* man = (PositionTrackingManager*)tm_entity_api->component_manager_by_hash(ctx, TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT);

	PositionTrackingManager_Clear(man);

	for(tm_engine_update_array_t* a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
		tm_position_tracking_component_t* position_tracking = a->components[0];
		tm_transform_component_t* transform = a->components[1];
		
		for(uint32_t i = 0; i < a->n; ++i) {
			PositionTrackingManager_PlaceEntity(man, position_tracking[i].type, transform[i].world.pos);
		}
	}

	//TM_LOG("Cell count = %d", man->cellCount);
	TM_PROFILER_END_FUNC_SCOPE();
}

static bool engine_filter_build_quadtree(tm_engine_o* inst, const tm_component_type_t* components, uint32_t num_components, const tm_component_mask_t* mask)
{
	return tm_entity_mask_has_component(mask, components[0]) && tm_entity_mask_has_component(mask, components[1]);
}

static void component__register_engine(struct tm_entity_context_o* ctx)
{
	const tm_component_type_t position_tracking_comp = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT);
	const tm_component_type_t transform_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
	
	const tm_engine_i position_tracking_component_engine = {
		.ui_name = "Position tracking",
		.hash  = TM_STATIC_HASH("POSITION_TRACKING_COMPONENT", 0x47939b859ad13241ULL),
		.num_components = 2,
		.components = { position_tracking_comp, transform_component },
		.writes = { true, false },
		.update = engine_build_quad_tree,
		.filter = engine_filter_build_quadtree,
		.inst = (tm_engine_o*)ctx,
	};
	tm_entity_api->register_engine(ctx, &position_tracking_component_engine);
}

void load_position_c(struct tm_api_registry_api* reg, bool load)
{
	tm_logger_api = reg->get(TM_LOGGER_API_NAME);
	tm_entity_api = reg->get(TM_ENTITY_API_NAME);
	tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
	tm_transform_component_api = reg->get(TM_TRANSFORM_COMPONENT_API_NAME);
	tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
	tm_localizer_api = reg->get(TM_LOCALIZER_API_NAME);
	tm_error_api = reg->get(TM_ERROR_API_NAME);
	tm_primitive_drawer_api = reg->get(TM_PRIMITIVE_DRAWER_API_NAME);
	tm_profiler_api = reg->get(TM_PROFILER_API_NAME);
	
	tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, truth__create_types);
	tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__create);
	tm_add_or_remove_implementation(reg, load, TM_ENTITY_SIMULATION_REGISTER_ENGINES_INTERFACE_NAME, component__register_engine);

	TM_LOG("load_position_c");
}