#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main(){ 
    fixed_t lambda = 0.5;
    fixed_t threshold = 1.0;
    fixed_t membrane = 0.0;
    count_t mismatch_count = 0;

    const spike_t k_refs[N_REFS] = {
        1, 0, 1, 0, 0, 1, 1, 0
    };

    // Test 1
    spike_t output = attention_unit(
        spike_t(1), 
        k_refs, 
        spike_t(1),
        lambda,
        threshold,
        membrane,
        mismatch_count
    );

    assert(mismatch_count == 4);
    assert(membrane == fixed_t(0.0));
    assert(output == 1);

    // Test 2
    membrane = fixed_t(0.0);
    
    output = attention_unit(
        spike_t(1), 
        k_refs, 
        spike_t(0),
        lambda,
        threshold,
        membrane,
        mismatch_count
    );

    assert(mismatch_count == 4);
    assert(membrane == fixed_t(0.0));
    assert(output == 0);

    // Test 3

    const spike_t k_one_diff[N_REFS] = {
        0, 1, 1, 1, 1, 1, 1, 1
    };

    membrane = fixed_t(0.0);

    output = attention_unit(
        spike_t(1), 
        k_one_diff,
        spike_t(0),
        lambda,
        threshold,
        membrane,
        mismatch_count
    );

    assert(mismatch_count == 1);
    assert(membrane == fixed_t(0.5));
    assert(output == 0);

    std::cout << "Attention unit tests passed" << std::endl;
    return 0;
}
