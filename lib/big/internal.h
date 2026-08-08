#ifndef BIG_INTERNALS_H
#define BIG_INTERNALS_H

#include "header.h"

bool split_big_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
);
void split_piece(uint64_t i_0, uint64_t span);
void split_span_res_join(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth);


bool split_span_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
);
void split_big_res_join(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth);

bool pi_is_stored(uint64_t size);
flt_num_t pi_load(uint64_t size);
uint64_t get_index_max(uint64_t size, uint64_t piece_size);
flt_num_t pi_finish(uint64_t size, uint64_t piece_size);

#endif // BIG_INTERNALS_H