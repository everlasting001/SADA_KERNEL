#include "sada_kernel.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// Emit a deterministic bit trace to compare separate baseline/optimized builds.
static distance_t distances[POINT_COUNT][CANDIDATE_COUNT];
static point_index_t candidates[POINT_COUNT][CANDIDATE_COUNT];
static point_index_t references[POINT_COUNT][REFERENCE_COUNT];
static spike_t q[CHANNEL_COUNT][POINT_COUNT];
static spike_t k[CHANNEL_COUNT][POINT_COUNT];
static spike_t v[CHANNEL_COUNT][POINT_COUNT];
static membrane_t membrane[CHANNEL_COUNT][POINT_COUNT];
static spike_t output[CHANNEL_COUNT][POINT_COUNT];
static attention_sum_t sums[CHANNEL_COUNT][POINT_COUNT];
static std::uint64_t digest = 14695981039346656037ULL;

static void mix(std::uint32_t word) {
    for (int byte = 0; byte < 4; ++byte) {
        digest ^= (word >> (8 * byte)) & 255;
        digest *= 1099511628211ULL;
    }
}

int main() {
    std::mt19937 random(20261114);
    for (int sample = 0; sample < 4; ++sample) {
        for (int point = 0; point < POINT_COUNT; ++point) {
            std::vector<int> ids;
            for (int id = 0; id < POINT_COUNT; ++id) {
                if (id != point) ids.push_back(id);
            }
            std::shuffle(ids.begin(), ids.end(), random);
            // Synthetic sorted distances: random, zero, constant and tiny.
            unsigned raw_distance = 0;
            for (int slot = 0; slot < CANDIDATE_COUNT; ++slot) {
                raw_distance += random() % 64;
                double d = sample == 0 ? raw_distance / 128.0 :
                           sample == 1 ? 0.0 : sample == 2 ? 0.5 :
                           raw_distance / 1048576.0;
                distances[point][slot] = distance_t(d);
                candidates[point][slot] = ids[slot];
            }
        }
        select_reference_points(distances, candidates, references);
        for (int point = 0; point < POINT_COUNT; ++point) {
            for (int slot = 0; slot < REFERENCE_COUNT; ++slot) {
                mix(references[point][slot].to_uint());
            }
        }
        for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
            for (int point = 0; point < POINT_COUNT; ++point) {
                membrane[channel][point] = membrane_t((int(random() % 513) - 256) / 64.0);
            }
        }
        for (int step = 0; step < 4; ++step) {
            for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
                for (int point = 0; point < POINT_COUNT; ++point) {
                    q[channel][point] = random() & 1;
                    k[channel][point] = random() & 1;
                    v[channel][point] = random() & 1;
                }
            }
            const double lambdas[] = {0.0, 0.25, 0.5, 1.0};
            sada_attention_kernel(distances, candidates, q, k, v,
                membrane_t(lambdas[step]), membrane_t(0.75), membrane, output, sums);
            for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
                for (int point = 0; point < POINT_COUNT; ++point) {
                    mix(membrane[channel][point].range(31, 0).to_uint());
                    mix(output[channel][point].to_uint());
                    mix(static_cast<std::uint32_t>(sums[channel][point].to_int()));
                }
            }
        }
        std::cout << "experiment=" << SADA_EXPERIMENT << " sample=" << sample
                  << " digest=" << std::hex << digest << std::dec << '\n';
    }
}
