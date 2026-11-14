#ifndef SADA_KERNEL_H
#define SADA_KERNEL_H

#include <ap_int.h>
#include <ap_fixed.h>

constexpr int N_POINTS = 256;
constexpr int N_CHANNELS = 96;
constexpr int TIME_STEPS = 1;
constexpr int N_CAND = 16; // 16 candidates
constexpr int N_REFS = 8; // 8 refs
constexpr int N_NEAR = 4;
constexpr int N_DENSITY = 4;


using spike_t = ap_uint<1>;
using index_t = ap_uint<8>;
using count_t = ap_uint<5>;

using fixed_t = ap_fixed<32, 16, AP_RND, AP_SAT>;

// 12 integer bits, 20 fractional bits.
using distance_t = ap_ufixed<32, 12, AP_RND, AP_SAT>;
// Four extra integer bits for summing 16 distances.
using distance_sum_t = ap_ufixed<36, 16, AP_RND, AP_SAT>;
// Initial density format: 24 integer bits, 24 fractional bits.
using density_t = ap_ufixed<48, 24, AP_RND, AP_SAT>;
// Eight extra integer bits for summing 256 densities.
using density_sum_t = ap_ufixed<56, 32, AP_RND, AP_SAT>;
// Signed difference with 24 fractional bits.
using density_diff_t = ap_fixed<49, 25, AP_RND, AP_SAT>;
// Preserve 48 fractional bits after squaring.
using variance_t = ap_ufixed<96, 48, AP_RND, AP_SAT>;
using variance_sum_t = ap_ufixed<104, 56, AP_RND, AP_SAT>;
// Signed output in [-3, 3], with 24 fractional bits.
using normalized_density_t =
    ap_fixed<27, 3, AP_RND, AP_SAT>;
// Wider intermediate value before explicit clipping.
using normalization_work_t =
    ap_fixed<72, 48, AP_RND, AP_SAT>;
// Difference range: [-6, 6].
using density_delta_t =
    ap_fixed<28, 4, AP_RND, AP_SAT>;
// Absolute difference range: [0, 6].
using density_score_t =
    ap_ufixed<27, 3, AP_RND, AP_SAT>;

// Average squared distances for one point.
distance_t mean_squared_distance(
    const distance_t distances[N_CAND]
);

// Estimate raw density from one average squared distance.
density_t estimate_density(distance_t mean_distance);

// Compute raw density for every point in one sample.
void compute_raw_density(
    const distance_t distances[N_POINTS][N_CAND],
    density_t rho[N_POINTS]
);

// Average raw densities across all points in one sample.
density_t mean_density(
    const density_t rho[N_POINTS]
);

// Compute sample variance and epsilon-clamped standard deviation.
void density_variance_std(
    const density_t rho[N_POINTS],
    density_t mean,
    variance_t &variance,
    density_t &std_safe
);

// Normalize and clip densities for one sample.
void normalize_clip_density(
    const density_t rho[N_POINTS],
    density_t mean,
    density_t std_safe,
    normalized_density_t rho_clip[N_POINTS]
);

// Select references for one current point.
void select_density_refs(
    index_t point_id,
    const normalized_density_t rho_clip[N_POINTS],
    const index_t candidate_idx[N_CAND],
    index_t active_idx[N_REFS]
);

// Select eight references for every point.
void select_all_density_refs(
    const normalized_density_t rho_clip[N_POINTS],
    const index_t candidate_idx[N_POINTS][N_CAND],
    index_t active_idx[N_POINTS][N_REFS]
);

// Full density-guided selection for one sample.
void density_selector(
    const distance_t distances[N_POINTS][N_CAND],
    const index_t candidate_idx[N_POINTS][N_CAND],
    index_t active_idx[N_POINTS][N_REFS]
);


spike_t plif_step(
    fixed_t input,
    fixed_t lambda,
    fixed_t threshold,
    fixed_t &membrane
);  // plif neutrons

spike_t attention_unit(
    spike_t q,
    const spike_t k_refs[N_REFS],
    spike_t v,
    fixed_t lambda,
    fixed_t threshold,
    fixed_t &membrane,
    count_t &mismatch_count
);

spike_t attention_point(
    spike_t q,
    const spike_t k_all[N_POINTS],
    const index_t candidate_idx[N_CAND],
    spike_t v,
    fixed_t lambda,
    fixed_t threshold,
    fixed_t &membrane,
    count_t &mismatch_count
);

void attention_channel(
    const spike_t q[N_POINTS],
    const spike_t k[N_POINTS],
    const spike_t v[N_POINTS],
    const index_t candidate_idx[N_POINTS][N_CAND],
    fixed_t lambda,
    fixed_t threshold,
    fixed_t membrane[N_POINTS],
    spike_t output[N_POINTS],
    count_t mismatch_count[N_POINTS]
);

void attention_frame(
    const spike_t q[N_CHANNELS][N_POINTS],
    const spike_t k[N_CHANNELS][N_POINTS],
    const spike_t v[N_CHANNELS][N_POINTS],
    const index_t candidate_idx[N_POINTS][N_CAND],
    fixed_t lambda,
    fixed_t threshold,
    fixed_t membrane[N_CHANNELS][N_POINTS],
    spike_t output[N_CHANNELS][N_POINTS],
    count_t mismatch_count[N_CHANNELS][N_POINTS]
);

// Complete density-selected attention for one sample and step.
void sada_attention_kernel(
    const distance_t distances[N_POINTS][N_CAND],
    const index_t candidate_idx[N_POINTS][N_CAND],
    const spike_t q[N_CHANNELS][N_POINTS],
    const spike_t k[N_CHANNELS][N_POINTS],
    const spike_t v[N_CHANNELS][N_POINTS],
    fixed_t lambda,
    fixed_t threshold,
    fixed_t membrane[N_CHANNELS][N_POINTS],
    spike_t output[N_CHANNELS][N_POINTS],
    count_t mismatch_count[N_CHANNELS][N_POINTS]
);

#endif
