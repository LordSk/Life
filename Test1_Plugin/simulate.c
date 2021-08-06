#include "world.h"
#include "brain.h"
#include "position.h"
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
#include <foundation/error.h>
#include <foundation/profiler.h>

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
static struct tm_error_api* tm_error_api;
static struct tm_profiler_api* tm_profiler_api;
// -------------------------------------

#define ASSERT(cond) TM_FATAL_ASSERT_FORMAT(cond, tm_error_api->log, #cond)


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
	tm_tt_id_t asset_bestiole = tm_the_truth_assets_api->asset_object_from_path(args->tt, args->asset_root, "bestiole.entity");
	//tm_tt_id_t asset_bestiole = tm_the_truth_assets_api->asset_object_from_path(args->tt, args->asset_root, "ant.entity");
	*ptr_asset_bestiole = asset_bestiole;

	tm_tt_id_t asset_bouffe = tm_the_truth_assets_api->asset_object_from_path(args->tt, args->asset_root, "bouffe.entity");
	*ptr_asset_bouffe = asset_bouffe;

	{
		brain_manager* man = (brain_manager*)tm_entity_api->component_manager_by_hash(state->entity_ctx, TM_TT_TYPE_HASH__BRAIN_COMPONENT);
		for(int n = 0; n < STARTING_BESTIOLE_COUNT; n++) {
			NeuralNet* nn = &man->nns[n];

			for(int i = 0; i < TM_ARRAY_COUNT(nn->w0); i++) {
				nn->w0[i] = tm_random_float(-1.0f, 1.0f);
			}
			for(int i = 0; i < TM_ARRAY_COUNT(nn->w1); i++) {
				nn->w1[i] = tm_random_float(-1.0f, 1.0f);
			}
		}
		man->count = STARTING_BESTIOLE_COUNT;
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
	TM_PROFILER_BEGIN_FUNC_SCOPE();

	tm_entity_t found = tm_entity_api->find_entity_from_asset(state->entity_ctx, *ptr_asset_bestiole);

	if(!found.u64) {
		//tm_entity_api->destroy_all_entities(state->entity_ctx);
		TM_INIT_TEMP_ALLOCATOR(ta);

		const tm_component_type_t comps[] = {
			tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT),
			tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT),
		};

		const tm_component_mask_t comp_mask = tm_entity_api->create_component_mask(comps, TM_ARRAY_COUNT(comps));
		tm_entity_set_t* bouffe_set = tm_entity_api->entities_matching(state->entity_ctx, &comp_mask, ta);

		for(tm_entity_array_t* a = bouffe_set->arrays; a < bouffe_set->arrays + bouffe_set->num_arrays; ++a) {
			tm_entity_api->queue_destroy_entities(state->entity_ctx, a->entities, a->n);
		}
		
		TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

		const tm_component_type_t ct_transform = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
		tm_transform_component_manager_o* tfman = (tm_transform_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, ct_transform);

		{

			tm_entity_t entities[BOUFFE_COUNT];
			tm_entity_api->batch_create_entity_from_asset(state->entity_ctx, *ptr_asset_bouffe, entities, BOUFFE_COUNT);

			for(tm_entity_t* e = entities; e != (entities + BOUFFE_COUNT); ++e) {
				tm_transform_component_api->set_position(tfman, *e, (tm_vec3_t){
					.x = tm_random_float(-WORLD_WIDTH/2 + 10, WORLD_WIDTH/2 - 10),
					.y = 0,
					.z = tm_random_float(-WORLD_HEIGHT/2 + 10, WORLD_HEIGHT/2 - 10),
				});

				float s = tm_random_float(0.3f, 0.5f);
				tm_transform_component_api->set_scale(tfman, *e, (tm_vec3_t){ s, s, s });
			}
		}

		brain_manager* man = (brain_manager*)tm_entity_api->component_manager_by_hash(state->entity_ctx, TM_TT_TYPE_HASH__BRAIN_COMPONENT);
		ASSERT(man->count == STARTING_BESTIOLE_COUNT);
		man->count = 0;

		// sort
		bool sort = true;
		while(sort) {
			sort = false;

			for(int i = 1; i < STARTING_BESTIOLE_COUNT; i++) {
				if(man->fitness[i] > man->fitness[i-1]) {
					const f64 tmpf = man->fitness[i];
					man->fitness[i] = man->fitness[i-1];
					man->fitness[i-1] = tmpf;

					const NeuralNet tmpNN = man->nns[i];
					man->nns[i] = man->nns[i-1];
					man->nns[i-1] = tmpNN;

					sort = true;
				}
			}
		}

		// cull and breed
		uint32_t topCount = STARTING_BESTIOLE_COUNT/2;
		for(uint32_t i = topCount; i < STARTING_BESTIOLE_COUNT; i++) {
			NeuralNet_mix(
				&man->nns[tm_random_uint32_t(0, topCount-1)],
				&man->nns[tm_random_uint32_t(0, topCount-1)],
				&man->nns[i]);
		}

		// mutate
		uint32_t mutations = 0;
		for(int i = 0; i < STARTING_BESTIOLE_COUNT; i++) {
			NeuralNet* nn = &man->nns[i];

			if(tm_random_uint32_t(0, STARTING_BESTIOLE_COUNT-i) < STARTING_BESTIOLE_COUNT/10) {
				for(int w = 0; w < TM_ARRAY_COUNT(nn->w0); w++) {
					const f32 f = tm_random_float(0, 1.0f);
					if(f < MUTATION_RESET_RATE) { nn->w0[i] = tm_random_float(-1.0f, 1.0f); mutations++; }
					else if(f < MUTATION_RATE) { nn->w0[i] += tm_random_float(-0.1f, 0.1f); mutations++; }
				}

				for(int w = 0; w < TM_ARRAY_COUNT(nn->w1); w++) {
					const f32 f = tm_random_float(0, 1.0f);
					if(f < MUTATION_RESET_RATE) { nn->w1[i] = tm_random_float(-1.0f, 1.0f); mutations++; }
					else if(f < MUTATION_RATE) { nn->w1[i] += tm_random_float(-0.1f, 0.1f); mutations++; }
				}
			}
		}

		f64 sum_fitness = 0.0;
		f64 best_fitness = man->fitness[0];
		for(int i = 0; i < STARTING_BESTIOLE_COUNT; i++) {
			sum_fitness += man->fitness[i];
		}
		TM_LOG("Fitness(avg=%f best=%f) mutations=%d", sum_fitness/STARTING_BESTIOLE_COUNT, best_fitness, mutations);

		{
			tm_entity_t entities[STARTING_BESTIOLE_COUNT];
			tm_entity_api->batch_create_entity_from_asset(state->entity_ctx, *ptr_asset_bestiole, entities, STARTING_BESTIOLE_COUNT);

			for(tm_entity_t* e = entities; e != (entities + STARTING_BESTIOLE_COUNT); ++e) {
			/*tm_transform_component_api->set_position(tfman, *e, (tm_vec3_t){
				.x = tm_random_float(-WORLD_WIDTH/2 + 10, WORLD_WIDTH/2 - 10),
				.y = 0,
				.z = tm_random_float(-WORLD_HEIGHT/2 + 10, WORLD_HEIGHT/2 - 10),
			});*/
				tm_transform_component_api->set_position(tfman, *e, (tm_vec3_t){
					.x = 0,
					.y = 0,
					.z = 0,
				});
			}

			for(int i = 0; i < STARTING_BESTIOLE_COUNT; i++) {
				tm_brain_component_t* brain = tm_entity_api->get_component_by_hash(state->entity_ctx, entities[i], TM_TT_TYPE_HASH__BRAIN_COMPONENT);
				brain->nn = man->nns[i];
			}
		}
	}

	TM_PROFILER_END_FUNC_SCOPE();
}

static tm_simulate_entry_i simulate_entry_i = {
	// Change this and re-run hash.exe if you wish to change the unique identifier
	.id = TM_STATIC_HASH("Life Simulate", 0x3b25b25c3a59fe27ULL),
	.display_name = "Life Simulate",
	.start = start,
	.stop = stop,
	.tick = tick,
};

tm_tt_id_t* ptr_asset_bouffe;
tm_tt_id_t* ptr_asset_bestiole;

void load_simulate_c(struct tm_api_registry_api* reg, bool load)
{
	tm_add_or_remove_implementation(reg, load, TM_SIMULATE_ENTRY_INTERFACE_NAME, &simulate_entry_i);
	
	tm_logger_api = reg->get(TM_LOGGER_API_NAME);
	tm_entity_api = reg->get(TM_ENTITY_API_NAME);
	tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
	tm_transform_component_api = reg->get(TM_TRANSFORM_COMPONENT_API_NAME);
	tm_the_truth_assets_api = reg->get(TM_THE_TRUTH_ASSETS_API_NAME);
	tm_random_api = reg->get(TM_RANDOM_API_NAME);
	tm_error_api = reg->get(TM_ERROR_API_NAME);
	tm_profiler_api = reg->get(TM_PROFILER_API_NAME);

	ptr_asset_bouffe = (tm_tt_id_t *)reg->static_variable(TM_STATIC_HASH("g_asset_bouffe", 0x39941fc1b746a47ULL),
		sizeof(tm_tt_id_t), __FILE__, __LINE__);
	ptr_asset_bestiole = (tm_tt_id_t *)reg->static_variable(TM_STATIC_HASH("g_asset_bestiole", 0xafe47869393f0516ULL),
		sizeof(tm_tt_id_t), __FILE__, __LINE__);
}