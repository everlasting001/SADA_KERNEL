#include "sada_kernel.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

static distance_t distances[POINT_COUNT][CANDIDATE_COUNT];
static point_index_t candidates[POINT_COUNT][CANDIDATE_COUNT];
static point_index_t expected_ids[POINT_COUNT][REFERENCE_COUNT];
static point_index_t actual_ids[POINT_COUNT][REFERENCE_COUNT];
static spike_t queries[CHANNEL_COUNT][POINT_COUNT];
static spike_t keys[CHANNEL_COUNT][POINT_COUNT];
static spike_t values[CHANNEL_COUNT][POINT_COUNT];
static membrane_t membrane[CHANNEL_COUNT][POINT_COUNT];
static double expected_membrane[CHANNEL_COUNT][POINT_COUNT];
static spike_t output[CHANNEL_COUNT][POINT_COUNT];
static attention_sum_t sums[CHANNEL_COUNT][POINT_COUNT];

static void prepare_inputs() {
    // Synthetic interface vectors, not a geometric 16NN data set.
    for (int point = 0; point < POINT_COUNT; ++point) {
        double base = (point % 4 + 1) * 0.125;
        for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) {
            distances[point][slot] = distance_t(slot < 8 ? base : 3.0 * base);
            candidates[point][slot] = (point + slot + 1) % POINT_COUNT;
        }
    }
    for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            queries[channel][point] = (channel + point) % 2;
            keys[channel][point] = ((point + 3 * channel) % 7 == 0);
            values[channel][point] = ((point + channel) % 3 != 0);
            double initial = ((point + channel) % 9 - 4) * 0.125;
            membrane[channel][point] = membrane_t(initial);
            expected_membrane[channel][point] = initial;
        }
    }
}

static void build_reference_ids() {
    if (EXPERIMENT != Experiment::XorDensity8) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
                expected_ids[point][slot] = candidates[point][slot];
            }
        }
        return;
    }

    // Independent double-precision density model using the quantized inputs.
    double raw[POINT_COUNT];
    double clipped[POINT_COUNT];
    const double epsilon = density_t(0.000001).to_double();
    double mean = 0;
    for (int point = 0; point < POINT_COUNT; ++point) {
        double sum = 0;
        for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) {
            sum += distances[point][slot].to_double();
        }
        raw[point] = 1.0 / (sum / CANDIDATE_COUNT + epsilon);
        mean += raw[point];
    }
    mean /= POINT_COUNT;
    double variance = 0;
    for (int point = 0; point < POINT_COUNT; ++point) {
        double offset = raw[point] - mean;
        variance += offset * offset;
    }
    variance /= POINT_COUNT - 1;
    double safe_std = std::max(std::sqrt(variance), epsilon);
    for (int point = 0; point < POINT_COUNT; ++point) {
        clipped[point] = std::max(-3.0, std::min(3.0, (raw[point] - mean) / safe_std));
    }

    for (int point = 0; point < POINT_COUNT; ++point) {
        for (int slot = 0; slot < NEAREST_COUNT; ++slot) {
            expected_ids[point][slot] = candidates[point][slot];
        }
        int positions[CANDIDATE_COUNT - NEAREST_COUNT];
        for (int slot = 0; slot < CANDIDATE_COUNT - NEAREST_COUNT; ++slot) {
            positions[slot] = slot + NEAREST_COUNT;
        }
        std::stable_sort(positions, positions + CANDIDATE_COUNT - NEAREST_COUNT,
            [&](int left, int right) {
                int a = candidates[point][left].to_uint();
                int b = candidates[point][right].to_uint();
                return std::fabs(clipped[point] - clipped[a]) >
                       std::fabs(clipped[point] - clipped[b]);
            });
        for (int slot = 0; slot < DENSITY_EXTRA_COUNT; ++slot) {
            expected_ids[point][NEAREST_COUNT + slot] = candidates[point][positions[slot]];
        }
    }
}

static void check_index_and_accumulator_ranges() {
    point_index_t last_point = POINT_COUNT - 1;
    assert(last_point.to_uint() == unsigned(POINT_COUNT - 1));

    // The mean is small enough for density_t; the sum needs extra bits.
    density_t raw[POINT_COUNT];
    const density_t large_density = density_t(16777215);
    for (int point = 0; point < POINT_COUNT; ++point) raw[point] = large_density;
    assert(mean_density(raw) == large_density);
}

static void check_reduction_and_state() {
    spike_t reference_keys[REFERENCE_COUNT];
    for (int query = 0; query <= 1; ++query) {
        for (int pattern = 0; pattern < 3; ++pattern) {
            int expected = 0;
            for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
                int key = pattern == 2 ? slot % 2 : pattern;
                reference_keys[slot] = key;
                expected += EXPERIMENT == Experiment::SubtractNearest8
                    ? query - key : (query != key);
            }
            assert(reduce_reference_spikes(spike_t(query), reference_keys).to_int() == expected);
        }
    }

    // Negative input must remain signed through the membrane update.
    membrane_t state = 0;
    assert(update_plif(attention_sum_t(-8), membrane_t(0.5), membrane_t(1), state) == 0);
    assert(state == membrane_t(-4));
    assert(update_plif(attention_sum_t(0), membrane_t(0.5), membrane_t(1), state) == 0);
    assert(state == membrane_t(-2));
    assert(update_plif(attention_sum_t(8), membrane_t(0.5), membrane_t(1), state) == 1);
    assert(state == membrane_t(0));
}

static void check_selection() {
    select_reference_points(distances, candidates, actual_ids);
    for (int point = 0; point < POINT_COUNT; ++point) {
        for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
            assert(actual_ids[point][slot] == expected_ids[point][slot]);
            assert(actual_ids[point][slot] != point);
            for (int prior = 0; prior < slot; ++prior) {
                assert(actual_ids[point][slot] != actual_ids[point][prior]);
            }
        }
    }

    // Equal densities exercise the std floor and deterministic tie rule.
    // Nearest modes must ignore distances entirely.
    for (int point = 0; point < POINT_COUNT; ++point) {
        for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) {
            distances[point][slot] = 0;
        }
    }
    select_reference_points(distances, candidates, actual_ids);
    for (int point = 0; point < POINT_COUNT; ++point) {
        for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
            assert(actual_ids[point][slot] == candidates[point][slot]);
        }
    }
    prepare_inputs();
}

static void check_density_helpers() {
    normalized_density_t density[POINT_COUNT];
    point_index_t candidate_row[CANDIDATE_COUNT];
    point_index_t selected[DENSITY_REFERENCE_COUNT];
    for (int point = 0; point < POINT_COUNT; ++point) density[point] = 3;
    for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) candidate_row[slot] = 16 + slot;
    for (int slot = 0; slot < NEAREST_COUNT; ++slot) density[16 + slot] = -3;
    density[26] = -3;
    density[27] = -2;
    density[28] = -1;
    density[29] = 0;
    select_density_neighbors(point_index_t(0), density, candidate_row, selected);
    const int expected[DENSITY_REFERENCE_COUNT] = {16, 17, 18, 19, 26, 27, 28, 29};
    for (int slot = 0; slot < DENSITY_REFERENCE_COUNT; ++slot) {
        assert(selected[slot] == expected[slot]);
    }

    // Small variance must survive large raw densities.
    density_t raw[POINT_COUNT];
    for (int point = 0; point < POINT_COUNT; ++point) {
        raw[point] = density_t(1000000.0 + (point < POINT_COUNT / 2 ? 0.0 : 1.0 / 65536.0));
    }
    variance_t variance = 0;
    density_t safe_std = 0;
    density_variance_std(raw, mean_density(raw), variance, safe_std);
    double delta = 1.0 / 65536.0;
    double reference_variance = delta * delta * POINT_COUNT / (4.0 * (POINT_COUNT - 1));
    assert(std::fabs(variance.to_double() - reference_variance) <= 2.0 * std::ldexp(1.0, -48));
    assert(std::fabs(safe_std.to_double() - std::sqrt(reference_variance)) <= 2.0 * std::ldexp(1.0, -24));
}

static void check_complete_kernel() {
    for (int step = 0; step < 2; ++step) {
        sada_attention_kernel(
            distances, candidates, queries, keys, values,
            membrane_t(0.5), membrane_t(1), membrane, output, sums
        );
        for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
            for (int point = 0; point < POINT_COUNT; ++point) {
                int query = queries[channel][point].to_uint();
                int expected_sum = 0;
                for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
                    int reference = expected_ids[point][slot].to_uint();
                    int key = keys[channel][reference].to_uint();
                    expected_sum += EXPERIMENT == Experiment::SubtractNearest8
                        ? query - key : (query != key);
                }
                double previous = expected_membrane[channel][point];
                double charged = previous + 0.5 * (expected_sum - previous);
                bool gate = charged >= 1;
                double next = gate ? 0 : charged;
                int expected_output = gate && values[channel][point].to_uint();
                expected_membrane[channel][point] = next;

                if (sums[channel][point].to_int() != expected_sum ||
                    membrane[channel][point].to_double() != next ||
                    output[channel][point].to_uint() != unsigned(expected_output)) {
                    std::cerr << "FAIL experiment=" << SADA_EXPERIMENT
                              << " step=" << step << " channel=" << channel
                              << " point=" << point << " sum=" << sums[channel][point]
                              << " expected=" << expected_sum << std::endl;
                    assert(false);
                }
            }
        }
    }
}

int main() {
    check_index_and_accumulator_ranges();
    check_reduction_and_state();
    check_density_helpers();
    prepare_inputs();
    build_reference_ids();
    check_selection();
    check_complete_kernel();
    std::cout << "PASS experiment=" << SADA_EXPERIMENT
              << " references=" << REFERENCE_COUNT
              << " points=" << POINT_COUNT << " channels=" << CHANNEL_COUNT
              << " steps=2" << std::endl;
    return 0;
}
