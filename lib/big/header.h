#ifndef __EXAMPLE_H__
#define __EXAMPLE_H__

#include "../../mods/number/header.h"
#include "../union/struct.h"

void binary_splitting_big(
    union_num_t out[],
    uint64_t size,
    uint64_t depth,
    uint64_t i_0,
    uint64_t i_max
);
flt_num_t pi_big(uint64_t size);

#endif
