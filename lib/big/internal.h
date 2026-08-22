#ifndef BIG_INTERNALS_H
#define BIG_INTERNALS_H

#include "header.h"

bool split_big_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
);
<<<<<<< HEAD
void split_piece(uint64_t index, uint64_t i_0, uint64_t span, uint64_t depth);
void split_span_res_join(uint64_t index, uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth, uint64_t threads);
=======
void split_piece(uint64_t i_0, uint64_t span);
void split_span_res_join(uint64_t index, uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth);
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
uint64_t split_big_res_op_size(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth, uint64_t index);


bool split_span_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
);
<<<<<<< HEAD
void split_big_res_join(uint64_t index, uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth, uint64_t threads);
=======
void split_big_res_join(uint64_t index, uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth);
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
uint64_t split_span_res_op_size(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth, uint64_t index);

bool pi_is_stored(uint64_t size);
bool disk_lock_enabled(void);
flt_num_t pi_load(uint64_t size);
uint64_t get_index_max(uint64_t size, uint64_t piece_size);
flt_num_t pi_finish(uint64_t size, uint64_t piece_size, uint64_t threads);

#endif // BIG_INTERNALS_H