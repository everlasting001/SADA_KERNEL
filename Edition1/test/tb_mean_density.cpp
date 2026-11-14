#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");

    density_t rho[N_POINTS];

    // Test 1: identical densities.
    for (int i = 0; i < N_POINTS; ++i) {
        rho[i] = density_t(2);
    }

    density_t result = mean_density(rho);
    assert(result == density_t(2));

    // Test 2: two density groups.
    for (int i = 0; i < N_POINTS; ++i) {
        rho[i] = density_t(i < 128 ? 2 : 6);
    }

    result = mean_density(rho);
    assert(result == density_t(4));

    // Test 3: check the wider accumulator.
    for (int i = 0; i < N_POINTS; ++i) {
        rho[i] = density_t(1000000);
    }

    result = mean_density(rho);
    assert(result == density_t(1000000));

    std::cout << "Mean density tests passed."
              << std::endl;
    return 0;
}