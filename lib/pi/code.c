#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/U64.h"
#include "../../mods/number/lib/fix/header.h"
#include "../../mods/number/lib/sig/header.h"
#include "../../mods/number/lib/num/struct.h"

#include "../jumpstart/header.h"
#include "../junc/header.h"
#include "../queue/header.h"
#include "../pear/header.h"



#ifdef DEBUG
#endif



STRUCT(thread_a_args)
{
    uint64_t layer_count;
    uint64_t index_0;
    uint64_t i_max;

    uint64_t id;
    junc_p junc_a_b;
    fix_num_t a0;
};

handler_p thread_a(handler_p _args)
{
    thread_a_args_p args = _args;

    uint64_t layer_count = args->layer_count;
    uint64_t index_0 = args->index_0;
    uint64_t i_max = args->i_max;

    uint64_t id = args->id;
    junc_p junc_a_b = args->junc_a_b;
    fix_num_t fix_a = args->a0;

    for(uint64_t i=0; i<i_max; i++)
    {
        uint64_t index = index_0 + layer_count * i + id;

        sig_num_t sig_1 = sig_num_wrap((int64_t)2 * index - 3);
        sig_num_t sig_2 = sig_num_wrap((int64_t)index);
        for(uint64_t k=1; k<layer_count; k++)
        {
            sig_1 = sig_num_mul(sig_1, sig_num_wrap((int64_t)2 * (index - k) - 3));
            sig_2 = sig_num_mul(sig_2, sig_num_wrap((int64_t)(index - k)));
        }

        fix_a = fix_num_mul_sig(fix_a, sig_1);
        fix_a = fix_num_shr(fix_a, 3 * layer_count);
        fix_a = fix_num_div_sig(fix_a, sig_2);

        fix_num_t fix_a_send = fix_num_copy(fix_a);
        junc_send(junc_a_b, &fix_a_send, NULL);
    }
    fix_num_free(fix_a);

    return NULL;
}

STRUCT(thread_b_args)
{
    uint64_t layer_count;
    uint64_t index_0;
    uint64_t i_max;

    uint64_t id;
    queue_p queue_a_b;
    queue_p queue_b_c;
};

handler_p thread_b(handler_p _args)
{
    thread_b_args_p args = _args;

    uint64_t layer_count = args->layer_count;
    uint64_t index_0 = args->index_0;
    uint64_t i_max = args->i_max;

    uint64_t id = args->id;
    queue_p queue_a_b = args->queue_a_b;
    queue_p queue_b_c = args->queue_b_c;

    for(uint64_t i=0; i<i_max; i++)
    {
        uint64_t index = index_0 + layer_count * i + id;

        fix_num_t fix_a;
        queue_recv(queue_a_b, &fix_a, NULL);

        fix_num_t fix_b;
        fix_b = fix_num_mul_sig(fix_a, sig_num_wrap((int64_t)1 - 2 * index));
        fix_b = fix_num_div_sig(fix_b, sig_num_wrap((int64_t)4 * index + 2));

        queue_send(queue_b_c, &fix_b, NULL);
    }

    return NULL;
}

STRUCT(thread_c_args)
{
    uint64_t pos;
    uint64_t i_max;
    
    junc_p junc_b_c;
    fix_num_t res;

    uint64_t g_id;
};

handler_p thread_c(handler_p _args)
{
    thread_c_args_p args = _args;

    uint64_t pos = args->pos;
    uint64_t i_max = args->i_max;

    junc_p junc_b_c = args->junc_b_c;

    // uint64_t g_id = args->g_id;

    fix_num_t fix_c = fix_num_wrap(0, pos);
    for(uint64_t i=0; i<i_max; i++)
    {
        if(i%1000 == 0)
            fprintf(stderr, "\nprogess: %lu / %lu", i / 1000, i_max / 1000);

        for(uint64_t j=0; j<junc_b_c->total; j++)
        {
            fix_num_t fix_b;
            junc_recv(junc_b_c, &fix_b, NULL);
            fix_c = fix_num_add(fix_c, fix_b);
        }
    }

    args->res = fix_c;
    return NULL;
}



void pi_queue_res_free(handler_p h, uint64_t res_size)
{
    uint64_t batch_size = res_size / sizeof(fix_num_t);
    for(uint64_t i=0; i<batch_size; i++)
        fix_num_free(((fix_num_p)h)[i]);
}

STRUCT(layer)
{
    uint64_t layer_b_count;
    junc_t junc_a_b;

    thread_a_args_t args_a;
    thread_b_args_p args_b;

    pthread_t tid_a;
    pthread_t *tid_b;

    fix_num_t a0;
};

STRUCT(group)
{
    uint64_t layer_count;
    layer_p layers;

    junc_t junc_b_c;
    thread_c_args_t args_c;
    pthread_t tid_c;
};

group_p group_create(uint64_t layer_count, uint64_t layer_b_count)
{
    layer_p layers = malloc(layer_count * sizeof(layer_t));
    assert(layers);

    for(uint64_t i=0; i<layer_count; i++)
    {
        layers[i].layer_b_count = layer_b_count;

        layers[i].args_b = malloc(layer_b_count * sizeof(thread_b_args_t));
        assert(layers[i].args_b);

        layers[i].tid_b = malloc(layer_b_count * sizeof(pthread_t));
        assert(layers[i].tid_b);
    }

    group_p g = malloc(sizeof(group_t));
    assert(g);
    *g = (group_t)
    {
        .layer_count = layer_count,
        .layers = layers,
    };
    return g;
}

void group_free(group_p g)
{
    for(uint64_t i=0; i<g->layer_count; i++)
    {
        free(g->layers[i].args_b);
        free(g->layers[i].tid_b);
    }

    free(g->layers);

    free(g);
}

group_p group_launch(
    uint64_t g_id,
    uint64_t size,
    uint64_t layer_count,
    uint64_t layer_b_count,
    uint64_t index_0,
    uint64_t index_max,
    uint64_t thread_0
)
{
    uint64_t queue_size = 5;

    assert((index_max - index_0) % (layer_count * layer_b_count) == 0);

    group_p g = group_create(layer_count, layer_b_count);
    g->junc_b_c = junc_init(layer_count * layer_b_count, queue_size, sizeof(fix_num_t), pi_queue_res_free);

    fix_num_t a0[layer_count];
    fix_num_t fix_a = jumpstart_thread(size, layer_count, index_0 - layer_count, thread_0);
    a0[0] = fix_num_copy(fix_a);
    for(uint64_t i=1; i<layer_count; i++)
    {
        uint64_t index = i + index_0 - layer_count;
        fix_a = fix_num_mul_sig(fix_a, sig_num_wrap((int64_t)2 * index - 3));
        fix_a = fix_num_div_sig(fix_a, sig_num_wrap((int64_t)8 * index));
        a0[i] = fix_num_copy(fix_a);
    }
    fix_num_free(fix_a);

    for(uint64_t i=0; i<layer_count; i++)
    {
        g->layers[i].junc_a_b = junc_init(layer_b_count, queue_size, sizeof(fix_num_t), pi_queue_res_free);
        g->layers[i].args_a = (thread_a_args_t)
        {
            .layer_count = layer_count,
            .index_0 = index_0,
            .i_max = (index_max - index_0) / layer_count,

            .id = i,
            .junc_a_b = &g->layers[i].junc_a_b,
            .a0 = a0[i]
        };
        g->layers[i].tid_a = pthread_create_treat(thread_a, &g->layers[i].args_a);
        pthread_lock(g->layers[i].tid_a, thread_0 + 2 * i);

        for(uint64_t j=0; j<layer_b_count; j++)
        {
            g->layers[i].args_b[j] = (thread_b_args_t)
            {
                .layer_count = layer_count * layer_b_count,
                .index_0 = index_0,
                .i_max = (index_max - index_0) / (layer_count * layer_b_count),

                .id = i + layer_count * j,
                .queue_a_b = &g->layers[i].junc_a_b.queues[j],
                .queue_b_c = &g->junc_b_c.queues[i + layer_count * j]
            };
            g->layers[i].tid_b[j] = pthread_create_treat(thread_b, &g->layers[i].args_b[j]);
        }
        pthread_lock(g->layers[i].tid_b[0], thread_0 + 2 * i + 1);
    }

    g->args_c = (thread_c_args_t)
    {
        .pos = size - 1,
        .i_max = (index_max - index_0) / (layer_count * layer_b_count),

        .junc_b_c = &g->junc_b_c,

        .g_id = g_id
    };
    g->tid_c = pthread_create_treat(thread_c, &g->args_c);
    pthread_lock(g->tid_c, thread_0 + 6);
    return g;
}

fix_num_t group_join(group_p g)
{
    pthread_join_treat(g->tid_c);
    for(uint64_t i=0; i<g->layer_count; i++)
    {
        for(uint64_t j=0; j<g->layers[i].layer_b_count; j++)
        {
            assert(queue_get_occupancy(&g->layers[i].junc_a_b.queues[j]) == 0);
            assert(queue_get_occupancy(&g->junc_b_c.queues[i + g->layer_count * j]) == 0);
        }

        pthread_join_treat(g->layers[i].tid_a);
        for(uint64_t j=0; j<g->layers[i].layer_b_count; j++)
            pthread_join_treat(g->layers[i].tid_b[j]);

        junc_free(&g->layers[i].junc_a_b);
    }
    junc_free(&g->junc_b_c);


    fix_num_t fix_c = g->args_c.res;
    group_free(g);
    return fix_c;
}



fix_num_t pi_0(uint64_t size, uint64_t index_max)
{
    fix_num_t fix_a = fix_num_wrap(6, size - 1);
    fix_num_t fix_pi = fix_num_wrap(3, size - 1);
    for(uint64_t i=1; i<index_max; i++)
    {
        fix_a = fix_num_mul_sig(fix_a, sig_num_wrap((int64_t)2 * i - 3));
        fix_a = fix_num_div_sig(fix_a, sig_num_wrap((int64_t)8 * i));

        fix_num_t fix_b = fix_num_copy(fix_a);
        fix_b = fix_num_mul_sig(fix_b, sig_num_wrap((int64_t)1 - 2 * i));
        fix_b = fix_num_div_sig(fix_b, sig_num_wrap((int64_t)4 * i + 2));

        fix_pi = fix_num_add(fix_pi, fix_b);
    }
    fix_num_free(fix_a);
    return fix_pi;
}

fix_num_t pi_threads(uint64_t size)
{
    uint64_t layer_count = 3;
    uint64_t layer_b_count = 2;
    uint64_t cut = 45;

    uint64_t index_max = 32 * size + 4;
    uint64_t index_mid = cut * index_max / 100;
    index_mid -= (index_mid - layer_count) % (layer_count * layer_b_count);
    index_max += layer_count - (index_max - index_mid) % layer_count;

    group_p g_1 = group_launch(0, size, layer_count, layer_b_count, layer_count, index_mid, 0);
    group_p g_2 = group_launch(1, size, layer_count, 1, index_mid, index_max, 8);

    fix_num_t fix_pi = pi_0(size, layer_count);
    fix_pi = fix_num_add(fix_pi, group_join(g_1));
    fix_pi = fix_num_add(fix_pi, group_join(g_2));
    return fix_pi;
}
