#include "position.h"
#include "world.h"
#include "brain.h"
#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/temp_allocator.h>
#include <foundation/math.inl>
#include <foundation/localizer.h>
#include <foundation/the_truth.h>
#include <foundation/random.h>
#include <foundation/camera.h>
#include <foundation/profiler.h>
#include <foundation/the_truth.h>
#include <foundation/the_truth_assets.h>
#include <foundation/error.h>

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
static struct tm_random_api* tm_random_api;
static struct tm_primitive_drawer_api* tm_primitive_drawer_api;
static struct tm_profiler_api* tm_profiler_api;
static struct tm_the_truth_assets_api* tm_the_truth_assets_api;
static struct tm_error_api* tm_error_api;
// -------------------------------------

enum
{
	ACTION_MOVE     = (1 << 0),
	ACTION_CHOMP    = (1 << 1),
	ACTION_MULTYPLY = (1 << 2),
};

void NeuralNet_mix(const NeuralNet* parent1, const NeuralNet* parent2, NeuralNet* out)
{
	for(int i = 0; i < TM_ARRAY_COUNT(parent1->w0); i++) {
		uint32_t r = tm_random_uint32_t(0, 2);
		if(r == 0) {
			out->w0[i] = parent1->w0[i];
		}
		else if(r == 1) {
			out->w0[i] = parent2->w0[i];
		}
		else {
			out->w0[i] = tm_lerp(parent1->w0[i], parent2->w0[i], tm_random_float(0, 1));
		}
	}

	for(int i = 0; i < TM_ARRAY_COUNT(parent1->w1); i++) {
		uint32_t r = tm_random_uint32_t(0, 2);
		if(r == 0) {
			out->w1[i] = parent1->w1[i];
		}
		else if(r == 1) {
			out->w1[i] = parent2->w1[i];
		}
		else {
			out->w1[i] = tm_lerp(parent1->w1[i], parent2->w1[i], tm_random_float(0, 1));
		}
	}
}

static void dbgdraw_brain_comp(tm_component_manager_o *manager, tm_entity_t e[], const void *data[], uint32_t n,
	struct tm_primitive_drawer_buffer_t *pbuf, struct tm_primitive_drawer_buffer_t *vbuf,
	tm_allocator_i *allocator, const tm_camera_t *camera, tm_rect_t viewport);

static const char* component__category(void)
{
	return TM_LOCALIZE("Life");
}

static tm_ci_editor_ui_i* editor_aspect = &(tm_ci_editor_ui_i){
	.category = component__category
};

static void truth__create_types(struct tm_the_truth_o* tt)
{
	const tm_tt_type_t custom_component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__BRAIN_COMPONENT, 0x0, 0);
	tm_the_truth_api->set_aspect(tt, custom_component_type, TM_CI_EDITOR_UI, editor_aspect);
}

static bool component__load_asset(tm_component_manager_o* man, tm_entity_t e, void* c_vp, const tm_the_truth_o* tt, tm_tt_id_t asset)
{
	//struct tm_brain_component_t* c = c_vp;
	//const tm_the_truth_object_o* asset_r = tm_tt_read(tt, asset);
	//c->input_angle = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__BRAIN_COMPONENT__INPUT_ANGLE);

	tm_brain_component_t* c = c_vp;
	c->input_nearestFood = (polar){0};

	/*
	for(int i = 0; i < TM_ARRAY_COUNT(c->nn.w0); i++) {
		c->nn.w0[i] = tm_random_float(-1.0f, 1.0f);
	}
	for(int i = 0; i < TM_ARRAY_COUNT(c->nn.w1); i++) {
		c->nn.w1[i] = tm_random_float(-1.0f, 1.0f);
	}
	*/

	c->energy = START_ENERGY;
	c->bouffe_count = 0;
	c->fitness = 0;
	return true;
}

static void component__manager_destroy(tm_component_manager_o* m)
{
	brain_manager* manager = (brain_manager*)m;
	
	tm_entity_context_o* ctx = manager->ctx;
	tm_allocator_i allocator = manager->allocator;
	tm_free(&allocator, manager, sizeof(*manager));
	tm_entity_api->destroy_child_allocator(ctx, &allocator);
}

static void component__create(struct tm_entity_context_o* ctx)
{
	tm_allocator_i allocator;
	tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__BRAIN_COMPONENT, &allocator);
	brain_manager* manager = tm_alloc(&allocator, sizeof(brain_manager));
	TM_FATAL_ASSERT(manager, tm_error_api->log);

	*manager = (brain_manager){0};
	manager->ctx = ctx;
	manager->allocator = allocator;

	tm_component_i component = {
		.name = TM_TT_TYPE__BRAIN_COMPONENT,
		.bytes = sizeof(struct tm_brain_component_t),
		.load_asset = component__load_asset,
		.debug_draw = dbgdraw_brain_comp,
		.manager = (tm_component_manager_o*)manager,
		.destroy = component__manager_destroy,
	};
	
	tm_entity_api->register_component(ctx, &component);
}

static bool engine_filter_has_all_components_1(tm_engine_o* inst, const tm_component_type_t* components, uint32_t num_components, const tm_component_mask_t* mask)
{
	return tm_entity_mask_has_component(mask, components[0]);
}

static bool engine_filter_has_both_components(tm_engine_o* inst, const tm_component_type_t* components, uint32_t num_components, const tm_component_mask_t* mask)
{
	return tm_entity_mask_has_component(mask, components[0]) && tm_entity_mask_has_component(mask, components[1]);
}

static bool engine_filter_has_all_components_3(tm_engine_o* inst, const tm_component_type_t* components, uint32_t num_components, const tm_component_mask_t* mask)
{
	return tm_entity_mask_has_component(mask, components[0]) &&
		   tm_entity_mask_has_component(mask, components[1]) &&
		   tm_entity_mask_has_component(mask, components[2]);
}

// Runs on (brain_component, transform_component)
static void engine_brain_apply_output(tm_engine_o* inst, tm_engine_update_set_t* data)
{
	TM_PROFILER_BEGIN_FUNC_SCOPE();

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
		const tm_brain_component_t* brain = a->components[0];
		tm_transform_component_t* transform = a->components[1];
		
		for(uint32_t i = 0; i < a->n; ++i) {
			if(brain[i].output_actions & ACTION_MOVE) {
				transform[i].world.rot = tm_quaternion_from_rotation((tm_vec3_t){ 0, 1, 0 }, brain->output_angle - TM_PI/2.f);
				transform[i].world.pos.x += (float)((double)cosf(brain->output_angle) * delta * BESTIOLE_SPEED);
				transform[i].world.pos.z += (float)((double)sinf(-brain->output_angle) * delta * BESTIOLE_SPEED);

				transform[i].world.pos.x = tm_max(transform[i].world.pos.x, -WORLD_WIDTH/2 + 10);
				transform[i].world.pos.x = tm_min(transform[i].world.pos.x, WORLD_WIDTH/2 - 10);

				transform[i].world.pos.z = tm_max(transform[i].world.pos.z, -WORLD_HEIGHT/2 + 10);
				transform[i].world.pos.z = tm_min(transform[i].world.pos.z, WORLD_HEIGHT/2 - 10);

				++transform[i].version;
				tm_carray_temp_push(mod_transform, a->entities[i], ta);
			}
		}
	}
	
	tm_entity_api->notify(ctx, data->engine->components[1], mod_transform, (uint32_t)tm_carray_size(mod_transform));
	
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	TM_PROFILER_END_FUNC_SCOPE();
}

// Runs on (brain_component, transform_component, position_tracking)
static void engine_brain_fetch_input(tm_engine_o* inst, tm_engine_update_set_t* data)
{
	TM_PROFILER_BEGIN_FUNC_SCOPE();

	TM_INIT_TEMP_ALLOCATOR(ta);

	tm_entity_t* mod_transform = 0;
	tm_entity_t* destroy_queue = 0;
	
	struct tm_entity_context_o* ctx = (struct tm_entity_context_o*)inst;
	PositionTrackingManager* man = (PositionTrackingManager*)tm_entity_api->component_manager_by_hash(ctx, TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT);
	
	for(tm_engine_update_array_t* a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
		const tm_entity_t* entities = a->entities;
		tm_brain_component_t* brains = a->components[0];
		const tm_transform_component_t* transforms = a->components[1];
		
		for(uint32_t i = 0; i < a->n; ++i) {
			tm_brain_component_t* brain = &brains[i];

			ClosestEntity closest = PositionTrackingManager_GetClosestEntity(man, transforms[i].world.pos, TRACKING_TYPE_BOUFFE, entities[i]);
			if(closest.e.u64) {
				f32 dist = tm_vec2_length(tm_vec2_sub(closest.pos, (tm_vec2_t){ transforms[i].world.pos.x, transforms[i].world.pos.z}));

				brain->input_nearestFood.angle = atan2f(closest.pos.y - transforms[i].world.pos.z, closest.pos.x - transforms[i].world.pos.x);
				brain->input_nearestFood.dist = dist;

				if(dist < 0.8f) {
					brain->energy += 20.0f;
					//if(brain->energy > MAX_ENERGY) brain->energy = MAX_ENERGY;
					brain->bouffe_count++;
					tm_carray_temp_push(destroy_queue, closest.e, ta);
					//TM_LOG("delete");
				}
			}
			else {
				brain->input_nearestFood = (polar){0};
			}

			brain->_cachedPos = (tm_vec2_t){ transforms[i].world.pos.x, transforms[i].world.pos.z };

			tm_carray_temp_push(mod_transform, entities[i], ta);
		}
	}
	
	tm_entity_api->notify(ctx, data->engine->components[0], mod_transform, (uint32_t)tm_carray_size(mod_transform));
	tm_entity_api->queue_destroy_entities(ctx, destroy_queue, (uint32_t)tm_carray_size(destroy_queue));

	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	TM_PROFILER_END_FUNC_SCOPE();
}

// Runs on (brain_component)
static void engine_brain_think(tm_engine_o* inst, tm_engine_update_set_t* data)
{
	TM_PROFILER_BEGIN_FUNC_SCOPE();

	TM_INIT_TEMP_ALLOCATOR(ta);
	
	tm_entity_t* mod_transform = 0;
	tm_entity_t* destroy_queue = 0;
	struct tm_entity_context_o* ctx = (struct tm_entity_context_o*)inst;

	const tm_component_type_t ct_transform = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
	tm_transform_component_manager_o* tfman = (tm_transform_component_manager_o*)tm_entity_api->component_manager(ctx, ct_transform);
	brain_manager* man = (brain_manager*)tm_entity_api->component_manager_by_hash(ctx, TM_TT_TYPE_HASH__BRAIN_COMPONENT);

	double delta = 0;
	for(const tm_entity_blackboard_value_t* bb = data->blackboard_start; bb != data->blackboard_end; ++bb) {
		if(TM_STRHASH_EQUAL(bb->id, TM_ENTITY_BB__DELTA_TIME)) delta = bb->double_value;
	}
	
	for(tm_engine_update_array_t* a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
		const tm_entity_t* entities = a->entities;
		tm_brain_component_t* brains = a->components[0];
		
		for(uint32_t i = 0; i < a->n; ++i) {
			tm_brain_component_t* brain = &brains[i];
			const f32* w0 = brain->nn.w0;
			const f32* w1 = brain->nn.w1;
			
			f32 inputs[NN_INPUT_COUNT + 1] = {
				brain->input_nearestFood.angle / TM_PI,
				brain->input_nearestFood.dist / 100.f,
				//brain->energy,
				1.0f, // bias
			};
			f32 hidden[NN_HIDDEN_COUNT + 1];
			f32 output[NN_OUPUT_COUNT];

			for(uint32_t h = 0; h < NN_HIDDEN_COUNT; h++) {
				f32 sum = 0;
				for(uint32_t in = 0; in < TM_ARRAY_COUNT(inputs); in++) {
					sum += w0[TM_ARRAY_COUNT(inputs) * h + in] * inputs[in];
				}
				hidden[h] = tanhf(sum);
			}
			hidden[NN_HIDDEN_COUNT] = 1.0f; // bias
			
			for(uint32_t o = 0; o < NN_OUPUT_COUNT; o++) {
				f32 sum = 0;
				for(uint32_t h = 0; h < TM_ARRAY_COUNT(hidden); h++) {
					sum += w1[TM_ARRAY_COUNT(hidden) * o + h] * hidden[h];
				}
				output[o] = tanhf(sum);
			}
			
			brain->output_angle = output[0] * TM_PI;
			brain->output_actions = 0 |
				ACTION_MOVE & (output[1] > 0.0f);
			
			tm_carray_temp_push(mod_transform, entities[i], ta);
		}
	}

	for(tm_engine_update_array_t* a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
		const tm_entity_t* entities = a->entities;
		tm_brain_component_t* brains = a->components[0];
		
		for(uint32_t i = 0; i < a->n; ++i) {
			tm_brain_component_t* brain = &brains[i];

			brain->fitness += (1.0/tm_min(0.001f, brain->input_nearestFood.dist) + brain->bouffe_count/10.0f) * delta;

			f64 multiplier = 1.0;
			if(brain->output_actions & ACTION_MOVE) multiplier = ENERY_DRAIN_MOVE_MULTIPLIER;

			brain->energy = (f32)(brain->energy - (delta * ENERY_DRAIN * multiplier));

			if(brain->energy < 0.f) {
				tm_carray_temp_push(destroy_queue, entities[i], ta);

				int id = man->count++;
				man->nns[id] = brain->nn;
				man->fitness[id] = brain->fitness;
				
				for(int b = 0; b < brain->bouffe_count; b++) {
					tm_entity_t bouffe[BOUFFE_COUNT];
					tm_entity_api->batch_create_entity_from_asset(ctx, *ptr_asset_bouffe, bouffe, brain->bouffe_count);
					
					for(tm_entity_t* e = bouffe; e != (bouffe + brain->bouffe_count); ++e) {
						tm_transform_component_api->set_position(tfman, *e, (tm_vec3_t){
							.x = tm_random_float(-WORLD_WIDTH/2 + 10, WORLD_WIDTH/2 - 10),
							.y = 0,
							.z = tm_random_float(-WORLD_HEIGHT/2 + 10, WORLD_HEIGHT/2 - 10),
						});
						
						float s = tm_random_float(0.3f, 0.5f);
						tm_transform_component_api->set_scale(tfman, *e, (tm_vec3_t){ s, s, s });
					}
				}
			}
		}
	}
	
	tm_entity_api->notify(ctx, data->engine->components[0], mod_transform, (uint32_t)tm_carray_size(mod_transform));
	tm_entity_api->queue_destroy_entities(ctx, destroy_queue, (uint32_t)tm_carray_size(destroy_queue));
	
	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

	TM_PROFILER_END_FUNC_SCOPE();
}

static void component__register_engines(struct tm_entity_context_o* ctx)
{
	const tm_component_type_t brain_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__BRAIN_COMPONENT);
	const tm_component_type_t transform_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
	const tm_component_type_t position_tracking_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__POSITION_TRACKING_COMPONENT);
	
	const tm_engine_i apply_output = {
		.ui_name = "Brain apply output",
		.hash  = TM_STATIC_HASH("BRAIN_APPLY_OUPUT", 0x47e26149c6174c27ULL),
		.num_components = 2,
		.components = { brain_component, transform_component },
		.writes = { false, true },
		.update = engine_brain_apply_output,
		//.filter = engine_filter_has_both_components,
		.inst = (tm_engine_o*)ctx,
	};
	tm_entity_api->register_engine(ctx, &apply_output);
	
	const tm_engine_i fetch_input = {
		.ui_name = "Brain fetch input",
		.hash  = TM_STATIC_HASH("BRAIN_FETCH_INPUT", 0x25d5c90db4640b7dULL),
		.num_components = 3,
		.components = { brain_component, transform_component, position_tracking_component },
		.writes = { true, false, false },
		.update = engine_brain_fetch_input,
		//.filter = engine_filter_has_all_components_3,
		.inst = (tm_engine_o*)ctx,
	};
	tm_entity_api->register_engine(ctx, &fetch_input);

	// FIXME: hack: we put transform_component as write because we might destroy the entity
	const tm_engine_i think = {
		.ui_name = "Brain think",
		.hash  = TM_STATIC_HASH("BRAIN_THINK", 0xb082b56c0b602869ULL),
		.num_components = 2,
		.components = { brain_component, transform_component },
		.writes = { true, true },
		.update = engine_brain_think,
		//.filter = engine_filter_has_all_components_1,
		.inst = (tm_engine_o*)ctx,
	};
	tm_entity_api->register_engine(ctx, &think);
}

static void dbgdraw_brain_comp(tm_component_manager_o *manager, tm_entity_t e[], const void *data[], uint32_t n,
	struct tm_primitive_drawer_buffer_t *pbuf, struct tm_primitive_drawer_buffer_t *vbuf,
	tm_allocator_i *allocator, const tm_camera_t *camera, tm_rect_t viewport)
{
	tm_primitive_drawer_api->reserve(pbuf, 4 * 8192 * 1024, vbuf, 4 * 8192 * 1024, allocator);

	for(uint32_t i = 0; i < n; i++) {
		const tm_brain_component_t* brain = (tm_brain_component_t*)data[i];

		const tm_vec3_t vertices[2] = {
			(tm_vec3_t) { brain->_cachedPos.x, 0, brain->_cachedPos.y },
			(tm_vec3_t) {
				brain->_cachedPos.x + cosf(brain->input_nearestFood.angle) * brain->input_nearestFood.dist,
				0,
				brain->_cachedPos.y + sinf(brain->input_nearestFood.angle) * brain->input_nearestFood.dist
			},
		};
		
		tm_primitive_drawer_api->stroke_lines(pbuf, vbuf, tm_mat44_identity(), vertices, 2, (tm_color_srgb_t){ 252, 36, 0, 255 },
			1, TM_PRIMITIVE_DRAWER_DEPTH_TEST_DISABLED);

		const tm_vec3_t healthbar[2] = {
			(tm_vec3_t) { brain->_cachedPos.x - 1.f, 0, brain->_cachedPos.y - 1.0f },
			(tm_vec3_t) {
				brain->_cachedPos.x - 1.0f + (brain->energy / START_ENERGY) * 2.0f,
				0,
				brain->_cachedPos.y - 1.0f
			},
		};
		
		tm_primitive_drawer_api->stroke_lines(pbuf, vbuf, tm_mat44_identity(), healthbar, 2, (tm_color_srgb_t){ 0, 255, 0, 255 },
			2, TM_PRIMITIVE_DRAWER_DEPTH_TEST_DISABLED);


		const tm_vec3_t tri[3] = {
			(tm_vec3_t) { brain->_cachedPos.x - 0.5f, 0, brain->_cachedPos.y - 0.5f },
			(tm_vec3_t) { brain->_cachedPos.x + 0.5f, 0, brain->_cachedPos.y - 0.5f },
			(tm_vec3_t) { brain->_cachedPos.x, 0, brain->_cachedPos.y + 0.5f },
		};
		
		const uint32_t indices[3] = { 0, 1, 2 };
		
		tm_primitive_drawer_api->stroke_triangles(pbuf, vbuf, tm_mat44_identity(), tri, 3, indices, 3,
			(tm_color_srgb_t){ 252, 36, 0, 255 },
			1, TM_PRIMITIVE_DRAWER_DEPTH_TEST_DISABLED);
	}
}

extern void load_simulate_c(struct tm_api_registry_api* reg, bool load);
extern void load_position_c(struct tm_api_registry_api* reg, bool load);

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
	tm_logger_api = reg->get(TM_LOGGER_API_NAME);
	tm_entity_api = reg->get(TM_ENTITY_API_NAME);
	tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
	tm_transform_component_api = reg->get(TM_TRANSFORM_COMPONENT_API_NAME);
	tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
	tm_localizer_api = reg->get(TM_LOCALIZER_API_NAME);
	tm_random_api = reg->get(TM_RANDOM_API_NAME);
	tm_primitive_drawer_api = reg->get(TM_PRIMITIVE_DRAWER_API_NAME);
	tm_profiler_api = reg->get(TM_PROFILER_API_NAME);
	tm_the_truth_assets_api = reg->get(TM_THE_TRUTH_ASSETS_API_NAME);
	tm_error_api = reg->get(TM_ERROR_API_NAME);

	tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, truth__create_types);
	tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__create);
	tm_add_or_remove_implementation(reg, load, TM_ENTITY_SIMULATION_REGISTER_ENGINES_INTERFACE_NAME, component__register_engines);

	load_simulate_c(reg, load);
	load_position_c(reg, load);
}