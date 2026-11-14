#include "sada_kernel.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    // Exactly representable input distances.
    const double test_values[] = {
        0.25,
        0.5,
        1.0 / 1024.0,
        0.0
    };

    const density_t epsilon = density_t(0.000001);

    // density_t has 24 fractional bits.
    const double lsb = 1.0 / 16777216.0;

    for (int i = 0; i < 4; ++i) {
        distance_t distance = distance_t(test_values[i]);

        density_t result = estimate_density(distance);

        // Floating-point reference using quantized inputs.
        double reference =
            1.0 / (distance.to_double() + epsilon.to_double());

        double actual = result.to_double();
        double error = std::fabs(actual - reference);

        std::cout << "distance=" << distance.to_double()
                  << ", density=" << actual
                  << ", reference=" << reference
                  << ", error=" << error
                  << std::endl;

        assert(actual > 0.0);
        assert(error <= 2.0 * lsb);
    }

    std::cout << "Density tests passed." << std::endl;
    return 0;
}