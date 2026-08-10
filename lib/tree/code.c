// #define LOCK_IN_PLACE

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
            }
            revert()
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
    }
    revert()
}

static bool node_is_ready(node_p n)
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
    }

    for(uint64_t i=0; i<2; i++)
    {
        if(!n->children[i].done)
        {
            return false;
        }
    }

    return true;
}

static node_p get_next_node(node_p n)
{
    if(n->processing)
    {
        return NULL;
    }

    if(node_is_ready(n))
    {
        n->processing = true;
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

        node_p n_next = get_next_node(n->children[i].n);
        if(n_next)
        {
            return n_next;
        }
    }

    return NULL;
}

// Estimates the average memory a node's join will use, from araucaria's own
// num_mul_estimate_memory. A join runs four cross multiplications between the two
// children (P1xP2, Q1xQ2, P1xR2, R1xQ2); R1xQ2 is used as the representative term,
// since R accumulates contributions from both children and so tends to be the
// largest operand of the four. Operand sizes are read from each child's
// already-stored result via split_span_res_op_size / split_big_res_op_size
// (index 2 = R, index 1 = Q).
static uint64_t node_estimate_memory(node_p n, uint64_t disk_threshold)
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
                return 0;
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
    }

    return num_mul_estimate_memory(op_1, op_2, disk_threshold);
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

            tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, "begin", i_0, remainder, depth);

            if(split_big_res_is_stored(size, i_0, remainder, depth))
            {
                tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, "already stored", i_0, remainder, depth);
                return;
            }

            uint64_t mem_avg_bytes = node_estimate_memory(n, UINT64_MAX);
            tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | avg " U64P(12) "B", index, pid, "joining", i_0, remainder, depth, mem_avg_bytes);
            TIME_SETUP
            split_big_res_join(index, size, i_0, remainder, depth);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, "joined", i_0, remainder, depth, dtime(t1));
        }
        break;

        case NODE_SPAN:
        {
            uint64_t size = n->a.s.size;
            uint64_t i_0 = n->a.s.i_0;
            uint64_t span = n->a.s.span;
            uint64_t depth = n->a.s.depth;

            tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, "begin", i_0, span, depth);

            if(split_span_res_is_stored(size, i_0, span, depth))
            {
                tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, "already stored", i_0, span, depth);
                return;
            }

            if(span == TREE_PIECE_SIZE)
            {
                TIME_SETUP
                split_piece(i_0, span);
                TIME_END(t1)
                tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " %3s | %7.1f", index, pid, "piece", i_0, span, "", dtime(t1));
                return;
            }

            uint64_t mem_avg_bytes = node_estimate_memory(n, UINT64_MAX);
            tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | avg " U64P(12) "B", index, pid, "joining", i_0, span, depth, mem_avg_bytes);
            TIME_SETUP
            split_span_res_join(index, size, i_0, span, depth);
            TIME_END(t1)
            tprintf("[" U64P(2) "][%7d] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, "joined", i_0, span, depth, dtime(t1));
        }
        break;
    }
}

STRUCT(tree_task)
{
    pid_t pid;
    node_p n;
    uint64_t time_start;
    bool active;
};

static uint64_t get_free_index(tree_task_p tasks, uint64_t n_process)
{
    for(uint64_t i=0; i<n_process; i++)
    {
        if(!tasks[i].active)
        {
            return i;
        }
    }

    revert()
}

static void task_start(tree_task_p tasks, uint64_t index, node_p n)
{
    pid_t pid = fork_safe();
    if(pid == 0)
    {
#ifdef LOCK_IN_PLACE
        fork_lock_processor(index);
#endif
        node_process(n, index);
        exit(EXIT_SUCCESS);
    }

    tprintf("[" U64P(2) "][%7d] %-20s|", index, (int)pid, "task start");

    tasks[index] = (tree_task_t){
        .pid = pid,
        .n = n,
        .time_start = get_time(),
        .active = true
    };
}

static uint64_t get_task_index(tree_task_p tasks, pid_t pid, uint64_t n_process)
{
    for(uint64_t i=0; i<n_process; i++)
    {
        if(tasks[i].active && tasks[i].pid == pid)
        {
            return i;
        }
    }

    revert()
}

static bool task_end(tree_task_p tasks, pid_t pid, uint64_t n_process)
{
    uint64_t index = get_task_index(tasks, pid, n_process);
    node_p n = tasks[index].n;
    uint64_t time_start = tasks[index].time_start;

    tprintf("[" U64P(2) "][%7d] %-20s| %25s | %7.1f", index, (int)pid, "task end", "", dtime(get_time() - time_start));

    tasks[index].active = false;

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

static void scheduler(uint64_t size, uint64_t n_process)
{
    uint64_t index_max = get_index_max(size, TREE_PIECE_SIZE);
    node_p n_root = node_big_create(NULL, size, 1, index_max, 0);
    tree_task_p tasks = calloc(n_process, sizeof(tree_task_t));
    assert(tasks);

    uint64_t active = 0;
    for(;;)
    {
        for(; active<n_process; active++)
        {
            node_p n = get_next_node(n_root);
            if(n == NULL)
            {
                break;
            }

            uint64_t index = get_free_index(tasks, n_process);
            task_start(tasks, index, n);
        }

        tprintf("              %-20s| " U64P(2) "", "active processes", active);
        pid_t pid = waitpid_safe(0, NULL);

        if(task_end(tasks, pid, n_process))
        {
            break;
        }
        active--;
    }
    free(tasks);
}

[[maybe_unused]]
flt_num_t pi_tree(uint64_t size, uint64_t n_process)
{
    long n_proc_avail = sysconf(_SC_NPROCESSORS_ONLN);
    if(n_proc_avail > 0 && n_process > (uint64_t)n_proc_avail)
    {
        n_process = (uint64_t)n_proc_avail;
    }

    tprintf("              %-20s| " U64P(10) "", "piece size", (uint64_t)TREE_PIECE_SIZE);
    tprintf("              %-20s| " U64P(10) "", "run size", get_index_max(size, TREE_PIECE_SIZE));

    if(pi_is_stored(size))
    {
        tprintf("              %-20s|", "pi already stored");
        return pi_load(size);
    }

    scheduler(size, n_process);
    tprintf("              %-20s|", "binary split solved");

    return pi_finish(size, TREE_PIECE_SIZE);
}