#ifndef BIG_INTERNALS_H
#define BIG_INTERNALS_H

#include <stdatomic.h>

#include "header.h"
#include "../../mods/macros/struct.h"

// A task handed to a join: the node's identity, plus a pointer to this task's
// thread grant in the scheduler's shared slot array. The scheduler can raise
// the grant while the join runs, so every multiplication re-reads it.
STRUCT(split_task)
{
    uint64_t index;
    uint64_t size;
    uint64_t i_0;
    uint64_t depth;
    const _Atomic uint64_t *threads;
};

bool split_big_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
);
void split_piece(uint64_t index, uint64_t i_0, uint64_t span, uint64_t depth);
void split_span_res_join(split_task_p t, uint64_t span);
uint64_t split_big_res_op_size(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth, uint64_t index);


bool split_span_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
);
void split_big_res_join(split_task_p t, uint64_t remainder);
uint64_t split_span_res_op_size(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth, uint64_t index);

bool pi_is_stored(uint64_t size);
bool disk_lock_enabled(void);
flt_num_t pi_load(uint64_t size);
uint64_t get_index_max(uint64_t size, uint64_t piece_size);
flt_num_t pi_finish(uint64_t size, uint64_t piece_size);

#endif // BIG_INTERNALS_H
