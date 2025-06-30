#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/U64.h"
#include "../../mods/number/lib/float/header.h"
#include "../../mods/number/lib/sig/header.h"

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
    bool keep_going;
};

handler_p thread_a(handler_p _args)
{
    thread_a_args_p args = _args;
    for(uint64_t i = args->id + args->layers; args->keep_going; i += args->layers)
    {
        sig_num_t sig_1 = sig_num_wrap((int64_t)2*i - 3);
        sig_num_t sig_2 = sig_num_wrap(8 * i);
        for(uint64_t j = 1; j < args->layers; j++)
        {
            sig_1 = sig_num_mul(sig_1, sig_num_wrap((int64_t)2 * i - 3 - 2 * j));
            sig_2 = sig_num_mul(sig_2, sig_num_wrap(8 * (i - j)));
        }
        float_num_t flt_a = float_num_div(
            float_num_wrap_sig(sig_1, args->size),
            float_num_wrap_sig(sig_2, args->size)
        );

        queue_post(args->queue_a_b, &flt_a, NULL);
    }

    return NULL;
}

STRUCT(thread_b_args)
{
    uint64_t size;
    queue_p queue_a_b;
    queue_p queue_b_d;
    uint64_t id;
    uint64_t layers;
    float_num_t b0;
    bool keep_going;
};

handler_p thread_b(handler_p _args)
{
    thread_b_args_p args = _args;
    float_num_t flt_b = args->b0;
    for(uint64_t i = args->id + args->layers; args->keep_going; i += args->layers)
    {
        float_num_t flt_a;
        queue_get(args->queue_a_b, &flt_a, NULL);
        if(!args->keep_going)
        {
            float_num_free(flt_a);
            break;
        }

        flt_b = float_num_mul(flt_b, flt_a);

        float_num_t flt_copy = float_num_copy(flt_b);
        queue_post(args->queue_b_d, &flt_copy, NULL);
    }
    float_num_free(flt_b);

    return NULL;
}

STRUCT(thread_c_args)
{
    uint64_t size;
    queue_p queue_c_d;
    float_num_t flt_1;
    float_num_t flt_m_1_2;
    uint64_t id;
    uint64_t layers;
    bool keep_going;
};

handler_p thread_c(handler_p _args)
{
    thread_c_args_p args = _args;
    for(uint64_t i = args->id + args->layers; args->keep_going; i += args->layers)
    {
        float_num_t flt = float_num_div(
            float_num_copy(args->flt_1),
            float_num_wrap(2 * i + 1, args->size)
        );
        flt = float_num_add(flt, float_num_copy(args->flt_m_1_2));

        queue_post(args->queue_c_d, &flt, NULL);
    }

    return NULL;
}

STRUCT(thread_d_args)
{
    uint64_t size;
    queue_p queue_b_d;
    queue_p queue_c_d;
    queue_p queue_d_pi;
    uint64_t id;
    uint64_t layers;
    bool keep_going;
};

handler_p thread_d(handler_p _args)
{
    thread_d_args_p args = _args;
    for(uint64_t i = args->id + args->layers; args->keep_going; i += args->layers)
    {
        float_num_t flt_b, flt_c;
        queue_get(args->queue_b_d, &flt_b, NULL);
        queue_get(args->queue_c_d, &flt_c, NULL);
        if(!args->keep_going)
        {
            float_num_free(flt_b);
            float_num_free(flt_c);
            break;
        }

        float_num_t flt_d = float_num_mul(flt_b, flt_c);

        queue_post(args->queue_d_pi, &flt_d, NULL);
    }

    return NULL;
}

STRUCT(thread_pi_args)
{
    uint64_t size;
    junc_p junc_d_pi;
    uint64_t layers;
    float_num_t pi0;
    float_num_t res;
};

handler_p thread_pi(handler_p _args)
{
    thread_pi_args_p args = _args;
    float_num_t flt_pi = args->pi0;
    for(uint64_t i=args->layers; ; i++)
    {
        float_num_t flt_d;
        junc_get(args->junc_d_pi, &flt_d, NULL);

        if(i%1000 == 0)
            fprintf(stderr, "\nexp: %ld", -(flt_d.size + flt_d.exponent));

        if(!float_num_safe_add(flt_pi, flt_d))
        {
            float_num_free(flt_d);
            break;
        }

        flt_pi = float_num_add(flt_pi, flt_d);
    }

    args->res = flt_pi;
    return NULL;
}



void pi_queue_res_free(handler_p h, uint64_t)
{
    float_num_free(*(float_num_p)h);
}

void pi_threads(uint64_t size, uint64_t layers)
{
    thread_a_args_t args_a[layers];
    thread_b_args_t args_b[layers];
    thread_c_args_t args_c[layers];
    thread_d_args_t args_d[layers];

    queue_t queue_a_b[layers];
    queue_t queue_b_d[layers];
    queue_t queue_c_d[layers];

    junc_t junc_d_pi = junc_init(layers, 50, sizeof(float_num_t), pi_queue_res_free);

    pthread_t pid_a[layers];
    pthread_t pid_b[layers];
    pthread_t pid_c[layers];
    pthread_t pid_d[layers];

    float_num_t b0[layers];

    float_num_t flt_b = float_num_wrap(6, size);
    float_num_t pi0 = float_num_wrap(3, size);
    b0[0] = float_num_copy(flt_b);

    for(uint64_t i=1; i<layers; i++)
    {
        float_num_t flt_a = float_num_div(
            float_num_wrap((int64_t)2 * i - 3, size),
            float_num_wrap((int64_t)8 * i, size)
        );
        flt_b = float_num_mul(flt_b, flt_a);
        b0[i] = float_num_copy(flt_b);

        float_num_t flt_c = float_num_div(
            float_num_wrap((int64_t)1 - 2 * i, size),
            float_num_wrap((int64_t)4 * i + 2, size)
        );
        flt_c = float_num_mul(flt_c, float_num_copy(flt_b));

        pi0 = float_num_add(pi0, flt_c);
    }

    float_num_t flt_1 = float_num_wrap(1, size);
    float_num_t flt_m_1_2 = float_num_div(
        float_num_wrap(-1, size),
        float_num_wrap( 2, size)
    );

    for(uint64_t i=0; i<layers; i++)
    {
        queue_a_b[i] = queue_init(50, sizeof(float_num_t), pi_queue_res_free);
        args_a[i] = (thread_a_args_t)
        {
            .size = size,
            .queue_a_b = &queue_a_b[i],
            .id = i,
            .layers = layers,
            .keep_going = true
        };
        pid_a[i] = pthread_create_treat(thread_a, &args_a[i]);

        queue_b_d[i] = queue_init(50, sizeof(float_num_t), pi_queue_res_free);
        args_b[i] = (thread_b_args_t)
        {
            .size = size,
            .queue_a_b = &queue_a_b[i],
            .queue_b_d = &queue_b_d[i],
            .id = i,
            .layers = layers,
            .b0 = b0[i],
            .keep_going = true
        };
        pid_b[i] = pthread_create_treat(thread_b, &args_b[i]);

        queue_c_d[i] = queue_init(50, sizeof(float_num_t), pi_queue_res_free);
        args_c[i] = (thread_c_args_t)
        {
            .size = size,
            .queue_c_d = &queue_c_d[i],
            .flt_1 = flt_1,
            .flt_m_1_2 = flt_m_1_2,
            .id = i,
            .layers = layers,
            .keep_going = true
        };
        pid_c[i] = pthread_create_treat(thread_c, &args_c[i]);

        args_d[i] = (thread_d_args_t)
        {
            .size = size,
            .queue_b_d = &queue_b_d[i],
            .queue_c_d = &queue_c_d[i],
            .queue_d_pi = &junc_d_pi.queues[i],
            .id = i,
            .layers = layers,
            .keep_going = true
        };
        pid_d[i] = pthread_create_treat(thread_d, &args_d[i]);
    }

    thread_pi_args_t args_pi = (thread_pi_args_t)
    {
        .size = size,
        .junc_d_pi = &junc_d_pi,
        .layers = layers,
        .pi0 = pi0
    };
    pthread_t pid_pi = pthread_create_treat(thread_pi, &args_pi);

    pthread_join_treat(pid_pi);
    float_num_t flt_pi = args_pi.res;

    for(uint64_t i=0; i<layers; i++)
    {
        args_a[i].keep_going = false;
        args_b[i].keep_going = false;
        args_c[i].keep_going = false;
        args_d[i].keep_going = false;
    }

    for(uint64_t i=0; i<layers; i++)
    {
        float_num_t flt = float_num_wrap(0, 2);
        if(!queue_unstuck(&queue_a_b[i], &flt)) flt = float_num_wrap(0, 2);
        if(!queue_unstuck(&queue_b_d[i], &flt)) flt = float_num_wrap(0, 2);
        if(!queue_unstuck(&queue_c_d[i], &flt)) flt = float_num_wrap(0, 2);
        if(queue_unstuck(&junc_d_pi.queues[i], &flt)) float_num_free(flt);
    }

    for(uint64_t i=0; i<layers; i++)
    {
        pthread_join_treat(pid_a[i]);
        pthread_join_treat(pid_b[i]);
        pthread_join_treat(pid_c[i]);
        pthread_join_treat(pid_d[i]);
    }

    for(uint64_t i=0; i<layers; i++)
    {
        queue_free(&queue_a_b[i]);
        queue_free(&queue_b_d[i]);
        queue_free(&queue_c_d[i]);
    }
    junc_free(&junc_d_pi);

    float_num_free(flt_1);
    float_num_free(flt_m_1_2);

    printf("\n\n");
    float_num_display_dec(flt_pi);
    float_num_free(flt_pi);
}
