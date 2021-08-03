#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/temp_allocator.h>
#include <foundation/math.inl>
#include <foundation/the_truth.h>
#include <foundation/the_truth_assets.h>
#include <foundation/core.h>
#include <foundation/random.h>

#include <plugins/simulate/simulate_entry.h>
#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/entity/scene_tree_component.h>
#include <plugins/render_utilities/render_component.h>

// APIs
static struct tm_logger_api *tm_logger_api;
static struct tm_entity_api *tm_entity_api;
static struct tm_temp_allocator_api *tm_temp_allocator_api;
static struct tm_transform_component_api *tm_transform_component_api;
static struct tm_the_truth_assets_api* tm_the_truth_assets_api;
static struct tm_random_api* tm_random_api;
// -------------------------------------

struct tm_simulate_state_o {
	// Store state here.
	
	tm_allocator_i *allocator;
	tm_entity_context_o *entity_ctx;
};

static tm_simulate_state_o *start(tm_simulate_start_args_t *args)
{
	tm_simulate_state_o *state = tm_alloc(args->allocator, sizeof(*state));
	*state = (tm_simulate_state_o) {
		.allocator = args->allocator,
		.entity_ctx = args->entity_ctx,
	};
	
	// Setup stuff at beginning of simulation.
	{
		tm_tt_id_t asset_bestiole = tm_the_truth_assets_api->asset_object_from_path(args->tt, args->asset_root, "bestiole.entity");
		//tm_tt_id_t asset_bestiole = tm_the_truth_assets_api->asset_object_from_path(args->tt, args->asset_root, "ant.entity");

		enum { count = 500 };
		tm_entity_t entities[count];
		tm_entity_api->batch_create_entity_from_asset(state->entity_ctx, asset_bestiole, entities, count);

		const tm_component_type_t ct_transform = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
		tm_transform_component_manager_o* man = (tm_transform_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, ct_transform);

		for(tm_entity_t* e = entities; e != (entities + count); ++e) {
			tm_transform_component_api->set_position(man, *e, (tm_vec3_t){
				.x = tm_random_float(-50, 50),
				.y = 0,
				.z = tm_random_float(-50, 50),
			});
		}
	}

	{
		tm_tt_id_t asset_bouffe = tm_the_truth_assets_api->asset_object_from_path(args->tt, args->asset_root, "bouffe.entity");
		
		enum { count = 10000 };
		tm_entity_t entities[count];
		tm_entity_api->batch_create_entity_from_asset(state->entity_ctx, asset_bouffe, entities, count);
		
		const tm_component_type_t ct_transform = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
		tm_transform_component_manager_o* man = (tm_transform_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, ct_transform);
		
		for(tm_entity_t* e = entities; e != (entities + count); ++e) {
			tm_transform_component_api->set_position(man, *e, (tm_vec3_t){
				.x = tm_random_float(-400, 400),
				.y = 0,
				.z = tm_random_float(-400, 400),
			});

			float s = tm_random_float(0.3f, 0.5f);
			tm_transform_component_api->set_scale(man, *e, (tm_vec3_t){ s, s, s });
		}
	}

	return state;
}

static void stop(tm_simulate_state_o *state)
{
	// Clean up when simulation ends.
	
	tm_allocator_i a = *state->allocator;
	tm_free(&a, state, sizeof(*state));
}

static void tick(tm_simulate_state_o *state, tm_simulate_frame_args_t *args)
{
	/*
	TM_INIT_TEMP_ALLOCATOR(ta);
	
	tm_entity_context_o *entity_ctx = state->entity_ctx;
	
	// Called once a frame.
	const tm_component_type_t ct_transform = tm_entity_api->lookup_component_type(entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
	const tm_component_type_t ct_scene_tree = tm_entity_api->lookup_component_type(entity_ctx, TM_TT_TYPE_HASH__SCENE_TREE_COMPONENT);
	tm_component_type_t components[2] = {
		ct_transform,
		ct_scene_tree,
	};
	
	tm_component_mask_t mask = tm_entity_api->create_component_mask(components, TM_ARRAY_COUNT(components));
	tm_entity_set_t* set = tm_entity_api->entities_matching(entity_ctx, &mask, ta);
	
	
	for(uint32_t a = 0; a < set->num_arrays; a++) {
		tm_entity_array_t* arr = &set->arrays[a];
		for(uint32_t i = 0; i < arr->n; i++) {
			tm_entity_t e = arr->entities[i];
			tm_transform_component_manager_o* man = (tm_transform_component_manager_o*)tm_entity_api->component_manager(entity_ctx, ct_transform);
			tm_vec3_t pos = tm_transform_component_api->get_position(man, e);
			tm_transform_component_api->set_position(man, e, (tm_vec3_t){
				.x = pos.x,
					.y = (float)sin(args->time + (double)i) * 1.f,
					.z = pos.z,
			});
			
			// TM_LOG("%d %f", i, (float)sin(args->time + (double)i) * 1.f);
		}
	}
	
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	*/
}

static tm_simulate_entry_i simulate_entry_i = {
	// Change this and re-run hash.exe if you wish to change the unique identifier
	.id = TM_STATIC_HASH("Life Simulate", 0x3b25b25c3a59fe27ULL),
	.display_name = "Life Simulate",
	.start = start,
	.stop = stop,
	.tick = tick,
};

void load_simulate_c(struct tm_api_registry_api* reg, bool load)
{
	tm_add_or_remove_implementation(reg, load, TM_SIMULATE_ENTRY_INTERFACE_NAME, &simulate_entry_i);
	
	tm_logger_api = reg->get(TM_LOGGER_API_NAME);
	tm_entity_api = reg->get(TM_ENTITY_API_NAME);
	tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
	tm_transform_component_api = reg->get(TM_TRANSFORM_COMPONENT_API_NAME);
	tm_the_truth_assets_api = reg->get(TM_THE_TRUTH_ASSETS_API_NAME);
	tm_random_api = reg->get(TM_RANDOM_API_NAME);
}