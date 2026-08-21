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
#define TREE_LEAF_COST_BYTES (U64(512) * 1024 * 1024)

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

// What node_can_launch decided about a node.
STRUCT(tree_plan)
{
    uint64_t mem_cost;
    uint64_t threads;
    bool is_noop;
};

STRUCT(node)
{
    node_p parent;
    uint64_t type;
    bool processing;
    tree_plan_t plan;
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
    switch(n->type)
    {
        case NODE_BIG:
        {
            if(split_big_res_is_stored(n->as.big.size, n->as.big.i_0, n->as.big.remainder, n->as.big.depth))
            {
                return true;
            }
        }
        break;

        case NODE_SPAN:
        {
            if(split_span_res_is_stored(n->as.span.size, n->as.span.i_0, n->as.span.span, n->as.span.depth))
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

static bool node_is_leaf(node_p n)
{
    return n->type == NODE_SPAN && n->as.span.span == TREE_PIECE_SIZE;
}

static void node_op_sizes(uint64_t *out_op_1, uint64_t *out_op_2, node_p n)
{
    assert(!node_is_leaf(n));

    switch(n->type)
    {
        case NODE_SPAN:
        {
            uint64_t size = n->as.span.size;
            uint64_t i_0 = n->as.span.i_0;
            uint64_t span = n->as.span.span;
            uint64_t depth = n->as.span.depth;

            *out_op_1 = split_span_res_op_size(size, i_0, span - 1, depth + 1, 2);
            *out_op_2 = split_span_res_op_size(size, i_0 + B(span - 1), span - 1, depth + 1, 1);
        }
        break;

        case NODE_BIG:
        {
            uint64_t size = n->as.big.size;
            uint64_t i_0 = n->as.big.i_0;
            uint64_t remainder = n->as.big.remainder;
            uint64_t depth = n->as.big.depth;
            uint64_t span = stdc_bit_width(remainder) - 1;

            *out_op_1 = split_span_res_op_size(size, i_0, span, depth + 1, 2);
            *out_op_2 = split_big_res_op_size(size, i_0 + B(span), remainder - B(span), depth + 1, 1);
        }
        break;

        default: revert()
    }
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



static uint64_t node_estimate_memory(node_p n)
{
    if(node_is_leaf(n))
    {
        return TREE_LEAF_COST_BYTES;
    }

    uint64_t op_1;
    uint64_t op_2;
    node_op_sizes(&op_1, &op_2, n);

    return num_mul_estimate_memory(op_1, op_2, UINT64_MAX, 1);
}

static bool scheduler_has_slot(tree_scheduler_p s)
{
    return s->active < s->n_process;
}

static bool scheduler_at_first_slot(tree_scheduler_p s)
{
    return !s->tasks[0].active;
}

static bool scheduler_has_memory(tree_scheduler_p s, uint64_t mem_cost)
{
    if(s->total_mem_cost >= s->mem_launch)
    {
        return false;
    }

    if(s->total_mem_cost + mem_cost >= s->mem_max)
    {
        return false;
    }

    return true;
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

static uint64_t node_threads(node_p n, uint64_t max_threads)
{
    if(node_is_leaf(n))
    {
        return 1;
    }

    uint64_t op_1;
    uint64_t op_2;
    node_op_sizes(&op_1, &op_2, n);

    uint64_t threads = num_mul_threads_ceiling(op_1, op_2);
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

static bool node_set_plan(node_p n, uint64_t mem_cost, uint64_t threads)
{
    n->plan = (tree_plan_t){
        .mem_cost = mem_cost,
        .threads = threads,
        .is_noop = false
    };
    return true;
}

static bool node_can_launch(tree_scheduler_p s, node_p n)
{
    if(!node_is_open(n))
    {
        return false;
    }

    if(node_is_stored(n))
    {
        n->plan = (tree_plan_t){
            .is_noop = true
        };
        return true;
    }

    if(!scheduler_has_threads(s))
    {
        return false;
    }

    if(!node_is_ready(n))
    {
        return false;
    }

    uint64_t mem_cost = node_estimate_memory(n);
    if(scheduler_has_memory(s, mem_cost))
    {
        uint64_t threads = node_threads(n, scheduler_free_threads(s));
        return node_set_plan(n, mem_cost, threads);
    }

    if(!scheduler_at_first_slot(s))
    {
        return false;
    }

    uint64_t threads = node_threads(n, s->n_process);
    return node_set_plan(n, mem_cost, threads);
}

// Walks the tree for a node the policy will take, deepest-first and child 0
// before child 1. Descends into a node only while it is open.
static node_p get_next_node(tree_scheduler_p s, node_p n)
{
    if(node_can_launch(s, n))
    {
        return n;
    }

    if(!node_is_open(n))
    {
        return NULL;
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
            uint64_t size = n->as.big.size;
            uint64_t i_0 = n->as.big.i_0;
            uint64_t remainder = n->as.big.remainder;
            uint64_t depth = n->as.big.depth;

            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, remainder, depth);

            if(split_big_res_is_stored(size, i_0, remainder, depth))
            {
                tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "already stored", i_0, remainder, depth);
                return;
            }

            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | avg " U64P(12) "B", index, pid, get_wall_time(), "joining", i_0, remainder, depth, n->plan.mem_cost);
            TIME_SETUP
            split_big_res_join(index, size, i_0, remainder, depth, n->plan.threads);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, remainder, depth, dtime(t1));
        }
        break;

        case NODE_SPAN:
        {
            uint64_t size = n->as.span.size;
            uint64_t i_0 = n->as.span.i_0;
            uint64_t span = n->as.span.span;
            uint64_t depth = n->as.span.depth;

            tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, span, depth);

            if(split_span_res_is_stored(size, i_0, span, depth))
            {
                tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "already stored", i_0, span, depth);
                return;
            }

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

    s->tasks[index] = (tree_task_t){
        .pid = pid,
        .n = n,
        .time_start = get_time(),
        .active = true
    };

    s->total_mem_cost += n->plan.mem_cost;
    s->total_threads += n->plan.threads;
    if(!n->plan.is_noop)
    {
        s->active++;
    }

    // THR is this task's thread count, SUM the scheduler total including it,
    // MEM the memory it was booked at -- the only line that states a leaf's cost.
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| THR " U64P(3) " SUM " U64P(3) " MEM " U64P(12) "",
        index, (int)pid, get_wall_time(), "task start", n->plan.threads, s->total_threads, n->plan.mem_cost);

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
    s->total_mem_cost -= n->plan.mem_cost;
    s->total_threads -= n->plan.threads;
    if(!n->plan.is_noop)
    {
        s->active--;
    }

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
    tree_scheduler_t s = tree_scheduler_create(n_process, mem_launch, mem_max);

    bool done = false;
    while(!done)
    {
        while(scheduler_has_slot(&s))
        {
            node_p n = get_next_node(&s, n_root);
            if(n == NULL)
            {
                break;
            }

            bool is_noop = n->plan.is_noop;
            pid_t pid = task_start(&s, n);

            if(!is_noop)
            {
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
        }

        tprintf("              %-20s| " U64P(2) "", "active processes", s.active);
        tprintf("              %-20s| " U64P(2) "", "active threads", s.total_threads);
        pid_t pid = waitpid_safe(0, NULL);

        done = task_end(&s, pid);
    }
    free(s.tasks);
}

[[maybe_unused]]
flt_num_t pi_tree(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    // Logged before the stored-result check: that path returns without reaching
    // the scheduler.
    tprintf("              %-20s| " U64P(10) "", "piece size", (uint64_t)TREE_PIECE_SIZE);
    tprintf("              %-20s| " U64P(10) "", "run size", get_index_max(size, TREE_PIECE_SIZE));

    if(pi_is_stored(size))
    {
        tprintf("              %-20s|", "pi already stored");
        return pi_load(size);
    }

    // n_process bounds both processes and threads.
    tprintf("              %-20s| " U64P(10) "", "n process", n_process);
    tprintf("              %-20s| " U64P(10) "", "mem launch", mem_launch);
    tprintf("              %-20s| " U64P(10) "", "mem max", mem_max);
    tprintf("              %-20s| " U64P(10) "", "disk lock", (uint64_t)disk_lock_enabled());

    scheduler(size, n_process, mem_launch, mem_max);
    tprintf("              %-20s|", "binary split solved");

    return pi_finish(size, TREE_PIECE_SIZE, n_process);
}