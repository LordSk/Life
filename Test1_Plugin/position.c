#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/temp_allocator.h>
#include <foundation/math.inl>
#include <foundation/localizer.h>
#include <foundation/the_truth.h>
#include <foundation/error.h>

#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/the_machinery_shared/component_interfaces/editor_ui_interface.h>

// APIs
static struct tm_logger_api *tm_logger_api;
static struct tm_entity_api *tm_entity_api;
static struct tm_temp_allocator_api *tm_temp_allocator_api;
static struct tm_transform_component_api *tm_transform_component_api;
static struct tm_the_truth_api* tm_the_truth_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_error_api* tm_error_api;
// -------------------------------------


#define TM_TT_TYPE__POSITION_TRACKING_COMPONENT "tm_position_tracking_component"
#define TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT TM_STATIC_HASH("tm_position_tracking_component", 0x1a03e3b9605ec002ULL)

struct tm_position_tracking_component_t
{
	float input_angle;
};

enum {
	CELL_WIDTH = 25,
	CELL_HEIGHT = 25,
	WORLD_WIDTH = 1000,
	WORLD_HEIGHT = 1000,
	CELL_COUNT_X = (WORLD_WIDTH-1)/CELL_WIDTH + 1, // round up
	CELL_COUNT_Y = (WORLD_HEIGHT-1)/CELL_HEIGHT + 1, // round up
	
	BLOCK_CAPACITY = 2048,
	BLOCK_CELL_CAPACITY = 64
};

typedef struct EntityBlock
{
	uint32_t count;
	tm_entity_t entities[BLOCK_CELL_CAPACITY];
	struct EntityBlock* next;
} EntityBlock;

typedef struct Cell
{
	EntityBlock* block;
} Cell;

typedef struct PositionTrackingManager
{
	tm_entity_context_o* ctx;
	tm_allocator_i allocator;

	EntityBlock blocks[BLOCK_CAPACITY];
	EntityBlock* free;
	Cell cells[CELL_COUNT_X * CELL_COUNT_Y];
} PositionTrackingManager;

static void PositionTrackingManager_init(PositionTrackingManager* g)
{
	memset(g, 0x0, sizeof(*g));
	
	g->free = g->blocks;
	for(EntityBlock* b = g->blocks + 1; b != g->blocks + BLOCK_CAPACITY; ++b) {
		(b-1)->next = b;
	}
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
	const tm_tt_type_t custom_component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__POSITION_TRACKING_COMPONENT, 0x0, 0);
	tm_the_truth_api->set_aspect(tt, custom_component_type, TM_CI_EDITOR_UI, editor_aspect);
}

static bool component__load_asset(tm_component_manager_o* man, tm_entity_t e, void* c_vp, const tm_the_truth_o* tt, tm_tt_id_t asset)
{
	//struct tm_position_tracking_component_t* c = c_vp;
	//const tm_the_truth_object_o* asset_r = tm_tt_read(tt, asset);
	//c->input_angle = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__POSITION_TRACKING_COMPONENT__INPUT_ANGLE);
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

	PositionTrackingManager_init(manager);
	manager->ctx = ctx;
	manager->allocator = allocator;

	tm_component_i component = {
		.name = TM_TT_TYPE__POSITION_TRACKING_COMPONENT,
		.bytes = sizeof(struct tm_position_tracking_component_t),
		.load_asset = component__load_asset,
		.manager = (tm_component_manager_o*)manager,
		.destroy = component__manager_destroy
	};
	
	tm_entity_api->register_component(ctx, &component);
}

// Runs on (custom_component, transform_component)
static void engine_update__custom_component(tm_engine_o* inst, tm_engine_update_set_t* data)
{
	TM_INIT_TEMP_ALLOCATOR(ta);
	
	tm_entity_t* mod_transform = 0;
	
	struct tm_entity_context_o* ctx = (struct tm_entity_context_o*)inst;
	
	double t = 0;
	double delta = 0;
	for(const tm_entity_blackboard_value_t* bb = data->blackboard_start; bb != data->blackboard_end; ++bb) {
		if(TM_STRHASH_EQUAL(bb->id, TM_ENTITY_BB__TIME)) t = bb->double_value;
		if(TM_STRHASH_EQUAL(bb->id, TM_ENTITY_BB__DELTA_TIME)) delta = bb->double_value;
	}
	
	for(tm_engine_update_array_t* a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
		struct tm_position_tracking_component_t* custom_component = a->components[0];
		tm_transform_component_t* transform = a->components[1];
		
		for(uint32_t i = 0; i < a->n; ++i) {
			transform[i].world.rot = tm_quaternion_from_rotation((tm_vec3_t){ 0, 1, 0 }, custom_component->input_angle - TM_PI/2.f);
			transform[i].world.pos.x += (float)((double)cosf(custom_component->input_angle) * delta * 1.0);
			transform[i].world.pos.z += (float)((double)sinf(-custom_component->input_angle) * delta * 1.0);
			++transform[i].version;
			tm_carray_temp_push(mod_transform, a->entities[i], ta);
		}
	}
	
	tm_entity_api->notify(ctx, data->engine->components[1], mod_transform, (uint32_t)tm_carray_size(mod_transform));
	
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static bool engine_filter__custom_component(tm_engine_o* inst, const tm_component_type_t* components, uint32_t num_components, const tm_component_mask_t* mask)
{
	return tm_entity_mask_has_component(mask, components[0]) && tm_entity_mask_has_component(mask, components[1]);
}

static void component__register_engine(struct tm_entity_context_o* ctx)
{
	const tm_component_type_t custom_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT);
	const tm_component_type_t transform_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
	
	const tm_engine_i custom_component_engine = {
		.ui_name = "Position tracking",
		.hash  = TM_STATIC_HASH("POSITION_TRACKING_COMPONENT", 0x47939b859ad13241ULL),
		.num_components = 2,
		.components = { custom_component, transform_component },
		.writes = { false, true },
		.update = engine_update__custom_component,
		.filter = engine_filter__custom_component,
		.inst = (tm_engine_o*)ctx,
	};
	tm_entity_api->register_engine(ctx, &custom_component_engine);
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
	
	tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, truth__create_types);
	tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__create);
	tm_add_or_remove_implementation(reg, load, TM_ENTITY_SIMULATION_REGISTER_ENGINES_INTERFACE_NAME, component__register_engine);

	TM_LOG("load_position_c");
}