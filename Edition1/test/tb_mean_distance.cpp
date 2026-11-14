#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main() {
    static_assert(N_CAND == 16, "Requires 16 candidates.");

    distance_t distances[N_CAND];

    // Test 1: identical distances.
    for (int r = 0; r < N_CAND; ++r) {
        distances[r] = distance_t(0.25);
    }

    distance_t result = mean_squared_distance(distances);
    assert(result == distance_t(0.25));

    // Test 2: two different distance groups.
    for (int r = 0; r < N_CAND; ++r) {
        distances[r] =
            distance_t(r < 8 ? 0.25 : 0.75);
    }

    result = mean_squared_distance(distances);
    assert(result == distance_t(0.5));

    // Test 3: the sum exceeds the distance type's range.
    for (int r = 0; r < N_CAND; ++r) {
        distances[r] = distance_t(3000);
    }

    result = mean_squared_distance(distances);
    assert(result == distance_t(3000));

    std::cout << "Mean distance tests passed."
              << std::endl;
    return 0;
}