#include "sada_kernel.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");
    static_assert(N_CAND == 16, "Requires 16 candidates.");
    static_assert(N_REFS == 8, "Requires 8 references.");

    static normalized_density_t rho_clip[N_POINTS];
    static index_t candidate_idx[N_POINTS][N_CAND];
    static index_t active_idx[N_POINTS][N_REFS];

    // Each point has its own candidate list; self is excluded.
    for (int i = 0; i < N_POINTS; ++i) {
        for (int r = 0; r < N_CAND; ++r) {
            candidate_idx[i][r] =
                (i + r + 1) % N_POINTS;
        }
    }

    // Test 1: all density differences are zero.
    for (int i = 0; i < N_POINTS; ++i) {
        rho_clip[i] = 0;
    }

    select_all_density_refs(
        rho_clip, candidate_idx, active_idx
    );

    // Tie rule: preserve candidate order.
    for (int i = 0; i < N_POINTS; ++i) {
        for (int s = 0; s < N_REFS; ++s) {
            assert(active_idx[i][s] == candidate_idx[i][s]);
        }
    }

    // Test 2: different densities across points.
    for (int i = 0; i < N_POINTS; ++i) {
        rho_clip[i] =
            normalized_density_t((i % 16 - 8) * 0.25);
    }

    select_all_density_refs(
        rho_clip, candidate_idx, active_idx
    );

    for (int i = 0; i < N_POINTS; ++i) {
        // The nearest four must remain unchanged.
        for (int r = 0; r < N_NEAR; ++r) {
            assert(active_idx[i][r] == candidate_idx[i][r]);
        }

        // Build a software reference using candidate positions.
        int positions[N_CAND - N_NEAR];

        for (int p = 0; p < N_CAND - N_NEAR; ++p) {
            positions[p] = N_NEAR + p;
        }

        double center = rho_clip[i].to_double();

        // Sort by descending difference; preserve order on ties.
        std::stable_sort(
            positions,
            positions + (N_CAND - N_NEAR),
            [&](int a, int b) {
                int id_a = candidate_idx[i][a].to_uint();
                int id_b = candidate_idx[i][b].to_uint();

                double score_a =
                    std::fabs(center - rho_clip[id_a].to_double());

                double score_b =
                    std::fabs(center - rho_clip[id_b].to_double());

                return score_a > score_b;
            }
        );

        for (int s = 0; s < N_DENSITY; ++s) {
            index_t expected = candidate_idx[i][positions[s]];
            assert(active_idx[i][N_NEAR + s] == expected);
        }

        // All eight selected IDs must be distinct.
        for (int s = 0; s < N_REFS; ++s) {
            for (int p = 0; p < s; ++p) {
                assert(active_idx[i][s] != active_idx[i][p]);
            }
        }
    }

    std::cout << "All-point density selection tests passed."
              << std::endl;
    return 0;
}