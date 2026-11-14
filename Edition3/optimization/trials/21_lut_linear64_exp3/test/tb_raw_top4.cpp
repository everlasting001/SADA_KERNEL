#include "sada_kernel.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

int main() {
    std::mt19937 random(20261114);
    density_t densities[POINT_COUNT];
    std::uint64_t raw_bits[POINT_COUNT];
    point_index_t candidates[CANDIDATE_COUNT];
    point_index_t selected[DENSITY_REFERENCE_COUNT];
    for (int test = 0; test < 1000; ++test) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            std::uint64_t bits = (std::uint64_t(random() & 65535) << 32) | random();
            if (test % 5 == 0) bits = 0;
            if (test % 5 == 1) bits = std::uint64_t(random() % 25) << 24;
            if (test % 5 == 2) bits = random() % 256;
            raw_bits[point] = bits;
            densities[point].range(47, 0) = ap_uint<48>(bits);
        }
        int current = random() % POINT_COUNT;
        std::vector<int> pool;
        for (int point = 0; point < POINT_COUNT; ++point) {
            if (point != current) pool.push_back(point);
        }
        std::shuffle(pool.begin(), pool.end(), random);
        for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) candidates[slot] = pool[slot];
        select_density_neighbors(point_index_t(current), densities, candidates, selected);
        for (int slot = 0; slot < NEAREST_COUNT; ++slot) assert(selected[slot] == pool[slot]);
        auto score = [&](int slot) {
            std::uint64_t a = raw_bits[current], b = raw_bits[pool[slot]];
            return a > b ? a - b : b - a;
        };
        std::vector<int> positions(CANDIDATE_COUNT - NEAREST_COUNT);
        std::iota(positions.begin(), positions.end(), NEAREST_COUNT);
        std::stable_sort(positions.begin(), positions.end(), [&](int a, int b) {
            return score(a) > score(b);
        });
        for (int slot = 0; slot < DENSITY_EXTRA_COUNT; ++slot) {
            assert(selected[NEAREST_COUNT + slot] == pool[positions[slot]]);
        }
    }
    std::cout << "PASS: 1000 raw-density Top-4 cases; full 48-bit range, fractional LSBs, and 200 all-tie cases."
              << std::endl;
}
