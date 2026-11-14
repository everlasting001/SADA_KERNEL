#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main(){
    static_assert(N_POINTS == 256, "Requires 256 points.");
    static_assert(N_REFS == 8, "Requires 8 references.");

    spike_t q[N_POINTS];
    spike_t k[N_POINTS];
    spike_t v[N_POINTS];
    index_t candidate_idx[N_POINTS][N_CAND];

    fixed_t membrane[N_POINTS];
    spike_t output[N_POINTS];
    count_t mismatch_count[N_POINTS];

    // Prepare input data and initial states.
    for (int i = 0; i < N_POINTS; ++i) {
        q[i] = i % 2;
        k[i] = 1;
        v[i] = (i % 3 == 0);
        membrane[i] = fixed_t(0.5);

        // Synthetic candidates; skip the current point.
        for (int r = 0; r < N_CAND; ++r) {
            candidate_idx[i][r] =
                (i + r + 1) % N_POINTS;
        }
    }

    // Only point 0 has K=0.
    k[0] = 0;

    attention_channel(
        q, k, v, candidate_idx,
        fixed_t(0.5),
        fixed_t(1.0),
        membrane, output, mismatch_count
    );

    for (int i = 0; i < N_POINTS; ++i) {
        bool has_zero_k = (i >= N_POINTS - N_REFS);

        if (i % 2 == 0) {
            // Q=0: seven or eight mismatches; fire and reset.
            assert(mismatch_count[i] ==
                   (has_zero_k ? 7 : 8));
            assert(membrane[i] == fixed_t(0));
            assert(output[i] == v[i]);
        } else {
            // Q=1: zero or one mismatch; no spike.
            assert(mismatch_count[i] ==
                   (has_zero_k ? 1 : 0));
            assert(membrane[i] ==
                   fixed_t(has_zero_k ? 0.75 : 0.25));
            assert(output[i] == 0);
        }
    }

    std::cout << "All 256 points passed." << std::endl;
    return 0;
}