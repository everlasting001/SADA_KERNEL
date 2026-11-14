#include "sada_kernel.h"

// Integer square root: 48 fractional input bits become 24 output bits.
distance_t mean_squared_distance(
    const distance_t squared_distances[CANDIDATE_COUNT]
) {
    distance_sum_t sum = 0;
    for (int neighbor = 0; neighbor < CANDIDATE_COUNT; ++neighbor) {
        sum += squared_distances[neighbor];
    }
    return distance_t(sum / CANDIDATE_COUNT);
}

density_t estimate_density(distance_t mean_distance) {
    const density_t epsilon = density_t(0.000001);
    density_t denominator = density_t(mean_distance) + epsilon;
    return density_t(density_t(1) / denominator);
}

void compute_raw_density(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    density_t raw_density[POINT_COUNT]
) {
    for (int point = 0; point < POINT_COUNT; ++point) {
#pragma HLS PIPELINE off
        distance_t mean_distance = mean_squared_distance(squared_distances[point]);
        raw_density[point] = estimate_density(mean_distance);
    }
}

void select_density_neighbors(
    point_index_t current_point,
    const density_t raw_density[POINT_COUNT],
    const point_index_t candidate_indices[CANDIDATE_COUNT],
    point_index_t reference_indices[DENSITY_REFERENCE_COUNT]
) {
    density_score_t top_scores[DENSITY_EXTRA_COUNT];
    point_index_t top_ids[DENSITY_EXTRA_COUNT];
    bool valid[DENSITY_EXTRA_COUNT];
    ap_uint<4> top_positions[DENSITY_EXTRA_COUNT];
#pragma HLS ARRAY_PARTITION variable=top_positions complete dim=1
#pragma HLS ARRAY_PARTITION variable=top_scores complete dim=1
#pragma HLS ARRAY_PARTITION variable=top_ids complete dim=1
#pragma HLS ARRAY_PARTITION variable=valid complete dim=1
    for (int slot = 0; slot < DENSITY_EXTRA_COUNT; ++slot) {
#pragma HLS UNROLL
        top_scores[slot] = 0;
        top_ids[slot] = 0;
        valid[slot] = false;
        top_positions[slot] = 0;
    }
    for (int neighbor = 0; neighbor < NEAREST_COUNT; ++neighbor) {
        reference_indices[neighbor] = candidate_indices[neighbor];
    }
    for (int neighbor = NEAREST_COUNT; neighbor < CANDIDATE_COUNT; ++neighbor) {
#pragma HLS PIPELINE II=1
        point_index_t candidate = candidate_indices[neighbor];
        density_difference_t difference =
            density_difference_t(raw_density[current_point]) -
            density_difference_t(raw_density[candidate]);
        density_score_t carry_score;
        if (difference < 0) carry_score = density_score_t(-difference);
        else carry_score = density_score_t(difference);
        point_index_t carry_id = candidate;
        ap_uint<4> carry_position = neighbor;
        bool carry_valid = true;
        for (int rank = 0; rank < DENSITY_EXTRA_COUNT; ++rank) {
#pragma HLS UNROLL
            // Strict comparison keeps earlier candidate IDs first on ties.
            if (carry_valid && (!valid[rank] || carry_score > top_scores[rank] ||
                (carry_score == top_scores[rank] && carry_position < top_positions[rank]))) {
                density_score_t previous_score = top_scores[rank];
                point_index_t previous_id = top_ids[rank];
                bool previous_valid = valid[rank];
                ap_uint<4> previous_position = top_positions[rank];
                top_scores[rank] = carry_score;
                top_ids[rank] = carry_id;
                valid[rank] = true;
                top_positions[rank] = carry_position;
                carry_score = previous_score;
                carry_id = previous_id;
                carry_valid = previous_valid;
                carry_position = previous_position;
            }
        }
    }
    for (int rank = 0; rank < DENSITY_EXTRA_COUNT; ++rank) {
        reference_indices[NEAREST_COUNT + rank] = top_ids[rank];
    }
}

void select_density_references(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    const point_index_t candidate_indices[POINT_COUNT][CANDIDATE_COUNT],
    point_index_t reference_indices[POINT_COUNT][DENSITY_REFERENCE_COUNT]
) {
    density_t raw_density[POINT_COUNT];
    compute_raw_density(squared_distances, raw_density);
    for (int point = 0; point < POINT_COUNT; ++point) {
#pragma HLS PIPELINE off
        select_density_neighbors(
            point_index_t(point), raw_density,
            candidate_indices[point], reference_indices[point]
        );
    }
}

void select_reference_points(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    const point_index_t candidate_indices[POINT_COUNT][CANDIDATE_COUNT],
    point_index_t reference_indices[POINT_COUNT][REFERENCE_COUNT]
) {
#if SADA_EXPERIMENT == 3
    select_density_references(squared_distances, candidate_indices, reference_indices); // 执行密度选择
#else
    // Shared logical input; these modes do not compute density.
    (void)squared_distances;
    for (int point = 0; point < POINT_COUNT; ++point) {
        for (int reference = 0; reference < REFERENCE_COUNT; ++reference) {
            reference_indices[point][reference] = candidate_indices[point][reference];
        }
    }   // 否则找到最近的8/16个点， 具体多少点由REFERENCE_COUNT决定 REFERENCE_COUNT = (SADA_EXPERIMENT == 2) ? 16 : 8;
#endif
}

attention_sum_t reduce_reference_spikes(
    spike_t query,
    const spike_t reference_keys[REFERENCE_COUNT]
) {
    attention_sum_t sum = 0;
    for (int reference = 0; reference < REFERENCE_COUNT; ++reference) {
#if SADA_EXPERIMENT == 0
        // Do not widen the subtraction to the accumulator width.
        spike_difference_t difference =
            spike_difference_t(query) - spike_difference_t(reference_keys[reference]);
        sum += attention_sum_t(difference); // 有符号减法
#else
        spike_t mismatch = query ^ reference_keys[reference];
        sum += attention_sum_t(mismatch);   // 异或门
#endif
    }
    return sum;
}

spike_t update_plif(
    attention_sum_t input_sum,
    membrane_t decay,
    membrane_t threshold,
    membrane_t &membrane
) {
    membrane_t charged =
        membrane + decay * (membrane_t(input_sum) - membrane);
    spike_t gate = (charged >= threshold);
    membrane = gate ? membrane_t(0) : charged;
    return gate;
}

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
) {
#pragma HLS ARRAY_PARTITION variable=reference_indices complete dim=2
    for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
#pragma HLS PIPELINE off
        const int KEY_COPIES = (REFERENCE_COUNT + 1) / 2;
        spike_t cached_keys[KEY_COPIES][POINT_COUNT];
#pragma HLS ARRAY_PARTITION variable=cached_keys complete dim=1
#pragma HLS BIND_STORAGE variable=cached_keys type=ram_2p impl=bram
        for (int load = 0; load < POINT_COUNT; ++load) {
#pragma HLS PIPELINE II=1
            spike_t key = key_spikes[channel][load];
            for (int copy = 0; copy < KEY_COPIES; ++copy) {
#pragma HLS UNROLL
                cached_keys[copy][load] = key;
            }
        }
        for (int point = 0; point < POINT_COUNT; ++point) {
#pragma HLS PIPELINE II=1
            spike_t reference_keys[REFERENCE_COUNT];
            for (int reference = 0; reference < REFERENCE_COUNT; ++reference) {
                point_index_t selected_point = reference_indices[point][reference];
                reference_keys[reference] = cached_keys[reference / 2][selected_point];
            }
            attention_sum_t sum = reduce_reference_spikes(
                query_spikes[channel][point], reference_keys
            );
            attention_sums[channel][point] = sum;
            spike_t gate = update_plif(sum, decay, threshold, membrane[channel][point]);
            gated_values[channel][point] = value_spikes[channel][point] & gate;
        }
    }
}

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
) {
    point_index_t reference_indices[POINT_COUNT][REFERENCE_COUNT];
#pragma HLS ARRAY_PARTITION variable=reference_indices complete dim=2
    select_reference_points(squared_distances, candidate_indices, reference_indices);
    apply_selected_attention(
        reference_indices, query_spikes, key_spikes, value_spikes,
        decay, threshold, membrane, gated_values, attention_sums
    );
}
