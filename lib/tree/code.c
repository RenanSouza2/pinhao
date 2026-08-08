#include <stdio.h>

#include "debug.h"
#include "../big/internal.h"

#include "../../mods/macros/assert.h"
#include "../../mods/macros/stdbit.h"
#include "../../mods/macros/fork.h"
#include "../../mods/macros/uint.h"
#include "../../mods/macros/time.h"

#ifdef DEBUG
#endif

#define TREE_PIECE_SIZE 22

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
    node_p n = malloc(sizeof(node_t));
    assert(n);

    if(stdc_count_ones(remainder) == 1)
    {
        uint64_t span = stdc_bit_width(remainder) - 1;
        return node_span_create(parent, size, i_0, span, depth);
    }

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
            assert(false);
        }
        break;

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
    assert(false);
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

static bool get_next_node(node_p *out_n, node_p n)
{
    if(n->processing)
    {
        return false;
    }

    if(node_is_ready(n))
    {
        n->processing = true;
        *out_n = n;
        return true;
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

        if(get_next_node(out_n, n->children[i].n))
        {
            return true;
        }
    }

    return false;
}

static void node_process(node_p n, uint64_t index)
{
    switch(n->type)
    {
        case NODE_BIG:
        {
            uint64_t size = n->a.b.size;
            uint64_t i_0 = n->a.b.i_0;
            uint64_t remainder = n->a.b.remainder;
            uint64_t depth = n->a.b.depth;

            tprintf("[" U64P() "] begin | " U64P() " " U64P() " " U64P() "", index, i_0, remainder, depth);

            if(split_big_res_is_stored(size, i_0, remainder, depth))
            {
                tprintf("[" U64P() "] already stored | " U64P() " " U64P() " " U64P() "", index, i_0, remainder, depth);
                return;
            }

            tprintf("[" U64P() "] joining | " U64P() " " U64P() " " U64P() "", index, i_0, remainder, depth);
            TIME_SETUP
            split_big_res_join(size, i_0, remainder, depth);
            TIME_END(t1)
            fprintf(stderr, "\t\t%.1f", dtime(t1));
        }
        break;

        case NODE_SPAN:
        {
            uint64_t size = n->a.s.size;
            uint64_t i_0 = n->a.s.i_0;
            uint64_t span = n->a.s.span;
            uint64_t depth = n->a.s.depth;

            tprintf("[" U64P() "] begin | " U64P() " " U64P() " " U64P() "", index, i_0, span, depth);

            if(split_span_res_is_stored(size, i_0, span, depth))
            {
                tprintf("[" U64P() "] already stored | " U64P() " " U64P() " " U64P() "", index, i_0, span, depth);
                return;
            }

            if(span == TREE_PIECE_SIZE)
            {
                split_piece(i_0, span);
                return;
            }

            tprintf("[" U64P() "] joining | " U64P() " " U64P() " " U64P() "", index, i_0, span, depth);
            TIME_SETUP
            split_span_res_join(size, i_0, span, depth);
            TIME_END(t1)
            fprintf(stderr, "\t\t%.1f", dtime(t1));
        }
        break;
    }
}

STRUCT(tree_task)
{
    pid_t pid;
    node_p n;
    uint64_t time_start;
};

static void task_start(tree_task_p tasks, uint64_t index, node_p n)
{
    pid_t pid = fork_safe();
    if(pid == 0)
    {
        node_process(n, index);
        exit(EXIT_SUCCESS);
    }

    tprintf("[" U64P() "] task start", index);

    tasks[index] = (tree_task_t){
        .pid = pid,
        .n = n,
        .time_start = get_time()
    };
}

static uint64_t get_task_index(tree_task_p tasks, pid_t pid, uint64_t max)
{
    for(uint64_t i=0; i<max; i++)
    {
        if(tasks[i].pid == pid)
        {
            return i;
        }
    }

    assert(false);
}

static bool task_end(tree_task_p tasks, pid_t pid, uint64_t max)
{
    uint64_t index = get_task_index(tasks, pid, max);
    node_p n = tasks[index].n;
    uint64_t time_start = tasks[index].time_start;

    tprintf("[" U64P() "] task end", index);
    fprintf(stderr, "\t\t%.1f", dtime(get_time() - time_start));

    if(index < max - 1)
    {
        tasks[index] = tasks[max - 1];
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

    assert(false);
}

static void scheduler(uint64_t size, uint64_t n_process)
{
    uint64_t index_max = get_index_max(size, TREE_PIECE_SIZE);

    node_p n_root = node_big_create(NULL, size, 1, index_max, 0);

    tree_task_p tasks = malloc(n_process * sizeof(tree_task_t));
    assert(tasks);

    uint64_t i = 0;

    for(;;)
    {
        for(; i<n_process; i++)
        {
            node_p n;
            if(!get_next_node(&n, n_root))
            {
                break;
            }

            task_start(tasks, i, n);
        }

        pid_t pid = waitpid_safe(0, NULL);

        if(task_end(tasks, pid, i))
        {
            break;
        }
        i--;
    }

    free(tasks);
}

[[maybe_unused]]
flt_num_t pi_tree(uint64_t size, uint64_t n_process)
{
    if(pi_is_stored(size))
    {
        tprintf("pi already stored");
        return pi_load(size);
    }

    scheduler(size, n_process);
    tprintf("binary split solved");

    return pi_finish(size, TREE_PIECE_SIZE);
}