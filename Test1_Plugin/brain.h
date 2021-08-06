#pragma once
#include <foundation/allocator.h>

#define TM_TT_TYPE__BRAIN_COMPONENT "tm_brain_component"
#define TM_TT_TYPE_HASH__BRAIN_COMPONENT TM_STATIC_HASH("tm_brain_component", 0x536202976f17722aULL)

typedef float f32;
typedef double f64;

enum
{
	NN_INPUT_COUNT = 2,
	NN_HIDDEN_COUNT = 4,
	NN_OUPUT_COUNT = 2,
};

typedef struct NeuralNet
{
	f32 w0[(NN_INPUT_COUNT + 1) * NN_HIDDEN_COUNT];
	f32 w1[(NN_HIDDEN_COUNT + 1) * NN_OUPUT_COUNT];
} NeuralNet;

typedef struct brain_manager
{
	struct tm_entity_context_o* ctx;
	struct tm_allocator_i allocator;
	
	NeuralNet nns[STARTING_BESTIOLE_COUNT];
	double fitness[STARTING_BESTIOLE_COUNT];
	int count;
} brain_manager;

typedef struct polar
{
	float angle;
	float dist;
} polar;

typedef struct tm_brain_component_t
{
	f32 energy;
	int bouffe_count;
	polar input_nearestFood;
	
	NeuralNet nn;
	float output_angle;
	uint32_t output_actions; // 1 bit == 1 action (move, chomp, multiply)
	
	tm_vec2_t _cachedPos;
	
	f64 fitness;
} tm_brain_component_t;

void NeuralNet_mix(const NeuralNet* parent1, const NeuralNet* parent2, NeuralNet* out);

#define START_ENERGY 100.0f
#define MUTATION_RESET_RATE 0.005f
#define MUTATION_RATE 0.05f
