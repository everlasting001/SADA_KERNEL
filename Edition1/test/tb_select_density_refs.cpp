#include "sada_kernel.h"
#include <cassert>
#include <iostream>

// Check output IDs and reject duplicates.
void check_result(
    const index_t active_idx[N_REFS],
    const int expected[N_REFS]
) {
    for (int s = 0; s < N_REFS; ++s) {
        assert(active_idx[s] == expected[s]);

        for (int p = 0; p < s; ++p) {
            assert(active_idx[s] != active_idx[p]);
        }
    }
}

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");
    static_assert(N_CAND == 16, "Requires 16 candidates.");
    static_assert(N_REFS == 8, "Requires 8 references.");
    static_assert(N_NEAR == 4, "Requires 4 nearest points.");

    static normalized_density_t rho_clip[N_POINTS];
    index_t candidate_idx[N_CAND];
    index_t active_idx[N_REFS];

    // Current point is 0; candidates are point IDs 16..31.
    for (int r = 0; r < N_CAND; ++r) {
        candidate_idx[r] = 16 + r;
    }

    // Test 1: unique scores among the remaining candidates.
    for (int i = 0; i < N_POINTS; ++i) {
        rho_clip[i] = 0;
    }

    // Nearest four have high scores but must not be selected twice.
    for (int r = 0; r < N_NEAR; ++r) {
        rho_clip[candidate_idx[r]] = 3;
    }

    // Absolute differences: 0.25, 0.5, ..., 3.0.
    for (int r = N_NEAR; r < N_CAND; ++r) {
        double magnitude = (r - N_NEAR + 1) * 0.25;

        rho_clip[candidate_idx[r]] =
            normalized_density_t(
                r % 2 == 0 ? -magnitude : magnitude
            );
    }

    select_density_refs(
        index_t(0), rho_clip, candidate_idx, active_idx
    );

    const int expected1[N_REFS] = {
        16, 17, 18, 19, 31, 30, 29, 28
    };
    check_result(active_idx, expected1);

    // Test 2: all scores are zero.
    for (int i = 0; i < N_POINTS; ++i) {
        rho_clip[i] = 0;
    }

    select_density_refs(
        index_t(0), rho_clip, candidate_idx, active_idx
    );

    const int expected2[N_REFS] = {
        16, 17, 18, 19, 20, 21, 22, 23
    };
    check_result(active_idx, expected2);

    // Test 3: maximum difference of 6.
    for (int i = 0; i < N_POINTS; ++i) {
        rho_clip[i] = 3;
    }

    for (int r = 0; r < N_NEAR; ++r) {
        rho_clip[candidate_idx[r]] = -3;
    }

    rho_clip[26] = -3;  // Score 6.
    rho_clip[27] = -2;  // Score 5.
    rho_clip[28] = -1;  // Score 4.
    rho_clip[29] =  0;  // Score 3.

    select_density_refs(
        index_t(0), rho_clip, candidate_idx, active_idx
    );

    const int expected3[N_REFS] = {
        16, 17, 18, 19, 26, 27, 28, 29
    };
    check_result(active_idx, expected3);

    std::cout << "Density selection tests passed."
              << std::endl;
    return 0;
}