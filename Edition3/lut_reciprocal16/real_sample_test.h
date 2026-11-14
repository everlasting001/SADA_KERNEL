#include "real_sample.h"
#include "reciprocal_config.h"

// Real export has 64 channels; keep the 96-channel synthesis baseline unchanged.
static void check_real_sample() {
    static double exact_state[CHANNEL_COUNT][POINT_COUNT];
    unsigned long long sum_changes = 0, value_changes = 0, state_changes = 0;
    int changed_rows = 0, changed_sets = 0;
    density_t real_density[POINT_COUNT];
    for (int point = 0; point < POINT_COUNT; ++point) {
        for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) {
            int address = point * CANDIDATE_COUNT + slot;
            distances[point][slot].range(31, 0) = REAL_DISTANCE_BITS[address];
            candidates[point][slot] = REAL_CANDIDATES[address];
        }
    }
    compute_raw_density(distances, real_density);
    for (int point = 0; point < POINT_COUNT; ++point) {
        assert(real_density[point].range(47, 0).to_uint64() == REAL_VARIANT_DENSITY_BITS[point]);
    }
    select_reference_points(distances, candidates, actual_ids);
    for (int point = 0; point < POINT_COUNT; ++point) {
        bool changed = false, different_set = false;
        for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
            unsigned chosen = EXPERIMENT == Experiment::XorDensity8
                ? REAL_VARIANT_IDS[point * 8 + slot] : REAL_CANDIDATES[point * 16 + slot];
            expected_ids[point][slot] = chosen;
            assert(actual_ids[point][slot].to_uint() == chosen);
            if (EXPERIMENT == Experiment::XorDensity8) {
                changed |= chosen != REAL_EXACT_IDS[point * 8 + slot];
                bool present = false;
                for (int old = 0; old < 8; ++old) present |= chosen == REAL_EXACT_IDS[point * 8 + old];
                different_set |= !present;
            }
        }
        changed_rows += changed;
        changed_sets += different_set;
    }
    for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            membrane[channel][point] = 0;
            expected_membrane[channel][point] = 0;
            exact_state[channel][point] = 0;
        }
    }
    for (int step = 0; step < 4; ++step) {
        for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
            for (int point = 0; point < POINT_COUNT; ++point) {
                int address = (step * 64 + channel) * POINT_COUNT + point;
                queries[channel][point] = channel < 64 ? ((REAL_Q[address / 64] >> (address % 64)) & 1) : 0;
                keys[channel][point] = channel < 64 ? ((REAL_K[address / 64] >> (address % 64)) & 1) : 0;
                values[channel][point] = channel < 64 ? ((REAL_V[address / 64] >> (address % 64)) & 1) : 0;
            }
        }
        sada_attention_kernel(distances, candidates, queries, keys, values,
                              membrane_t(0.5), membrane_t(1), membrane, output, sums);
        for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
            for (int point = 0; point < POINT_COUNT; ++point) {
                int approximate_sum = 0, exact_sum = 0;
                int query = queries[channel][point].to_uint();
                for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
                    int chosen = expected_ids[point][slot].to_uint();
                    int gold = EXPERIMENT == Experiment::XorDensity8
                        ? REAL_EXACT_IDS[point * 8 + slot] : REAL_CANDIDATES[point * 16 + slot];
                    int key = keys[channel][chosen].to_uint();
                    int gold_key = keys[channel][gold].to_uint();
                    approximate_sum += EXPERIMENT == Experiment::SubtractNearest8 ? query - key : (query != key);
                    exact_sum += EXPERIMENT == Experiment::SubtractNearest8 ? query - gold_key : (query != gold_key);
                }
                double next = expected_membrane[channel][point] + 0.5 *
                              (approximate_sum - expected_membrane[channel][point]);
                bool gate = next >= 1;
                expected_membrane[channel][point] = gate ? 0 : next;
                int expected_value = gate && values[channel][point].to_uint();
                assert(sums[channel][point].to_int() == approximate_sum);
                assert(membrane[channel][point].to_double() == expected_membrane[channel][point]);
                assert(output[channel][point].to_uint() == unsigned(expected_value));
                double gold_next = exact_state[channel][point] + 0.5 * (exact_sum - exact_state[channel][point]);
                bool gold_gate = gold_next >= 1;
                exact_state[channel][point] = gold_gate ? 0 : gold_next;
                if (channel < 64) {
                    sum_changes += approximate_sum != exact_sum;
                    value_changes += expected_value != int(gold_gate && values[channel][point].to_uint());
                    state_changes += expected_membrane[channel][point] != exact_state[channel][point];
                }
            }
        }
    }
    std::cout << "REAL_SAMPLE rows_changed=" << changed_rows << " sets_changed=" << changed_sets
              << " xor_sums_changed=" << sum_changes << " values_changed=" << value_changes
              << " states_changed=" << state_changes
              << " real_channels=64 padded_channels=32 steps=4 decay=0.5 threshold=1" << std::endl;
}
