// #define LOCK_IN_PLACE

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

#ifdef DEBUG
#endif

#define TREE_PIECE_SIZE 24

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

STRUCT(node)
{
    node_p parent;
    uint64_t type;
    bool processing;
    uint64_t mem_cost;
    uint64_t threads;
    union {
        big_t b;
        span_t s;
    } a;
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
        .threads = 1,
        .a.s = (span_t){
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
        .threads = 1,
        .a.b = (big_t){
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
            uint64_t size = n->a.b.size;
            uint64_t i_0 = n->a.b.i_0;
            uint64_t remainder = n->a.b.remainder;
            uint64_t span = stdc_bit_width(remainder) - 1;
            uint64_t depth = n->a.b.depth;

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
            uint64_t size = n->a.s.size;
            uint64_t i_0 = n->a.s.i_0;
            uint64_t span = n->a.s.span;
            uint64_t depth = n->a.s.depth;
            uint64_t offset = index * B(span - 1);
            return node_span_create(n, size, i_0 + offset, span - 1, depth + 1);
        }

        default: revert()
    }
}

static bool node_is_stored(node_p n)
{
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
    for(uint64_t i=0; i<2; i++)
    {
        if(!n->children[i].done)
        {
            return false;
        }
    }

    return true;
}

// Operand sizes of the multiplication a node's join is estimated by. A join runs
// four cross multiplications between the two children (P1xP2, Q1xQ2, P1xR2,
// R1xQ2); R1xQ2 is used as the representative term, since R accumulates
// contributions from both children and so tends to be the largest operand of the
// four. Sizes are read from each child's already-stored result via
// split_span_res_op_size / split_big_res_op_size (index 2 = R, index 1 = Q).
// Returns false for a piece, which computes in process and has no join.
static bool node_op_sizes(node_p n, uint64_t *out_op_1, uint64_t *out_op_2)
{
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
                return false;
            }

            *out_op_1 = split_span_res_op_size(size, i_0, span - 1, depth + 1, 2);
            *out_op_2 = split_span_res_op_size(size, i_0 + B(span - 1), span - 1, depth + 1, 1);
        }
        break;

        case NODE_BIG:
        {
            uint64_t size = n->a.b.size;
            uint64_t i_0 = n->a.b.i_0;
            uint64_t remainder = n->a.b.remainder;
            uint64_t depth = n->a.b.depth;
            uint64_t span = stdc_bit_width(remainder) - 1;

            *out_op_1 = split_span_res_op_size(size, i_0, span, depth + 1, 2);
            *out_op_2 = split_big_res_op_size(size, i_0 + B(span), remainder - B(span), depth + 1, 1);
        }
        break;

        default: revert()
    }

    return true;
}

// How many series terms a node covers -- what decides both its place in the tree
// and how wide its level is.
static uint64_t node_terms(node_p n)
{
    switch(n->type)
    {
        case NODE_BIG:
        {
            return n->a.b.remainder;
        }

        case NODE_SPAN:
        {
            return B(n->a.s.span);
        }

        default: revert()
    }
}

// Estimates the average memory a node's join will use, from araucaria's own
// num_mul_estimate_memory, for a join that will be given `threads` threads.
static uint64_t node_estimate_memory(node_p n, uint64_t disk_threshold_bytes, uint64_t threads)
{
    uint64_t op_1;
    uint64_t op_2;
    if(!node_op_sizes(n, &op_1, &op_2))
    {
        // return 0;
        return 1;
        // return UINT64_MAX / 4;
    }

    return num_mul_estimate_memory(op_1, op_2, disk_threshold_bytes, threads);
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
    uint64_t index_max;
};

static tree_scheduler_t tree_scheduler_create(uint64_t n_process, uint64_t mem_launch, uint64_t mem_max, uint64_t index_max)
{
    tree_scheduler_t s = {
        .tasks = calloc(n_process, sizeof(tree_task_t)),
        .total_mem_cost = 0,
        .n_process = n_process,
        .mem_launch = mem_launch,
        .mem_max = mem_max,
        .index_max = index_max
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

// How many threads a node's join should ask for.
//
// Two limits independently decide how many joins can be in flight at once, and
// whichever is tighter leaves the cores that threads exist to pick up:
//
//   - tree width. A level of span `span` holds about index_max / 2^span nodes,
//     and the last levels above the pieces are eight, four, two and finally one
//     node wide, so process parallelism runs out however many slots are free.
//   - memory. A join's working set is several times its operands, so towards the
//     root a single one fills the launch budget on its own. A node that can only
//     ever run solo should take the whole machine no matter how wide its level
//     nominally is -- that case is invisible to tree width alone.
//
// Threads fill exactly the gap the tighter limit leaves, and only that gap: at
// an equal core budget processes and threads deliver the same throughput to
// within a few percent, so a level that still offers a node per process stays at
// one thread and costs nothing. The request is then clamped to what araucaria
// would clamp it to anyway (num_mul_threads_ceiling, which wants a minimum
// number of limbs per thread). No cap beyond that is needed: n_process /
// concurrent already sums to n_process across the tasks in flight, and a join
// that does run alone genuinely wants every hardware thread -- measured, a solo
// multiplication of 134M-limb operands takes 33.5s on eight threads and 31.1s on
// sixteen, so clamping to the physical core count would leave that on the table.
//
// The result is self-limiting rather than merely tuned: the scheduler admits
// tasks against the same memory estimate this reads, and simultaneously runnable
// nodes cover disjoint index ranges, so the threads of the tasks actually
// running sum to about n_process instead of oversubscribing the machine.
static uint64_t node_threads(tree_scheduler_p s, node_p n)
{
    uint64_t op_1;
    uint64_t op_2;
    if(!node_op_sizes(n, &op_1, &op_2))
    {
        return 1;
    }

    uint64_t width = s->index_max / node_terms(n);

    // Estimated at one thread on purpose: measured peak RSS is the same at one
    // thread and at sixteen, so the count a join ends up with does not move the
    // figure enough to be worth resolving the circularity between the two.
    uint64_t mem_cost = node_estimate_memory(n, UINT64_MAX, 1);
    uint64_t fit = mem_cost ? s->mem_launch / mem_cost : s->n_process;

    uint64_t concurrent = width < fit ? width : fit;
    if(concurrent > s->n_process)
    {
        concurrent = s->n_process;
    }

    if(concurrent == 0)
    {
        concurrent = 1;
    }

    uint64_t threads = s->n_process / concurrent;

    uint64_t ceiling = num_mul_threads_ceiling(op_1, op_2);
    if(threads > ceiling)
    {
        threads = ceiling;
    }

    return threads ? threads : 1;
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
        uint64_t threads = node_threads(s, n);
        uint64_t mem_cost = node_estimate_memory(n, UINT64_MAX, threads);
        uint64_t index = get_free_index(s);

        // Two zones. New work is admitted only while usage is still below
        // mem_launch; a task admitted there is allowed to overshoot into the
        // mem_launch..mem_max band, but nothing may push usage past mem_max.
        // Once usage sits in the band the tree stops launching until a task
        // ends. index 0 means no other task is running, so that one always
        // launches — the tree must keep moving whatever its estimate says.
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
        n->threads = threads;
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
}

static void node_process(node_p n, uint64_t index)
{
    int pid = (int)getpid();

    switch(n->type)
    {
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
            split_big_res_join(index, size, i_0, remainder, depth, n->threads);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, remainder, depth, dtime(t1));
        }
        break;

        case NODE_SPAN:
        {
            uint64_t size = n->a.s.size;
            uint64_t i_0 = n->a.s.i_0;
            uint64_t span = n->a.s.span;
            uint64_t depth = n->a.s.depth;

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
            split_span_res_join(index, size, i_0, span, depth, n->threads);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, span, depth, dtime(t1));
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
        node_process(n, index);
        exit(EXIT_SUCCESS);
    }

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s|", index, (int)pid, get_wall_time(), "task start");

    s->tasks[index] = (tree_task_t){
        .pid = pid,
        .n = n,
        .time_start = get_time(),
        .active = true
    };

    s->total_mem_cost += n->mem_cost;

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
    s->total_mem_cost -= n->mem_cost;

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

static void scheduler(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    uint64_t index_max = get_index_max(size, TREE_PIECE_SIZE);
    node_p n_root = node_big_create(NULL, size, 1, index_max, 0);
    tree_scheduler_t s = tree_scheduler_create(n_process, mem_launch, mem_max, index_max);

    bool done = false;
    uint64_t active = 0;
    while(!done)
    {
        while(active < n_process)
        {
            node_p n = get_next_node(&s, n_root);
            if(n == NULL)
            {
                break;
            }

            // Check before forking: once the child runs it may create the very
            // file this asks about, so a check after task_start could race it.
            bool stored = node_is_stored(n);
            pid_t pid = task_start(&s, n);

            if(!stored)
            {
                active++;
                continue;
            }

            // A stored node has nothing to compute, so its task exits at once.
            // Reap that pid here instead of falling through to the wait below,
            // which frees the index and lets the next node take it on the very
            // next iteration. A no-op never holds a slot or counts as active.
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
        }

        tprintf("              %-20s| " U64P(2) "", "active processes", active);
        pid_t pid = waitpid_safe(0, NULL);

        done = task_end(&s, pid);
        active--;
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
    tprintf("              %-20s| " U64P(10) "", "mem launch", mem_launch);
    tprintf("              %-20s| " U64P(10) "", "mem max", mem_max);
    tprintf("              %-20s| " U64P(10) "", "disk lock", (uint64_t)disk_lock_enabled());

    if(pi_is_stored(size))
    {
        tprintf("              %-20s|", "pi already stored");
        return pi_load(size);
    }

    scheduler(size, n_process, mem_launch, mem_max);
    tprintf("              %-20s|", "binary split solved");

    // pi_finish runs after the tree is solved, alone, so it takes the machine.
    return pi_finish(size, TREE_PIECE_SIZE, n_process);
}