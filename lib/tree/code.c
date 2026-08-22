<<<<<<< HEAD
=======
// #define LOCK_IN_PLACE

>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "debug.h"
#include "../big/internal.h"

#include "../../mods/macros/assert.h"
#include "../../mods/macros/stdbit.h"
#include "../../mods/macros/fork.h"
#include "../../mods/macros/uint.h"
#include "../../mods/macros/time.h"

<<<<<<< HEAD
#define TREE_PIECE_SIZE 24
#define TREE_LEAF_COST_BYTES (U64(512) * 1024 * 1024)
=======
#ifdef DEBUG
#endif

#define TREE_PIECE_SIZE 24
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0

#define NODE_BIG 0
#define NODE_SPAN 1

STRUCT(big)
{
    uint64_t size;
    uint64_t i_0;
    uint64_t remainder;
    uint64_t depth;
};

STRUCT(span)
{
    uint64_t size;
    uint64_t i_0;
    uint64_t span;
    uint64_t depth;
};

// node_can_launch verdicts. HALT is the head-of-line barrier: a node that
// cannot be afforded while other tasks are running stops the whole walk, so no
// smaller task takes the memory it is waiting on.
#define LAUNCH_SKIP 0
#define LAUNCH_TAKE 1
#define LAUNCH_HALT 2

// What node_can_launch decided about a node.
STRUCT(tree_plan)
{
    uint64_t mem_cost;
    uint64_t threads;
};

STRUCT(node)
{
    node_p parent;
    uint64_t type;
    bool processing;
<<<<<<< HEAD
    bool is_stored;
    bool ops_set;
    uint64_t op_1;
    uint64_t op_2;
    tree_plan_t plan;
=======
    uint64_t mem_cost;
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
    union {
        big_t big;
        span_t span;
    } as;
    struct {
        bool done;
        node_p n;
    } children[2];
};

static node_p node_span_create(
    node_p parent,
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
) {
    assert(span >= TREE_PIECE_SIZE);

    node_p n = malloc(sizeof(node_t));
    assert(n);

    *n = (node_t){
        .parent = parent,
        .type = NODE_SPAN,
        .processing = false,
        .is_stored = split_span_res_is_stored(size, i_0, span, depth),
        .as.span = (span_t){
            .size = size,
            .i_0 = i_0,
            .span = span,
            .depth = depth
        }
    };

    if(span == TREE_PIECE_SIZE)
    {
        n->children[0].done = true;
        n->children[1].done = true;
    }

    return n;
}

static node_p node_big_create(
    node_p parent,
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
) {
    if(stdc_count_ones(remainder) == 1)
    {
        uint64_t span = stdc_bit_width(remainder) - 1;
        return node_span_create(parent, size, i_0, span, depth);
    }

    node_p n = malloc(sizeof(node_t));
    assert(n);

    *n = (node_t){
        .parent = parent,
        .type = NODE_BIG,
        .processing = false,
        .is_stored = split_big_res_is_stored(size, i_0, remainder, depth),
        .as.big = (big_t){
            .size = size,
            .i_0 = i_0,
            .remainder = remainder,
            .depth = depth
        }
    };

    return n;
}

static node_p node_expand(node_p n, uint64_t index)
{
    switch(n->type)
    {
        case NODE_BIG:
        {
            uint64_t size = n->as.big.size;
            uint64_t i_0 = n->as.big.i_0;
            uint64_t remainder = n->as.big.remainder;
            uint64_t span = stdc_bit_width(remainder) - 1;
            uint64_t depth = n->as.big.depth;

            switch(index)
            {
                case 0:
                {
                    return node_span_create(n, size, i_0, span, depth + 1);
                }

                case 1:
                {
                    return node_big_create(n, size, i_0 + B(span), remainder - B(span), depth + 1);
                }

                default: revert()
            }
        }

        case NODE_SPAN:
        {
            uint64_t size = n->as.span.size;
            uint64_t i_0 = n->as.span.i_0;
            uint64_t span = n->as.span.span;
            uint64_t depth = n->as.span.depth;
            uint64_t offset = index * B(span - 1);
            return node_span_create(n, size, i_0 + offset, span - 1, depth + 1);
        }

        default: revert()
    }
}

static bool node_is_stored(node_p n)
{
<<<<<<< HEAD
=======
    switch(n->type)
    {
        case NODE_BIG:
        {
            if(split_big_res_is_stored(n->a.b.size, n->a.b.i_0, n->a.b.remainder, n->a.b.depth))
            {
                return true;
            }
        }
        break;

        case NODE_SPAN:
        {
            if(split_span_res_is_stored(n->a.s.size, n->a.s.i_0, n->a.s.span, n->a.s.depth))
            {
                return true;
            }
        }
        break;

        default: revert()
    }
    return false;
}

static bool node_is_ready(node_p n)
{
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
    for(uint64_t i=0; i<2; i++)
    {
        if(!n->children[i].done)
        {
            return false;
        }
    }

    return true;
}

<<<<<<< HEAD
static bool node_is_leaf(node_p n)
{
    return n->type == NODE_SPAN && n->as.span.span == TREE_PIECE_SIZE;
=======
static uint64_t node_estimate_memory(node_p n, uint64_t disk_threshold_bytes)
{
    uint64_t op_1 = 0;
    uint64_t op_2 = 0;

    switch(n->type)
    {
        case NODE_SPAN:
        {
            uint64_t size = n->a.s.size;
            uint64_t i_0 = n->a.s.i_0;
            uint64_t span = n->a.s.span;
            uint64_t depth = n->a.s.depth;
            if(span == TREE_PIECE_SIZE)
            {
                // return 0;
                return 1;
                // return UINT64_MAX / 4;
            }

            op_1 = split_span_res_op_size(size, i_0, span - 1, depth + 1, 2);
            op_2 = split_span_res_op_size(size, i_0 + B(span - 1), span - 1, depth + 1, 1);
        }
        break;

        case NODE_BIG:
        {
            uint64_t size = n->a.b.size;
            uint64_t i_0 = n->a.b.i_0;
            uint64_t remainder = n->a.b.remainder;
            uint64_t depth = n->a.b.depth;
            uint64_t span = stdc_bit_width(remainder) - 1;

            op_1 = split_span_res_op_size(size, i_0, span, depth + 1, 2);
            op_2 = split_big_res_op_size(size, i_0 + B(span), remainder - B(span), depth + 1, 1);
        }
        break;

        default: revert()
    }

    return num_mul_estimate_memory(op_1, op_2, disk_threshold_bytes, 1);
}

STRUCT(tree_task)
{
    pid_t pid;
    node_p n;
    uint64_t time_start;
    bool active;
};

STRUCT(tree_scheduler)
{
    tree_task_p tasks;
    uint64_t total_mem_cost;
    uint64_t n_process;
    uint64_t mem_launch;
    uint64_t mem_max;
};

static tree_scheduler_t tree_scheduler_create(uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    tree_scheduler_t s = {
        .tasks = calloc(n_process, sizeof(tree_task_t)),
        .total_mem_cost = 0,
        .n_process = n_process,
        .mem_launch = mem_launch,
        .mem_max = mem_max
    };
    assert(s.tasks);
    return s;
}

static uint64_t get_free_index(tree_scheduler_p s)
{
    for(uint64_t i=0; i<s->n_process; i++)
    {
        if(!s->tasks[i].active)
        {
            return i;
        }
    }

    revert()
}

static node_p get_next_node(tree_scheduler_p s, node_p n)
{
    if(n->processing)
    {
        return NULL;
    }

    if(node_is_stored(n))
    {
        return n;
    }

    if(node_is_ready(n))
    {
        uint64_t mem_cost = node_estimate_memory(n, UINT64_MAX);
        uint64_t index = get_free_index(s);

        if(
            index > 0 &&
            mem_cost > 0 &&
            (
                s->total_mem_cost >= s->mem_launch ||
                s->total_mem_cost + mem_cost >= s->mem_max
            )
        ) {
            return NULL;
        }

        n->mem_cost = mem_cost;
        return n;
    }

    for(uint64_t i=0; i<2; i++)
    {
        if(n->children[i].done)
        {
            continue;
        }

        if(n->children[i].n == NULL)
        {
            n->children[i].n = node_expand(n, i);
        }

        node_p n_next = get_next_node(s, n->children[i].n);
        if(n_next)
        {
            return n_next;
        }
    }

    return NULL;
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
}

static void node_op_sizes(node_p n)
{
    assert(!node_is_leaf(n));
    assert(node_is_ready(n));

    if(n->ops_set)
    {
        return;
    }

    switch(n->type)
    {
<<<<<<< HEAD
=======
        case NODE_BIG:
        {
            uint64_t size = n->a.b.size;
            uint64_t i_0 = n->a.b.i_0;
            uint64_t remainder = n->a.b.remainder;
            uint64_t depth = n->a.b.depth;

            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, remainder, depth);

            if(split_big_res_is_stored(size, i_0, remainder, depth))
            {
                tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "already stored", i_0, remainder, depth);
                return;
            }

            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | avg " U64P(12) "B", index, pid, get_wall_time(), "joining", i_0, remainder, depth, n->mem_cost);
            TIME_SETUP
            split_big_res_join(index, size, i_0, remainder, depth);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, remainder, depth, dtime(t1));
        }
        break;

>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
        case NODE_SPAN:
        {
            uint64_t size = n->as.span.size;
            uint64_t i_0 = n->as.span.i_0;
            uint64_t span = n->as.span.span;
            uint64_t depth = n->as.span.depth;

<<<<<<< HEAD
            n->op_1 = split_span_res_op_size(size, i_0, span - 1, depth + 1, 2);
            n->op_2 = split_span_res_op_size(size, i_0 + B(span - 1), span - 1, depth + 1, 1);
        }
        break;

        case NODE_BIG:
        {
            uint64_t size = n->as.big.size;
            uint64_t i_0 = n->as.big.i_0;
            uint64_t remainder = n->as.big.remainder;
            uint64_t depth = n->as.big.depth;
            uint64_t span = stdc_bit_width(remainder) - 1;

            n->op_1 = split_span_res_op_size(size, i_0, span, depth + 1, 2);
            n->op_2 = split_big_res_op_size(size, i_0 + B(span), remainder - B(span), depth + 1, 1);
        }
        break;

=======
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, span, depth);

            if(split_span_res_is_stored(size, i_0, span, depth))
            {
                tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "already stored", i_0, span, depth);
                return;
            }

            if(span == TREE_PIECE_SIZE)
            {
                TIME_SETUP
                split_piece(i_0, span);
                TIME_END(t1)
                tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " %3s | %7.1f", index, pid, get_wall_time(), "piece", i_0, span, "", dtime(t1));
                return;
            }

            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | avg " U64P(12) "B", index, pid, get_wall_time(), "joining", i_0, span, depth, n->mem_cost);
            TIME_SETUP
            split_span_res_join(index, size, i_0, span, depth);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, span, depth, dtime(t1));
        }
        break;

>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
        default: revert()
    }
    n->ops_set = true;
}

static pid_t task_start(tree_scheduler_p s, node_p n)
{
    uint64_t index = get_free_index(s);

<<<<<<< HEAD
STRUCT(tree_scheduler)
{
    tree_task_p tasks;
    uint64_t active;
    uint64_t total_mem_cost;
    uint64_t total_threads;
    uint64_t n_process;
    uint64_t mem_launch;
    uint64_t mem_max;
};

static tree_scheduler_t tree_scheduler_create(uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    tree_scheduler_t s = {
        .tasks = calloc(n_process, sizeof(tree_task_t)),
        .active = 0,
        .total_mem_cost = 0,
        .total_threads = 0,
        .n_process = n_process,
        .mem_launch = mem_launch,
        .mem_max = mem_max
    };
    assert(s.tasks);
    return s;
}

static uint64_t get_free_index(tree_scheduler_p s)
{
    for(uint64_t i=0; i<s->n_process; i++)
    {
        if(!s->tasks[i].active)
        {
            return i;
        }
    }

    revert()
}



static uint64_t node_estimate_memory(node_p n, uint64_t threads)
{
    if(node_is_leaf(n))
    {
        return TREE_LEAF_COST_BYTES;
    }

    node_op_sizes(n);

    return num_mul_estimate_memory(n->op_1, n->op_2, UINT64_MAX, threads);
}

static bool scheduler_has_slot(tree_scheduler_p s)
{
    return s->active < s->n_process;
}

static bool scheduler_is_empty(tree_scheduler_p s)
{
    return s->active == 0;
}

static bool scheduler_has_memory(tree_scheduler_p s, uint64_t mem_cost)
{
    return s->total_mem_cost + mem_cost < s->mem_max;
}

static uint64_t scheduler_free_threads(tree_scheduler_p s)
{
    if(s->total_threads >= s->n_process)
    {
        return 0;
    }

    return s->n_process - s->total_threads;
}

static bool scheduler_has_threads(tree_scheduler_p s)
{
    return scheduler_free_threads(s) > 0;
}

static bool scheduler_can_launch(tree_scheduler_p s)
{
    return s->total_mem_cost < s->mem_launch;
}

static uint64_t node_threads(node_p n, uint64_t max_threads)
{
    if(node_is_leaf(n))
    {
        return 1;
    }

    node_op_sizes(n);
    uint64_t threads = num_mul_threads_ceiling(n->op_1, n->op_2);
    if(threads > max_threads)
    {
        threads = max_threads;
    }

    return B(stdc_bit_width(threads) - 1);
}

static bool node_is_open(node_p n)
{
    return !n->processing;
}

static uint64_t node_set_plan(node_p n, uint64_t mem_cost, uint64_t threads)
{
    n->plan = (tree_plan_t){
        .mem_cost = mem_cost,
        .threads = threads
    };
    return LAUNCH_TAKE;
}

static uint64_t node_can_launch(tree_scheduler_p s, node_p n)
{
    if(!scheduler_has_threads(s))
    {
        return LAUNCH_HALT;
    }

    if(!scheduler_can_launch(s))
    {
        return LAUNCH_HALT;
    }

    if(!node_is_open(n))
    {
        return LAUNCH_SKIP;
    }

    if(!node_is_ready(n))
    {
        return LAUNCH_SKIP;
    }

    uint64_t threads = node_threads(n, scheduler_free_threads(s));
    uint64_t mem_cost = node_estimate_memory(n, threads);
    if(scheduler_has_memory(s, mem_cost))
    {
        return node_set_plan(n, mem_cost, threads);
    }

    if(scheduler_is_empty(s))
    {
        return node_set_plan(n, mem_cost, threads);
    }

    if(node_is_leaf(n))
    {
        return LAUNCH_HALT;
    }

    return LAUNCH_SKIP;
}

static void node_process_stored(node_p n)
{
    int pid = (int)getpid();

    switch(n->type)
    {
        case NODE_BIG:
        {
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", U64(0), pid, get_wall_time(), "already stored", n->as.big.i_0, n->as.big.remainder, n->as.big.depth);
        }
        break;

        case NODE_SPAN:
        {
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", U64(0), pid, get_wall_time(), "already stored", n->as.span.i_0, n->as.span.span, n->as.span.depth);
        }
        break;

        default: revert()
    }
}

static uint64_t node_get_next(node_p *out_n, tree_scheduler_p s, node_p n)
{
    if(!node_is_open(n))
    {
        return LAUNCH_SKIP;
    }

    for(uint64_t i=0; i<2; i++)
    {
        if(n->children[i].done)
        {
            continue;
        }

        if(n->children[i].n != NULL)
        {
            continue;
        }

        n->children[i].n = node_expand(n, i);
        if(n->children[i].n->is_stored)
        {
            node_process_stored(n->children[i].n);
            n->children[i].done = true;
            free(n->children[i].n);
            n->children[i].n = NULL;
        }
    }

    uint64_t verdict = node_can_launch(s, n);
    if(verdict == LAUNCH_TAKE)
    {
        *out_n = n;
        return LAUNCH_TAKE;
    }

    if(verdict == LAUNCH_HALT)
    {
        return LAUNCH_HALT;
    }

    for(uint64_t i=0; i<2; i++)
    {
        if(n->children[i].done)
        {
            continue;
        }

        verdict = node_get_next(out_n, s, n->children[i].n);
        if(verdict != LAUNCH_SKIP)
        {
            return verdict;
        }
    }

    return LAUNCH_SKIP;
}

static void node_big_process(node_p n, uint64_t index)
{
    int pid = (int)getpid();
    uint64_t size = n->as.big.size;
    uint64_t i_0 = n->as.big.i_0;
    uint64_t remainder = n->as.big.remainder;
    uint64_t depth = n->as.big.depth;

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, remainder, depth);
    TIME_SETUP
    split_big_res_join(index, size, i_0, remainder, depth, n->plan.threads);
    TIME_END(t1)
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, remainder, depth, dtime(t1));
}

static void node_span_process(node_p n, uint64_t index)
{
    int pid = (int)getpid();
    uint64_t size = n->as.span.size;
    uint64_t i_0 = n->as.span.i_0;
    uint64_t span = n->as.span.span;
    uint64_t depth = n->as.span.depth;

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, span, depth);
    if(span == TREE_PIECE_SIZE)
    {
        TIME_SETUP
        split_piece(index, i_0, span, depth);
        TIME_END(t1)
        tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " %3s | %7.1f", index, pid, get_wall_time(), "piece", i_0, span, "", dtime(t1));
        return;
    }

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | avg " U64P(12) "B", index, pid, get_wall_time(), "joining", i_0, span, depth, n->plan.mem_cost);
    TIME_SETUP
    split_span_res_join(index, size, i_0, span, depth, n->plan.threads);
    TIME_END(t1)
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, span, depth, dtime(t1));
}

static void node_process(node_p n, uint64_t index)
{
    switch(n->type)
    {
        case NODE_BIG:
        {
            node_big_process(n, index);
        }
        break;

        case NODE_SPAN:
        {
            node_span_process(n, index);
        }
        break;

        default: revert()
    }
}

static pid_t task_start(tree_scheduler_p s, node_p n)
{
    uint64_t index = get_free_index(s);

    n->processing = true;

    pid_t pid = fork_safe();
    if(pid == 0)
    {

#ifdef LOCK_IN_PLACE
        fork_lock_processor(index);
#endif

=======
    n->processing = true;

    pid_t pid = fork_safe();
    if(pid == 0)
    {
#ifdef LOCK_IN_PLACE
        fork_lock_processor(index);
#endif
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
        node_process(n, index);
        exit(EXIT_SUCCESS);
    }

<<<<<<< HEAD
=======
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s|", index, (int)pid, get_wall_time(), "task start");

>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
    s->tasks[index] = (tree_task_t){
        .pid = pid,
        .n = n,
        .time_start = get_time(),
        .active = true
    };

<<<<<<< HEAD
    s->total_mem_cost += n->plan.mem_cost;
    s->total_threads += n->plan.threads;
    s->active++;

    // THR is this task's thread count, SUM the scheduler total including it,
    // MEM the memory it was booked at -- the only line that states a leaf's cost.
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| THR " U64P(3) " SUM " U64P(3) " MEM " U64P(12) "",
        index, (int)pid, get_wall_time(), "task start", n->plan.threads, s->total_threads, n->plan.mem_cost);
=======
    s->total_mem_cost += n->mem_cost;
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0

    return pid;
}

static uint64_t get_task_index(tree_scheduler_p s, pid_t pid)
{
    for(uint64_t i=0; i<s->n_process; i++)
    {
        if(s->tasks[i].active && s->tasks[i].pid == pid)
        {
            return i;
        }
    }

    revert()
}

static bool task_end(tree_scheduler_p s, pid_t pid)
{
    uint64_t index = get_task_index(s, pid);
    node_p n = s->tasks[index].n;
    uint64_t time_start = s->tasks[index].time_start;

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| %25s | %7.1f", index, (int)pid, get_wall_time(), "task end", "", dtime(get_time() - time_start));

    s->tasks[index].active = false;
<<<<<<< HEAD
    s->total_mem_cost -= n->plan.mem_cost;
    s->total_threads -= n->plan.threads;
    s->active--;
=======
    s->total_mem_cost -= n->mem_cost;
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0

    node_p parent = n->parent;
    if(parent == NULL)
    {
        free(n);
        return true;
    }

    for(uint64_t i=0; i<2; i++)
    {
        if(parent->children[i].n == n)
        {
            parent->children[i].done = true;
            parent->children[i].n = NULL;
            free(n);
            return false;
        }
    }

    revert()
}

<<<<<<< HEAD
static void task_check_exit(pid_t pid, int status)
{
    if(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS)
    {
        return;
    }

    if(WIFSIGNALED(status))
    {
        tprintf("[%7d][%17.6f] %-20s| signal " U64P(3) "", (int)pid, get_wall_time(), "task killed", (uint64_t)WTERMSIG(status));
    }
    else
    {
        tprintf("[%7d][%17.6f] %-20s| status " U64P(3) "", (int)pid, get_wall_time(), "task failed", (uint64_t)WEXITSTATUS(status));
    }

    TRAP("worker did not exit cleanly")
}

=======
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
static void scheduler(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    uint64_t index_max = get_index_max(size, TREE_PIECE_SIZE);
    node_p n_root = node_big_create(NULL, size, 1, index_max, 0);
<<<<<<< HEAD
    if(n_root->is_stored)
    {
        node_process_stored(n_root);
        free(n_root);
        return;
    }

    tree_scheduler_t s = tree_scheduler_create(n_process, mem_launch, mem_max);

    bool done = false;
    while(!done)
    {
        while(scheduler_has_slot(&s))
        {
            node_p n = NULL;
            if(node_get_next(&n, &s, n_root) != LAUNCH_TAKE)
=======
    tree_scheduler_t s = tree_scheduler_create(n_process, mem_launch, mem_max);

    bool done = false;
    uint64_t active = 0;
    while(!done)
    {
        while(active < n_process)
        {
            node_p n = get_next_node(&s, n_root);
            if(n == NULL)
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
            {
                break;
            }

<<<<<<< HEAD
            task_start(&s, n);
=======
            bool stored = node_is_stored(n);
            pid_t pid = task_start(&s, n);

            if(!stored)
            {
                active++;
                continue;
            }

            waitpid_safe(pid, NULL);
            done = task_end(&s, pid);
            if(done)
            {
                break;
            }
        }

        if(done)
        {
            break;
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
        }

        tprintf("              %-20s| " U64P(2) "", "active processes", s.active);
        tprintf("              %-20s| " U64P(2) "", "active threads", s.total_threads);
        int status;
        pid_t pid = waitpid_safe(0, &status);
        task_check_exit(pid, status);

        done = task_end(&s, pid);
<<<<<<< HEAD
=======
        active--;
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
    }
    free(s.tasks);
}

[[maybe_unused]]
flt_num_t pi_tree(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    long n_proc_avail = sysconf(_SC_NPROCESSORS_ONLN);
    if(n_proc_avail > 0 && n_process > (uint64_t)n_proc_avail)
    {
        n_process = (uint64_t)n_proc_avail;
    }

    tprintf("              %-20s| " U64P(10) "", "piece size", (uint64_t)TREE_PIECE_SIZE);
    tprintf("              %-20s| " U64P(10) "", "run size", get_index_max(size, TREE_PIECE_SIZE));
<<<<<<< HEAD
    tprintf("              %-20s| " U64P(10) "", "n process", n_process);
=======
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0
    tprintf("              %-20s| " U64P(10) "", "mem launch", mem_launch);
    tprintf("              %-20s| " U64P(10) "", "mem max", mem_max);
    tprintf("              %-20s| " U64P(10) "", "disk lock", (uint64_t)disk_lock_enabled());

    if(pi_is_stored(size))
    {
        tprintf("[%17.6f] %-20s|", get_wall_time(), "pi already stored");
        return pi_load(size);
    }

    scheduler(size, n_process, mem_launch, mem_max);
<<<<<<< HEAD
    tprintf("[%17.6f] %-20s|", get_wall_time(), "binary split solved");
=======
    tprintf("              %-20s|", "binary split solved");
>>>>>>> af5a3933339db3767acbb04ef0747a6cae45b2d0

    return pi_finish(size, TREE_PIECE_SIZE, n_process);
}