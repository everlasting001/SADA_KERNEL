#include "sada_kernel.h"

// Integer square root: 48 fractional input bits become 24 output bits.
static density_t square_root_variance(variance_t variance) {
    ap_uint<96> bits = variance.range(95, 0);
    ap_uint<48> root = 0;
    ap_uint<50> remainder = 0;
    for (int pair = 47; pair >= 0; --pair) {
        remainder <<= 2;
        remainder.range(1, 0) = bits.range(2 * pair + 1, 2 * pair);
        ap_uint<50> trial = ap_uint<50>(root);
        trial = (trial << 2) | ap_uint<50>(1);
        root <<= 1;
        if (remainder >= trial) {
            remainder -= trial;
            root[0] = 1;
        }
    }
    density_t result = 0;
    result.range(47, 0) = root;
    return result;
}

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
        distance_t mean_distance = mean_squared_distance(squared_distances[point]);
        raw_density[point] = estimate_density(mean_distance);
    }
}

density_t mean_density(const density_t raw_density[POINT_COUNT]) {
    density_sum_t sum = 0;
    for (int point = 0; point < POINT_COUNT; ++point) {
        sum += raw_density[point];
    }
    return density_t(sum / POINT_COUNT);
}

void density_variance_std(
    const density_t raw_density[POINT_COUNT],
    density_t mean,
    variance_t &variance,
    density_t &safe_std
) {
    variance_sum_t sum = 0;
    for (int point = 0; point < POINT_COUNT; ++point) {
        density_offset_t offset =
            density_offset_t(raw_density[point]) - density_offset_t(mean);
        variance_t squared_offset = variance_t(offset * offset);
        sum += squared_offset;
    }
    variance = variance_t(sum / (POINT_COUNT - 1));
    density_t standard_deviation = square_root_variance(variance);
    const density_t epsilon = density_t(0.000001);
    safe_std = (standard_deviation < epsilon) ? epsilon : standard_deviation;
}

void normalize_clip_density(
    const density_t raw_density[POINT_COUNT],
    density_t mean,
    density_t safe_std,
    normalized_density_t clipped_density[POINT_COUNT]
) {
    const density_t epsilon = density_t(0.000001);
    density_t denominator = (safe_std < epsilon) ? epsilon : safe_std;
    const normalization_t upper = 3;
    const normalization_t lower = -3;
    for (int point = 0; point < POINT_COUNT; ++point) {
        density_offset_t offset =
            density_offset_t(raw_density[point]) - density_offset_t(mean);
        normalization_t normalized = normalization_t(
            offset / density_offset_t(denominator)
        );
        if (normalized > upper) {
            clipped_density[point] = normalized_density_t(3);
        } else if (normalized < lower) {
            clipped_density[point] = normalized_density_t(-3);
        } else {
            clipped_density[point] = normalized_density_t(normalized);
        }
    }
}

void select_density_neighbors(
    point_index_t current_point,
    const normalized_density_t clipped_density[POINT_COUNT],
    const point_index_t candidate_indices[CANDIDATE_COUNT],
    point_index_t reference_indices[DENSITY_REFERENCE_COUNT]
) {
    density_score_t scores[CANDIDATE_COUNT];
    bool selected[CANDIDATE_COUNT];
    for (int neighbor = 0; neighbor < NEAREST_COUNT; ++neighbor) {
        reference_indices[neighbor] = candidate_indices[neighbor];
    }
    for (int neighbor = NEAREST_COUNT; neighbor < CANDIDATE_COUNT; ++neighbor) {
        point_index_t candidate = candidate_indices[neighbor];
        density_difference_t difference =
            density_difference_t(clipped_density[current_point]) -
            density_difference_t(clipped_density[candidate]);
        if (difference < 0) {
            scores[neighbor] = density_score_t(-difference);
        } else {
            scores[neighbor] = density_score_t(difference);
        }
        selected[neighbor] = false;
    }
    for (int slot = 0; slot < DENSITY_EXTRA_COUNT; ++slot) {
        int best_position = NEAREST_COUNT;
        density_score_t best_score = 0;
        bool found = false;
        for (int neighbor = NEAREST_COUNT; neighbor < CANDIDATE_COUNT; ++neighbor) {
            // Strict comparison preserves candidate order on ties.
            if (!selected[neighbor] && (!found || scores[neighbor] > best_score)) {
                best_position = neighbor;
                best_score = scores[neighbor];
                found = true;
            }
        }
        reference_indices[NEAREST_COUNT + slot] = candidate_indices[best_position];
        selected[best_position] = true;
    }
}

void select_density_references(
    const distance_t squared_distances[POINT_COUNT][CANDIDATE_COUNT],
    const point_index_t candidate_indices[POINT_COUNT][CANDIDATE_COUNT],
    point_index_t reference_indices[POINT_COUNT][DENSITY_REFERENCE_COUNT]
) {
    density_t raw_density[POINT_COUNT];
    normalized_density_t clipped_density[POINT_COUNT];
    compute_raw_density(squared_distances, raw_density);
    density_t mean = mean_density(raw_density);
    variance_t variance = 0;
    density_t safe_std = 0;
    density_variance_std(raw_density, mean, variance, safe_std);
    normalize_clip_density(raw_density, mean, safe_std, clipped_density);
    for (int point = 0; point < POINT_COUNT; ++point) {
        select_density_neighbors(
            point_index_t(point), clipped_density,
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
    for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            spike_t reference_keys[REFERENCE_COUNT];
            for (int reference = 0; reference < REFERENCE_COUNT; ++reference) {
                point_index_t selected_point = reference_indices[point][reference];
                reference_keys[reference] = key_spikes[channel][selected_point];
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
    select_reference_points(squared_distances, candidate_indices, reference_indices);
    apply_selected_attention(
        reference_indices, query_spikes, key_spikes, value_spikes,
        decay, threshold, membrane, gated_values, attention_sums
    );
}
