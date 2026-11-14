#ifndef SADA_RAW_DENSITY16_KERNEL_H
#define SADA_RAW_DENSITY16_KERNEL_H

#include <ap_fixed.h>
#include <ap_int.h>

// Use the same -DSADA_EXPERIMENT value for design and test sources.
// 0: subtract + nearest8; 1: XOR + nearest8;
// 2: XOR + all16;        3: XOR + density-selected8.
#ifndef SADA_EXPERIMENT
#define SADA_EXPERIMENT 3
#endif

#if SADA_EXPERIMENT < 0 || SADA_EXPERIMENT > 3
#error "SADA_EXPERIMENT must be 0, 1, 2, or 3."
#endif

enum class Experiment {
    SubtractNearest8 = 0,
    XorNearest8 = 1,
    XorAll16 = 2,
    XorDensity8 = 3
};

constexpr Experiment EXPERIMENT = static_cast<Experiment>(SADA_EXPERIMENT);
constexpr int POINT_COUNT = 512;
constexpr int CHANNEL_COUNT = 96;
constexpr int CANDIDATE_COUNT = 16;
constexpr int NEAREST_COUNT = 4;
constexpr int DENSITY_EXTRA_COUNT = 4;
constexpr int DENSITY_REFERENCE_COUNT = NEAREST_COUNT + DENSITY_EXTRA_COUNT;
constexpr int REFERENCE_COUNT = (SADA_EXPERIMENT == 2) ? 16 : 8;

static_assert(POINT_COUNT > CANDIDATE_COUNT, "Candidates exclude self.");
static_assert(POINT_COUNT <= 512, "Point IDs use nine bits.");
static_assert(DENSITY_REFERENCE_COUNT == 8, "Density selection keeps 4+4 points.");
static_assert(REFERENCE_COUNT <= CANDIDATE_COUNT, "Invalid reference count.");

using spike_t = ap_uint<1>;
using point_index_t = ap_uint<9>;

// The subtraction itself uses only two-bit signed operands.
using spike_difference_t = ap_int<2>;
// Common accumulator/debug format covers all four modes: [-8, 16].
using attention_sum_t = ap_int<6>;

using membrane_t = ap_fixed<32, 16, AP_RND, AP_SAT>;
using distance_t = ap_ufixed<32, 12, AP_RND, AP_SAT>;
using distance_sum_t = ap_ufixed<36, 16, AP_RND, AP_SAT>;
using density_t = ap_ufixed<48, 24, AP_RND, AP_SAT>;
// Raw densities retain their full 24 integer and 24 fractional bits.
using density_difference_t = ap_fixed<49, 25, AP_RND, AP_SAT>;
using density_score_t = ap_ufixed<48, 24, AP_RND, AP_SAT>;

// Raw density estimation; no statistics, normalization or clipping.
distance_t mean_squared_distance(const distance_t squared_distances[CANDIDATE_COUNT]);
density_t estimate_density(distance_t mean_distance);
void compute_raw_density(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    density_t raw_density[POINT_COUNT]
);
// Density selection always returns eight IDs, independently of the build mode.
void select_density_neighbors(
    point_index_t current_point,
    const density_t raw_density[POINT_COUNT],
    const point_index_t candidate_indices[CANDIDATE_COUNT],
    point_index_t reference_indices[DENSITY_REFERENCE_COUNT]
);
void select_density_references(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    const point_index_t candidate_indices[POINT_COUNT][CANDIDATE_COUNT],
    point_index_t reference_indices[POINT_COUNT][DENSITY_REFERENCE_COUNT]
);

// Compile-time policy: nearest8, all16, or density-selected8.
void select_reference_points(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    const point_index_t candidate_indices[POINT_COUNT][CANDIDATE_COUNT],
    point_index_t reference_indices[POINT_COUNT][REFERENCE_COUNT]
);
attention_sum_t reduce_reference_spikes(
    spike_t query,
    const spike_t reference_keys[REFERENCE_COUNT]
);

// decay_input=True, reset=0; each position has its own caller-owned state.
spike_t update_plif(
    attention_sum_t input_sum,
    membrane_t decay,
    membrane_t threshold,
    membrane_t &membrane
);
void apply_selected_attention(
    const point_index_t reference_indices[POINT_COUNT][REFERENCE_COUNT],
    const spike_t query_spikes[CHANNEL_COUNT][POINT_COUNT],
    const spike_t key_spikes[CHANNEL_COUNT][POINT_COUNT],
    const spike_t value_spikes[CHANNEL_COUNT][POINT_COUNT],
    membrane_t decay,
    membrane_t threshold,
    membrane_t membrane[CHANNEL_COUNT][POINT_COUNT],
    spike_t gated_values[CHANNEL_COUNT][POINT_COUNT],
    attention_sum_t attention_sums[CHANNEL_COUNT][POINT_COUNT]
);

// One sample and one time step per call. Keep state between sequence steps.
// Candidates are unique, sorted by squared distance, and exclude self.
void sada_attention_kernel(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    const point_index_t candidate_indices[POINT_COUNT][CANDIDATE_COUNT],
    const spike_t query_spikes[CHANNEL_COUNT][POINT_COUNT],
    const spike_t key_spikes[CHANNEL_COUNT][POINT_COUNT],
    const spike_t value_spikes[CHANNEL_COUNT][POINT_COUNT],
    membrane_t decay,
    membrane_t threshold,
    membrane_t membrane[CHANNEL_COUNT][POINT_COUNT],
    spike_t gated_values[CHANNEL_COUNT][POINT_COUNT],
    attention_sum_t attention_sums[CHANNEL_COUNT][POINT_COUNT]
);

#endif
