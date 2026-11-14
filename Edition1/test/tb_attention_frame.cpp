#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");
    static_assert(N_REFS == 8, "Requires 8 references.");

    // Static arrays avoid large stack allocations.
    static spike_t q[N_CHANNELS][N_POINTS];
    static spike_t k[N_CHANNELS][N_POINTS];
    static spike_t v[N_CHANNELS][N_POINTS];
    static index_t candidate_idx[N_POINTS][N_CAND];

    static fixed_t membrane[N_CHANNELS][N_POINTS];
    static spike_t output[N_CHANNELS][N_POINTS];
    static count_t mismatch_count[N_CHANNELS][N_POINTS];

    // Candidates are shared by all channels.
    for (int i = 0; i < N_POINTS; ++i) {
        for (int r = 0; r < N_CAND; ++r) {
            candidate_idx[i][r] =
                (i + r + 1) % N_POINTS;
        }
    }

    // Use different input patterns across channels.
    for (int c = 0; c < N_CHANNELS; ++c) {
        for (int i = 0; i < N_POINTS; ++i) {
            q[c][i] = (c + i) % 2;
            k[c][i] = 1;
            v[c][i] = ((c + i) % 3 == 0);
            membrane[c][i] = fixed_t(0.5);
        }

        // Even channels: point 0 has K=0.
        // Odd channels: every point has K=1.
        k[c][0] = c % 2;
    }

    // Two consecutive time steps; keep membrane states.
    for (int step = 0; step < 2; ++step) {
        attention_frame(
            q, k, v, candidate_idx,
            fixed_t(0.5), fixed_t(1.0),
            membrane, output, mismatch_count
        );

        for (int c = 0; c < N_CHANNELS; ++c) {
            for (int i = 0; i < N_POINTS; ++i) {
                bool has_zero_k =
                    (c % 2 == 0) &&
                    (i >= N_POINTS - N_REFS);

                if (q[c][i] == 0) {
                    assert(mismatch_count[c][i] ==
                           (has_zero_k ? 7 : 8));
                    assert(membrane[c][i] == fixed_t(0));
                    assert(output[c][i] == v[c][i]);
                } else {
                    fixed_t expected_membrane;

                    if (step == 0) {
                        expected_membrane =
                            has_zero_k ? 0.75 : 0.25;
                    } else {
                        expected_membrane =
                            has_zero_k ? 0.875 : 0.125;
                    }

                    assert(mismatch_count[c][i] ==
                           (has_zero_k ? 1 : 0));
                    assert(membrane[c][i] ==
                           expected_membrane);
                    assert(output[c][i] == 0);
                }
            }
        }
    }

    std::cout << "All channels and states passed."
              << std::endl;
    return 0;
}