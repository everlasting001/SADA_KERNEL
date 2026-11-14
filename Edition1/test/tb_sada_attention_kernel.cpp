#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");
    static_assert(N_CAND == 16, "Requires 16 candidates.");
    static_assert(N_REFS == 8, "Requires 8 references.");

    static distance_t distances[N_POINTS][N_CAND];
    static index_t candidate_idx[N_POINTS][N_CAND];
    static index_t reference_idx[N_POINTS][N_REFS];

    static spike_t q[N_CHANNELS][N_POINTS];
    static spike_t k[N_CHANNELS][N_POINTS];
    static spike_t v[N_CHANNELS][N_POINTS];

    static fixed_t membrane[N_CHANNELS][N_POINTS];
    static double reference_membrane[N_CHANNELS][N_POINTS];

    static spike_t output[N_CHANNELS][N_POINTS];
    static count_t mismatch_count[N_CHANNELS][N_POINTS];

    // Synthetic sorted distances and candidate IDs.
    for (int i = 0; i < N_POINTS; ++i) {
        double base = (i % 4 + 1) * 0.125;

        for (int r = 0; r < N_CAND; ++r) {
            distances[i][r] =
                distance_t(r < 8 ? base : 3.0 * base);

            candidate_idx[i][r] =
                (i + r + 1) % N_POINTS;
        }
    }

    // Prepare independently checked reference IDs.
    density_selector(
        distances, candidate_idx, reference_idx
    );

    // Different spike patterns and states across positions.
    for (int c = 0; c < N_CHANNELS; ++c) {
        for (int i = 0; i < N_POINTS; ++i) {
            q[c][i] = (c + i) % 2;
            k[c][i] = ((i + 3 * c) % 7 == 0);
            v[c][i] = ((c + i) % 3 != 0);

            double initial = ((c + i) % 4) * 0.125;
            membrane[c][i] = fixed_t(initial);
            reference_membrane[c][i] = initial;
        }
    }

    // Test parameters only, not trained parameters.
    const fixed_t lambda = fixed_t(0.5);
    const fixed_t threshold = fixed_t(1.0);

    bool differs_from_nearest = false;

    // Keep states between the two calls.
    for (int step = 0; step < 2; ++step) {
        sada_attention_kernel(
            distances, candidate_idx,
            q, k, v,
            lambda, threshold,
            membrane, output, mismatch_count
        );

        for (int c = 0; c < N_CHANNELS; ++c) {
            for (int i = 0; i < N_POINTS; ++i) {
                int expected_count = 0;
                int nearest_count = 0;
                int q_bit = q[c][i].to_uint();

                // Independent integer XOR-count reference.
                for (int r = 0; r < N_REFS; ++r) {
                    int selected = reference_idx[i][r].to_uint();
                    int nearest = candidate_idx[i][r].to_uint();

                    expected_count +=
                        (q_bit != k[c][selected].to_uint());

                    nearest_count +=
                        (q_bit != k[c][nearest].to_uint());
                }

                if (expected_count != nearest_count) {
                    differs_from_nearest = true;
                }

                // Independent PLIF reference.
                double old_membrane = reference_membrane[c][i];

                double charged =
                    old_membrane +
                    0.5 * (expected_count - old_membrane);

                bool gate = (charged >= 1.0);

                double expected_membrane =
                    gate ? 0.0 : charged;

                int expected_output =
                    gate && (v[c][i].to_uint() != 0);

                reference_membrane[c][i] = expected_membrane;

                // Report the first failing position.
                if (mismatch_count[c][i].to_uint()
                        != static_cast<unsigned>(expected_count) ||
                    membrane[c][i].to_double()
                        != expected_membrane ||
                    output[c][i].to_uint()
                        != static_cast<unsigned>(expected_output)) {

                    std::cout
                        << "FAIL: step=" << step
                        << ", channel=" << c
                        << ", point=" << i
                        << "\ncount: "
                        << mismatch_count[c][i].to_uint()
                        << " / expected " << expected_count
                        << "\nmembrane: "
                        << membrane[c][i].to_double()
                        << " / expected " << expected_membrane
                        << "\noutput: "
                        << output[c][i].to_uint()
                        << " / expected " << expected_output
                        << std::endl;

                    return 1;
                }
            }
        }

        std::cout << "Step " << step << " passed."
                  << std::endl;
    }

    // Ensure the test can detect an accidental nearest-8 path.
    assert(differs_from_nearest);

    std::cout << "Full attention chain passed."
              << std::endl;
    return 0;
}