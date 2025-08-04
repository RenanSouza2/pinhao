#ifndef __BILINEAR_H__
#define __BILINEAR_H__

#include "../union/struct.h"

void binary_splitting_join(
    union_num_t out[],
    union_num_t res_1[3],
    union_num_t res_2[3]
);
void binary_splitting(union_num_t out[], uint64_t size, uint64_t i_0, uint64_t i_max);

#endif
