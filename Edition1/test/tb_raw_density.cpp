#include "sada_kernel.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    static_assert(N_CAND == 16, "Requires 16 candidates.");

    static distance_t distances[N_POINTS][N_CAND];
    static density_t rho[N_POINTS];

    // Each row uses a different distance scale.
    // Within a row: 8 values are d, and 8 are 3*d.
    for (int i = 0; i < N_POINTS; ++i) {
        distance_t d = distance_t((i % 4 + 1) * 0.125);

        for (int r = 0; r < N_CAND; ++r) {
            distances[i][r] =
                (r < 8) ? d : distance_t(d * 3);
        }
    }

    compute_raw_density(distances, rho);

    const density_t epsilon = density_t(0.000001);
    const double lsb = 1.0 / 16777216.0;

    for (int i = 0; i < N_POINTS; ++i) {
        // mean(d x 8, 3*d x 8) = 2*d.
        double expected_mean = (i % 4 + 1) * 0.25;
        double reference =
            1.0 / (expected_mean + epsilon.to_double());

        double error =
            std::fabs(rho[i].to_double() - reference);

        if (error > 2.0 * lsb) {
            std::cout << "Failed at point " << i
                      << ", actual=" << rho[i].to_double()
                      << ", reference=" << reference
                      << std::endl;
            return 1;
        }
    }

    std::cout << "All raw densities passed." << std::endl;
    return 0;
}