#include "sada_kernel.h"
#include "reciprocal_config.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main() {
    std::mt19937 random(20261114);
    std::vector<std::uint32_t> inputs = {0, 1, 2, 17, 0xffffffffU};
    for (int bit = 0; bit < 32; ++bit) {
        std::uint32_t value = std::uint32_t(1) << bit;
        inputs.push_back(value - 1);
        inputs.push_back(value);
        inputs.push_back(value + 1);
    }
    for (int test = 0; test < 20000; ++test) inputs.push_back(random());
    std::sort(inputs.begin(), inputs.end());
    std::uint64_t previous = ~std::uint64_t(0);
    double max_relative = 0;
    for (std::uint32_t raw : inputs) {
        distance_t distance;
        distance.range(31, 0) = raw;
        density_t density = estimate_density(distance);
        std::uint64_t actual = density.range(47, 0).to_uint64();
        std::uint64_t denominator = (std::uint64_t(raw) << 4) + 17;
        std::uint64_t exact = (std::uint64_t(1) << 48) / denominator;
        assert(actual <= previous);
        previous = actual;
        double relative = std::fabs(double(actual) - double(exact)) / exact;
        max_relative = std::max(max_relative, relative);
#if LUT_IS_EXACT
        assert(actual == exact);
#else
        const double size = 1 << LUT_INDEX_BITS;
        const double bound = LUT_LINEAR
            ? 1.0 / (4 * size * size) + 1.0 / (size * 65536.0) + std::ldexp(1.0, -29)
            : 1.0 / (2 * size) + std::ldexp(1.0, -29);
        assert(relative <= bound + 3.0 / exact);
#endif
    }
    std::cout << "DOMAIN_PASS points=" << inputs.size() << " mean_bits=[0,4294967295]"
              << " monotonic=true max_relative_to_fixed_divider=" << max_relative << std::endl;
}
