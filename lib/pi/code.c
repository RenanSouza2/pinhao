#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
// #include "../../mods/macros/time.h"
#include "../../mods/macros/uint.h"
#include "../../mods/number/lib/sig/header.h"

#include "../pear/header.h"
#include "../linear/header.h"



#ifdef DEBUG
#endif



STRUCT(thread_pi_args)
{
    uint64_t id, thread_count;
    uint64_t index_0, index_max;
    uint64_t size;
    pthread_t *tid;
    bool launched;
    union_num_t res[3];
};

handler_p thread_pi(handler_p _args)
{
    thread_pi_args_p args = (thread_pi_args_p)_args;

    uint64_t id = args->id;
    uint64_t thread_count = args->thread_count;
    uint64_t index_0 = args->index_0;
    uint64_t index_max = args->index_max;
    uint64_t size = args->size;
    union_num_t res[3];

    // printf("\nargs: %lu %lu %lu", index_0, index_max, size);
    binary_splitting(res, size, index_0, index_max, 0, id ? 0 : index_max);

    while(!args->launched);
    for(uint64_t mask = 1; (mask < thread_count) && ((mask & id) == 0); mask *= 2)
    {
        pthread_join_treat(args->tid[id + mask]);
        binary_splitting_join(res, res, args[mask].res);
    }

    args->res[0] = res[0];
    args->res[1] = res[1];
    args->res[2] = res[2];
    return NULL;
}

float_num_t pi_threads(uint64_t size, uint64_t thread_count, uint64_t thread_0)
{
    uint64_t index_max = 32 * size + 4;
    uint64_t index[thread_count + 1];
    index[0] = 0;
    index[thread_count] = index_max;
    for(uint64_t i=1; i<thread_count; i++)
    {
        index[i] = index_max * i / thread_count;
    }
    // for(uint64_t i=0;  i<=thread_count; i++)
    //     printf("\nindex[%lu]: %lu", i, index[i]);

    thread_pi_args_t args[thread_count];
    pthread_t tid[thread_count];
    for(uint64_t i=0;i<thread_count; i++)
    {
        args[i] = (thread_pi_args_t)
        {
            .id = i,
            .thread_count = thread_count,
            .index_0 = index[i] + 1,
            .index_max = index[i + 1],
            .size = size,
            .tid = tid
        };
        tid[i] = pthread_create_treat(thread_pi, &args[i]);
        pthread_lock(tid[i], thread_0 + i);
    }

    for(uint64_t i=0;i<thread_count; i++)
        args[i].launched = true;

    pthread_join_treat(tid[0]);
    union_num_t *res = args[0].res;
    union_num_free(res[0]);

    float_num_t flt_q = union_num_unwrap_float(res[1]);
    float_num_t flt_r = union_num_unwrap_float(res[2]);

    float_num_t flt_pi = flt_r;
    flt_pi = float_num_mul_sig(flt_pi, sig_num_wrap(6));
    flt_pi = float_num_div(flt_pi, flt_q);
    flt_pi = float_num_add(flt_pi, float_num_wrap(3, size));
    return flt_pi;
}
