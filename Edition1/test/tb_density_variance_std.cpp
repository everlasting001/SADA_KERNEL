#include "sada_kernel.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Test two equally sized density groups.
void check_case(
    const char *name,
    double low,
    double high
) {
    static density_t rho[N_POINTS];

    for (int i = 0; i < N_POINTS; ++i) {
        rho[i] = density_t(
            i < N_POINTS / 2 ? low : high
        );
    }

    density_t mean = mean_density(rho);
    variance_t variance = 0;
    density_t std_safe = 0;

    density_variance_std(
        rho, mean, variance, std_safe
    );

    // Analytical floating-point reference.
    double expected_mean = (low + high) / 2.0;
    double delta = high - low;

    double expected_variance =
        delta * delta * N_POINTS /
        (4.0 * (N_POINTS - 1));

    const density_t epsilon = density_t(0.000001);
    double expected_std = std::sqrt(expected_variance);

    if (expected_std < epsilon.to_double()) {
        expected_std = epsilon.to_double();
    }

    // Fixed-point resolutions.
    const double density_lsb = std::ldexp(1.0, -24);
    const double variance_lsb = std::ldexp(1.0, -48);

    double mean_error =
        std::fabs(mean.to_double() - expected_mean);

    double variance_error =
        std::fabs(variance.to_double() - expected_variance);

    double std_error =
        std::fabs(std_safe.to_double() - expected_std);

    std::cout << name
              << ": mean=" << mean.to_double()
              << ", variance=" << variance.to_double()
              << ", std=" << std_safe.to_double()
              << std::endl;

    assert(mean_error <= density_lsb);
    assert(variance_error <= 2.0 * variance_lsb);
    assert(std_error <= 2.0 * density_lsb);

    // Check exact behavior for constant input.
    if (low == high) {
        assert(variance == variance_t(0));
        assert(std_safe == epsilon);
    }
}

int main() {
    static_assert(N_POINTS == 256, "Requires 256 points.");

    check_case("Constant", 2.0, 2.0);

    check_case("Two groups", 2.0, 6.0);

    // Large values with a small, exactly representable difference.
    check_case(
        "Small variation",
        1000000.0,
        1000000.0 + 1.0 / 65536.0
    );

    std::cout << "Variance and std tests passed."
              << std::endl;
    return 0;
}