#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main(){
    static_assert(N_REFS == 8, "This test requires 8 references.");

    spike_t k_all[N_POINTS];

    for (int i = 0; i < N_POINTS; i++) {
        k_all[i] = 1;
    }

    const index_t candidate_idx[N_CAND] = {
        16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 30, 31
    };

    const spike_t pattern[N_REFS] = {
        1, 0, 1, 0, 0, 1, 1, 0
    };

    // Write the pattern at the selected point IDs.
    for (int r = 0; r < N_REFS; r++) {
        index_t j = candidate_idx[r];
        k_all[j] = pattern[r];
    }

    fixed_t membrane = 0;
    count_t mismatch_count = 0;

    spike_t output = attention_point(
        spike_t(1), // Q
        k_all,
        candidate_idx,
        spike_t(1), // V
        fixed_t(0.5),
        fixed_t(1.0),
        membrane,
        mismatch_count
    );

    std::cout << "count=" << mismatch_count.to_uint()
              << ", membrane=" << membrane.to_double()
              << ", output=" << output.to_uint()
              << std::endl;

    assert(mismatch_count == 4);
    assert(membrane == fixed_t(0));
    assert(output == 1);

    std::cout << "Reference lookup test passed." << std::endl;

    return 0;
}