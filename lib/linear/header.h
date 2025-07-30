#ifndef __LINEAR_H__
#define __LINEAR_H__

#include "struct.h"

fix_num_t pi_v1(uint64_t size);
fix_num_t pi_v2(uint64_t size);
float_num_t pi_v3(uint64_t size);

void union_num_free(union_num_t u);
float_num_t union_num_unwrap_float(union_num_t u);

void binary_splitting_join(
    union_num_t out[],
    union_num_t res_1[3],
    union_num_t res_2[3]
);
void binary_splitting(
    union_num_t out[],
    uint64_t size,
    uint64_t i_0,
    uint64_t i_max,
    uint64_t depth,
    uint64_t max
);

#endif
