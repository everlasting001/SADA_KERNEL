#include "sada_kernel.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

void run_case(const char *name, bool varied) {
    static distance_t distances[N_POINTS][N_CAND];
    static index_t candidate_idx[N_POINTS][N_CAND];
    static index_t active_idx[N_POINTS][N_REFS];

    double reference_rho[N_POINTS];
    double reference_clip[N_POINTS];

    const density_t epsilon = density_t(0.000001);
    const double eps = epsilon.to_double();

    // Build sorted synthetic distances and valid candidate IDs.
    for (int i = 0; i < N_POINTS; ++i) {
        double base = varied
            ? (i % 4 + 1) * 0.125
            : 0.125;

        double distance_sum = 0;

        for (int r = 0; r < N_CAND; ++r) {
            distances[i][r] =
                distance_t(r < 8 ? base : 3.0 * base);

            candidate_idx[i][r] =
                (i + r + 1) % N_POINTS;

            distance_sum += distances[i][r].to_double();
        }

        reference_rho[i] =
            1.0 / (distance_sum / N_CAND + eps);
    }

    // Independent floating-point statistics.
    double mean = 0;

    for (int i = 0; i < N_POINTS; ++i) {
        mean += reference_rho[i];
    }
    mean /= N_POINTS;

    double variance = 0;

    for (int i = 0; i < N_POINTS; ++i) {
        double diff = reference_rho[i] - mean;
        variance += diff * diff;
    }
    variance /= (N_POINTS - 1);

    double std_safe = std::sqrt(variance);
    if (std_safe < eps) {
        std_safe = eps;
    }

    // Normalize and clip the software reference.
    for (int i = 0; i < N_POINTS; ++i) {
        double value = (reference_rho[i] - mean) / std_safe;

        if (value > 3.0) value = 3.0;
        if (value < -3.0) value = -3.0;

        reference_clip[i] = value;
    }

    // Run the complete HLS function.
    density_selector(distances, candidate_idx, active_idx);

    for (int i = 0; i < N_POINTS; ++i) {
        // Verify nearest-neighbor preservation.
        for (int r = 0; r < N_NEAR; ++r) {
            assert(active_idx[i][r] == candidate_idx[i][r]);
        }

        int positions[N_CAND - N_NEAR];

        for (int p = 0; p < N_CAND - N_NEAR; ++p) {
            positions[p] = N_NEAR + p;
        }

        // Software Top-4 with the same tie rule.
        std::stable_sort(
            positions,
            positions + (N_CAND - N_NEAR),
            [&](int a, int b) {
                int id_a = candidate_idx[i][a].to_uint();
                int id_b = candidate_idx[i][b].to_uint();

                double score_a = std::fabs(
                    reference_clip[i] - reference_clip[id_a]
                );
                double score_b = std::fabs(
                    reference_clip[i] - reference_clip[id_b]
                );

                return score_a > score_b;
            }
        );

        for (int s = 0; s < N_DENSITY; ++s) {
            index_t expected = candidate_idx[i][positions[s]];
            index_t actual = active_idx[i][N_NEAR + s];

            if (actual != expected) {
                std::cout << name
                          << ": point=" << i
                          << ", slot=" << s
                          << ", actual=" << actual.to_uint()
                          << ", expected=" << expected.to_uint()
                          << std::endl;
                assert(false);
            }
        }

        // Check that all eight reference IDs are distinct.
        for (int s = 0; s < N_REFS; ++s) {
            for (int p = 0; p < s; ++p) {
                assert(active_idx[i][s] != active_idx[i][p]);
            }
        }
    }

    std::cout << name << " passed." << std::endl;
}

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");
    static_assert(N_CAND == 16, "Requires 16 candidates.");
    static_assert(N_REFS == 8, "Requires 8 references.");
    static_assert(N_NEAR == 4, "Requires 4 nearest points.");

    run_case("Uniform density", false);
    run_case("Varied density", true);

    std::cout << "Full density selector tests passed."
              << std::endl;
    return 0;
}