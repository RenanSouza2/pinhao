#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/U64.h"
#include "../../mods/number/lib/fix/header.h"
#include "../../mods/number/lib/float/header.h"
#include "../../mods/number/lib/sig/header.h"
#include "../../mods/number/lib/num/struct.h"

#include "../junc/header.h"
#include "../queue/header.h"
#include "../pear/header.h"



#ifdef DEBUG
#endif



STRUCT(thread_a_args)
{
    uint64_t size;
    queue_p queue_a_b;
    uint64_t id;
    uint64_t layers;
    uint64_t batch_size;
    fix_num_t a0;
    bool volatile keep_going;
};

handler_p thread_a(handler_p _args)
{
    thread_a_args_p args = _args;
    fix_num_t fix_a = args->a0;
    uint64_t jump = args->layers * args->batch_size;
    fix_num_t fix_a_batch[args->batch_size];
    uint64_t del = 0;
    for(uint64_t i = args->id + args->layers; args->keep_going; i += jump)
    {
        del++;
        for(uint64_t j=0; j<args->batch_size; j++)
        {
            uint64_t index = i + args->layers * j;

            sig_num_t sig_a = sig_num_wrap((int64_t)2 * index - 3);
            sig_num_t sig_b = sig_num_wrap((int64_t)index);
            for(uint64_t k=1; k<args->layers; k++)
            {
                sig_a = sig_num_mul(sig_a, sig_num_wrap((int64_t)2 * (index - k) - 3));
                sig_b = sig_num_mul(sig_b, sig_num_wrap((int64_t)(index - k)));
            }

            fix_a = fix_num_mul_sig(fix_a, sig_a);
            fix_a = fix_num_shr(fix_a, 3 * args->layers);
            fix_a = fix_num_div_sig(fix_a, sig_b);

            fix_a_batch[j] = fix_num_copy(fix_a);
        }

        queue_post(args->queue_a_b, &fix_a_batch, NULL);
    }
    fix_num_free(fix_a);

    return NULL;
}

STRUCT(thread_b_args)
{
    uint64_t size;
    queue_p queue_a_b;
    queue_p queue_b_pi;
    uint64_t id;
    uint64_t layers;
    uint64_t batch_size;
    bool volatile keep_going;
};

handler_p thread_b(handler_p _args)
{
    thread_b_args_p args = _args;
    uint64_t jump = args->layers * args->batch_size;
    fix_num_t fix_a_batch[args->batch_size], fix_b_batch[args->batch_size];
    for(uint64_t i = args->id + args->layers; args->keep_going; i += jump)
    {
        queue_get(args->queue_a_b, &fix_a_batch, NULL);
        
        if(!args->keep_going)
        {
            for(uint64_t j=0; j<args->batch_size; j++)
                fix_num_free(fix_a_batch[j]);

            break;
        }

        for(uint64_t j=0; j<args->batch_size; j++)
        {
            uint64_t index = i + args->layers * j;

            fix_num_t fix_b = fix_num_mul_sig(fix_a_batch[j], sig_num_wrap((int64_t)1 - 2 * index));
            fix_b = fix_num_div_sig(fix_b, sig_num_wrap((int64_t)4 * index + 2));
            fix_b_batch[j] = fix_b;
        }

        queue_post(args->queue_b_pi, &fix_b_batch, NULL);
    }

    return NULL;
}

STRUCT(thread_pi_args)
{
    uint64_t size;
    junc_p junc_b_pi;
    uint64_t layers;
    uint64_t batch_size;
    fix_num_t pi0;
    fix_num_t res;
    bool volatile is_idle;
    uint64_t volatile state;
    uint64_t volatile total;
};

handler_p thread_pi(handler_p _args)
{
    thread_pi_args_p args = _args;
    fix_num_t fix_pi = args->pi0;
    fix_num_t fix_b_batch[args->batch_size];
    for(uint64_t i=1; ; i++)
    {
        args->total++;
    
        fix_num_t fix_b = fix_num_wrap(0, args->size);
        for(uint64_t j=0; j<args->layers; j++)
        {
            args->state = 1;
            junc_get(args->junc_b_pi, &fix_b_batch, &args->is_idle);
            args->state = 2;
            for(uint64_t k=0; k<args->batch_size; k++)
                fix_b = fix_num_add(fix_b, fix_b_batch[k]);
        }

        args->state = 3;
        uint64_t tam = 100;
        if(i%(tam) == 0)
        {
            uint64_t done = args->size - fix_b.sig.num->count;
            fprintf(stderr, "\npgr: %lu / %lu", done / tam, args->size / tam);
        }
        
        if(fix_num_is_zero(fix_b))
        {
            fix_num_free(fix_b);
            break;
        }
        
        args->state = 4;
        fix_pi = fix_num_add(fix_pi, fix_b);
    }

    args->res = fix_pi;
    return NULL;
}



void pi_queue_res_free(handler_p h, uint64_t res_size)
{
    uint64_t batch_size = res_size / sizeof(fix_num_t);
    for(uint64_t i=0; i<batch_size; i++)
        fix_num_free(((fix_num_p)h)[i]);
}

void pi_threads(uint64_t size, uint64_t thread_0)
{
    uint64_t layers = 3;
    uint64_t queue_size = 5;
    uint64_t batch_size = 20;

    thread_a_args_t args_a[layers];
    thread_b_args_t args_b[layers];

    queue_t queue_a_b[layers];

    junc_t junc_b_pi = junc_init(layers, queue_size, batch_size * sizeof(fix_num_t), pi_queue_res_free);

    pthread_t pid_a[layers];
    pthread_t pid_b[layers];

    fix_num_t a0[layers];
    a0[0] = fix_num_wrap(6, size);

    fix_num_t fix_a = fix_num_wrap(6, size);
    fix_num_t pi0 = fix_num_wrap(3, size);
    for(uint64_t i=1; i<layers; i++)
    {
        fix_a = fix_num_mul_sig(fix_a, sig_num_wrap((int64_t)2 * i - 3));
        fix_a = fix_num_div_sig(fix_a, sig_num_wrap((int64_t)8 * i));
        a0[i] = fix_num_copy(fix_a);
        
        fix_num_t fix_d = fix_num_copy(fix_a);
        fix_d = fix_num_mul_sig(fix_d, sig_num_wrap((int64_t)1 - 2 * i));
        fix_d = fix_num_div_sig(fix_d, sig_num_wrap((int64_t)4 * i + 2));

        pi0 = fix_num_add(pi0, fix_d);
    }
    fix_num_free(fix_a);

    for(uint64_t i=0; i<layers; i++)
    {
        queue_a_b[i] = queue_init(queue_size, batch_size * sizeof(fix_num_t), pi_queue_res_free);
        args_a[i] = (thread_a_args_t)
        {
            .size = size,
            .queue_a_b = &queue_a_b[i],
            .id = i,
            .layers = layers,
            .batch_size = batch_size,
            .a0 = a0[i],
            .keep_going = true
        };
        pid_a[i] = pthread_create_treat(thread_a, &args_a[i]);
        pthread_lock(pid_a[i], thread_0 + 2 * i);

        args_b[i] = (thread_b_args_t)
        {
            .size = size,
            .queue_a_b = &queue_a_b[i],
            .queue_b_pi = &junc_b_pi.queues[i],
            .id = i,
            .layers = layers,
            .batch_size = batch_size,
            .keep_going = true
        };
        pid_b[i] = pthread_create_treat(thread_b, &args_b[i]);
        pthread_lock(pid_b[i], thread_0 + 2 * i + 1);
    }

    thread_pi_args_t args_pi = (thread_pi_args_t)
    {
        .size = size,
        .junc_b_pi = &junc_b_pi,
        .layers = layers,
        .batch_size = batch_size,
        .pi0 = pi0
    };
    pthread_t pid_pi = pthread_create_treat(thread_pi, &args_pi);
    pthread_lock(pid_pi, thread_0 + 6);

    pthread_join_treat(pid_pi);
    fix_num_t fix_pi = args_pi.res;

    for(uint64_t i=0; i<layers; i++)
    {
        args_a[i].keep_going = false;
        args_b[i].keep_going = false;
    }

    for(uint64_t i=0; i<layers; i++)
    {
        fix_num_t fix[batch_size];
        for(uint64_t j=0; j<batch_size; j++)
            fix[j] = fix_num_wrap(0, 2);
        
        if(!queue_unstuck(&queue_a_b[i], &fix))
        {
            for(uint64_t j=0; j<batch_size; j++)
                fix[j] = fix_num_wrap(0, 2);
        }
        if(queue_unstuck(&junc_b_pi.queues[i], &fix))
        {
            for(uint64_t j=0; j<batch_size; j++)
                fix_num_free(fix[j]);
        }
    }

    for(uint64_t i=0; i<layers; i++)
    {
        pthread_join_treat(pid_a[i]);
        pthread_join_treat(pid_b[i]);
    }

    for(uint64_t i=0; i<layers; i++)
    {
        queue_free(&queue_a_b[i]);
    }
    junc_free(&junc_b_pi);

    printf("\n\n");
    fix_num_display_dec(fix_pi);
    fix_num_free(fix_pi);
}
