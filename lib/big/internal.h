#ifndef BIG_INTERNALS_H
#define BIG_INTERNALS_H

#include <stdatomic.h>

#include "header.h"
#include "../../mods/macros/struct.h"
#include "../../mods/macros/uint.h"

// Set by the task itself once it has begun its final multiplication: past that
// it never reads the slot again, so a donation would be booked and never used.
#define SPLIT_TASK_CLOSED B(63)

// A task handed to a join: the node's identity, plus a pointer to this task's
// thread grant in the scheduler's shared slot array. The scheduler can raise
// the grant while the join runs, so every multiplication re-reads it. The
// grant is the low bits; SPLIT_TASK_CLOSED rides on the same word so closing
// and reading can be one atomic step.
// depth is the node's depth in the whole tree and is only ever logged. level
// is what names it on disk: the depth below the chunk a chain fuses, 0 at that
// chunk, restarting at every fold. A chain node ignores level -- it is named
// by the chunks it fuses.
STRUCT(split_task)
{
    uint64_t index;
    uint64_t size;
    uint64_t i_0;
    uint64_t depth;
    uint64_t level;
    _Atomic uint64_t *threads;
};

void split_chunk_set(uint64_t chunk);

bool split_big_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t level
);
void split_piece(uint64_t index, uint64_t i_0, uint64_t span, uint64_t depth);
void split_span_res_join(split_task_p t, uint64_t span);
uint64_t split_big_res_op_size(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t level, uint64_t index);


bool split_span_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t level
);
void split_big_res_join(split_task_p t, uint64_t remainder);
void split_pair_res_join(split_task_p t, uint64_t remainder, uint64_t remainder_1);
uint64_t split_span_res_op_size(uint64_t size, uint64_t i_0, uint64_t span, uint64_t level, uint64_t index);

bool pi_is_stored(uint64_t size);
bool disk_lock_enabled(void);
flt_num_t pi_load(uint64_t size);
uint64_t get_index_max(uint64_t size, uint64_t piece_size);
flt_num_t pi_finish(uint64_t size, uint64_t piece_size, uint64_t threads);

#endif // BIG_INTERNALS_H
