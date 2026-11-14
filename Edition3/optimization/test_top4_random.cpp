#include "sada_kernel.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

int main() {
    std::mt19937 random(20261114);
    normalized_density_t density[POINT_COUNT];
    point_index_t candidates[CANDIDATE_COUNT];
    point_index_t selected[DENSITY_REFERENCE_COUNT];
    int grid[POINT_COUNT];
    for (int test = 0; test < 1000; ++test) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            grid[point] = test % 5 == 0 ? 0 : int(random() % 25) - 12;
            density[point] = normalized_density_t(grid[point] * 0.25);
        }
        int current = random() % POINT_COUNT;
        std::vector<int> pool;
        for (int point = 0; point < POINT_COUNT; ++point) {
            if (point != current) pool.push_back(point);
        }
        std::shuffle(pool.begin(), pool.end(), random);
        for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) candidates[slot] = pool[slot];
        select_density_neighbors(point_index_t(current), density, candidates, selected);
        for (int slot = 0; slot < NEAREST_COUNT; ++slot) assert(selected[slot] == pool[slot]);
        std::vector<int> positions(CANDIDATE_COUNT - NEAREST_COUNT);
        std::iota(positions.begin(), positions.end(), NEAREST_COUNT);
        std::stable_sort(positions.begin(), positions.end(), [&](int a, int b) {
            return std::abs(grid[current] - grid[pool[a]]) >
                   std::abs(grid[current] - grid[pool[b]]);
        });
        for (int slot = 0; slot < DENSITY_EXTRA_COUNT; ++slot) {
            assert(selected[NEAREST_COUNT + slot] == pool[positions[slot]]);
        }
    }
    std::cout << "PASS: 1000 randomized Top-4 cases, including 200 all-tie cases."
              << std::endl;
}
