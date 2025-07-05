#ifndef __JUMPSTART_H__
#define __JUMPSTART_H__

#include "../../mods/number/lib/fix/header.h"

fix_num_t jumpstart_standard(uint64_t index_max, uint64_t size, uint64_t layer_count);
fix_num_t jumpstart_div_during(uint64_t index_max, uint64_t size, uint64_t layer_count);
fix_num_t jumpstart_ass_1(uint64_t index_max, uint64_t size, uint64_t layer_count);
fix_num_t jumpstart_ass_2(uint64_t index_max, uint64_t size, uint64_t layer_count);

fix_num_t jumpstart_thread(
    uint64_t size,
    uint64_t layer_count,
    uint64_t index_0,
    uint64_t thread_0
);

#endif
