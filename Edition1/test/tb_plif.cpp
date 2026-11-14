#include "sada_kernel.h"
#include <cassert>
#include <iostream>

int main(){
    fixed_t lambda = 0.5;
    fixed_t threshold = 1.0;
    fixed_t membrance = 0.0;

    // step 1

    spike_t spike = plif_step(
        fixed_t(1.0),
        lambda, 
        threshold,
        membrance
    );

    assert(spike == 0);
    assert(membrance == fixed_t(0.5));

    // step 2

    spike = plif_step(
        fixed_t(2.0),
        lambda, 
        threshold,
        membrance
    );

    assert(spike == 1);
    assert(membrance == fixed_t(0.0));

    // step 3

    spike = plif_step(
        fixed_t(2.0),
        lambda, 
        threshold,
        membrance
    );

    assert(spike == 1);
    assert(membrance == fixed_t(0.0));

    std::cout << "PLIF tests passed" << std::endl;
    return 0;
}
