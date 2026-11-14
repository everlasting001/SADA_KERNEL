#include "sada_kernel.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    static density_t rho[N_POINTS];
    static normalized_density_t rho_clip[N_POINTS];

    // Test 1: signs, fractions, boundaries, and clipping.
    const double inputs[] = {
        0, 4, 8, 10, 10.5, 12, 14, 16, 20
    };

    const double expected[] = {
        -3, -3, -1, 0, 0.25, 1, 2, 3, 3
    };

    for (int i = 0; i < N_POINTS; ++i) {
        rho[i] = density_t(inputs[i % 9]);
    }

    normalize_clip_density(
        rho,
        density_t(10),
        density_t(2),
        rho_clip
    );

    for (int i = 0; i < N_POINTS; ++i) {
        assert(rho_clip[i].to_double() == expected[i % 9]);
    }

    // Test 2: zero standard deviation must use epsilon.
    for (int i = 0; i < N_POINTS; ++i) {
        rho[i] = density_t(10);
    }

    rho[0] = density_t(9.75);
    rho[1] = density_t(10.25);

    normalize_clip_density(
        rho,
        density_t(10),
        density_t(0),
        rho_clip
    );

    assert(rho_clip[0] == normalized_density_t(-3));
    assert(rho_clip[1] == normalized_density_t(3));

    for (int i = 2; i < N_POINTS; ++i) {
        assert(rho_clip[i] == normalized_density_t(0));
    }

    // Test 3: non-exact division, including a negative result.
    rho[0] = density_t(9);
    rho[1] = density_t(11);

    normalize_clip_density(
        rho,
        density_t(10),
        density_t(3),
        rho_clip
    );

    const double tolerance = 2.0 * std::ldexp(1.0, -24);

    assert(std::fabs(rho_clip[0].to_double() + 1.0 / 3.0)
           <= tolerance);

    assert(std::fabs(rho_clip[1].to_double() - 1.0 / 3.0)
           <= tolerance);

    for (int i = 2; i < N_POINTS; ++i) {
        assert(rho_clip[i] == normalized_density_t(0));
    }

    std::cout << "Normalization and clipping tests passed."
              << std::endl;
    return 0;
}

