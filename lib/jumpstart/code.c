#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/struct.h"
#include "../../mods/macros/U64.h"
#include "../../mods/number/lib/float/header.h"
#include "../../mods/number/lib/sig/header.h"

#include "../pear/header.h"



#ifdef DEBUG
#endif

#include <unistd.h>



int64_t element_upper(int64_t index, uint64_t delta)
{
    return 2 * (index + delta) - 3;
}

int64_t element_lower(int64_t index, uint64_t /*delta*/)
{
    return index;
}


typedef int64_t (*element_f)(int64_t index, uint64_t delta);

STRUCT(thread_mul_sig_args)
{
    uint64_t size;
    uint64_t layer_count;
    uint64_t index_0;
    uint64_t split;

    uint64_t id;
    element_f element;
    pthread_t *tid;

    bool volatile launched;
    float_num_t flt;
};

handler_p thread_mul_sig(handler_p _args)
{
    thread_mul_sig_args_p args = (thread_mul_sig_args_p)_args;

    uint64_t size = args->size;
    uint64_t layer_count = args->layer_count;
    uint64_t index_0 = args->index_0;
    uint64_t split = args->split;

    uint64_t id = args->id;
    element_f element = args->element;
    pthread_t *tid = args->tid;

    uint64_t index_max = index_0 / 2;
    uint64_t i_max = (index_max + layer_count - 2) / layer_count;
    uint64_t delta = (index_0 + 1) / 2;

    float_num_t flt = float_num_wrap(1, size);
    for(uint64_t i=0 + id; i<i_max; i+=split)
    {
        uint64_t index = 2 + layer_count * i;

        sig_num_t sig = sig_num_wrap(element(index, delta));
        for(uint64_t k=1; (k<layer_count) && index + k <= index_max; k++)
            sig = sig_num_mul(sig, sig_num_wrap(element(index + k, delta)));

        flt = float_num_mul_sig(flt, sig);
    }

    while(!args->launched);

    for(uint64_t mask = 1; ((mask & id) == 0) && (mask < split); mask *= 2)
    {
        pthread_join_treat(tid[id+mask]);
        flt = float_num_mul(flt, args[mask].flt);
    }

    args->flt = flt; 
    return &args->flt;
}

fix_num_t jumpstart_thread(
    uint64_t size,
    uint64_t layer_count,
    uint64_t index_0,
    uint64_t thread_0
)
{    
    uint64_t split = 2;

    thread_mul_sig_args_t args_upper[split];
    thread_mul_sig_args_t args_lower[split];
    pthread_t tid_upper[split];
    pthread_t tid_lower[split];

    if(index_0 == 0)
        return fix_num_wrap(6, size - 1);

    assert(index_0 > 3);
    uint64_t pos = size - index_0 / 32;

    for(uint64_t i=0; i<split; i++)
    {
        args_upper[i] = (thread_mul_sig_args_t)
        {
            .size = pos,
            .layer_count = layer_count,
            .index_0 = index_0,
            .split = split,

            .id = i,
            .element = element_upper,
            .tid = tid_upper,
        };
        tid_upper[i] = pthread_create_treat(thread_mul_sig, &args_upper[i]);
        pthread_lock(tid_upper[i], thread_0 + i);

        args_lower[i] = (thread_mul_sig_args_t)
        {
            .size = pos,
            .layer_count = layer_count,
            .index_0 = index_0,
            .split = split,

            .id = i,
            .element = element_lower,
            .tid = tid_lower,
        };
        tid_lower[i] = pthread_create_treat(thread_mul_sig, &args_lower[i]);
        pthread_lock(tid_lower[i], thread_0 + split + i);
    }

    for(uint64_t i=0; i<split; i++)
    {
        args_upper[i].launched = true;
        args_lower[i].launched = true;
    }

    pthread_join_treat(tid_upper[0]);
    float_num_t flt = float_num_mul_sig(args_upper[0].flt, sig_num_wrap(-6));
    flt = float_num_shr(flt, 7 * index_0 / 2);

    pthread_join_treat(tid_lower[0]);
    flt = float_num_div(flt, args_lower[0].flt);

    return fix_num_wrap_float(flt, size);
}



fix_num_t jumpstart_standard(uint64_t index_max, uint64_t size, uint64_t layer_count)
{
    assert(index_max % layer_count == 0);

    uint64_t i_max = index_max / layer_count;
    float_num_t flt_1 = float_num_wrap(6, size);
    float_num_t flt_2 = float_num_wrap(1, size);
    for(uint64_t i=1; i<i_max; i++)
    {
        uint64_t index = 1 + layer_count * (i - 1);

        sig_num_t sig_1 = sig_num_wrap((int64_t)2 * index - 3);
        sig_num_t sig_2 = sig_num_wrap((int64_t)8 * index);
        for(uint64_t k=1; k<layer_count; k++)
        {
            sig_1 = sig_num_mul(sig_1, sig_num_wrap((int64_t)2 * (index + k) - 3));
            sig_2 = sig_num_mul(sig_2, sig_num_wrap((int64_t)8 * (index + k)));
        }

        flt_1 = float_num_mul_sig(flt_1, sig_1);
        flt_2 = float_num_mul_sig(flt_2, sig_2);
    }
    flt_1 = float_num_div(flt_1, flt_2);
    return fix_num_wrap_float(flt_1, size);
}

fix_num_t jumpstart_div_during(uint64_t index_max, uint64_t size, uint64_t layer_count)
{
    assert(index_max % layer_count == 0);

    uint64_t i_max = index_max / layer_count;
    float_num_t flt = float_num_wrap(6, size);
    for(uint64_t i=1; i<i_max; i++)
    {
        uint64_t index = layer_count * i;

        sig_num_t sig_1 = sig_num_wrap((int64_t)2 * index - 3);
        sig_num_t sig_2 = sig_num_wrap((int64_t)8 * index);
        for(uint64_t k=1; k<layer_count; k++)
        {
            sig_1 = sig_num_mul(sig_1, sig_num_wrap((int64_t)2 * (index - k) - 3));
            sig_2 = sig_num_mul(sig_2, sig_num_wrap((int64_t)8 * (index - k)));
        }

        flt = float_num_mul_sig(flt, sig_1);
        flt = float_num_div_sig(flt, sig_2);
    }
    return fix_num_wrap_float(flt, size);
}

fix_num_t jumpstart_ass_1(uint64_t index_max, uint64_t size, uint64_t layer_count)
{
    assert(index_max > 3);
    uint64_t index_max_prod = index_max / 2;
    uint64_t i_max = (index_max_prod + layer_count - 2) / layer_count;
    uint64_t delta = (index_max + 1) / 2;

    float_num_t flt_1 = float_num_wrap(-6, size);
    float_num_t flt_2 = float_num_wrap( 1, size);
    for(uint64_t i=0; i<i_max; i++)
    {
        uint64_t index = 2 + layer_count * i;

        sig_num_t sig_1 = sig_num_wrap(2 * (index + delta) - 3);
        sig_num_t sig_2 = sig_num_wrap(index);
        for(uint64_t k=1; (k<layer_count) && index + k <= index_max_prod; k++)
        {
            sig_1 = sig_num_mul(sig_1, sig_num_wrap(2 * (index + k + delta) - 3));
            sig_2 = sig_num_mul(sig_2, sig_num_wrap(index + k));
        }

        flt_1 = float_num_mul_sig(flt_1, sig_1);
        flt_2 = float_num_mul_sig(flt_2, sig_2);
    }
    flt_1 = float_num_shr(flt_1, 7 * index_max / 2);
    flt_1 = float_num_div(flt_1, flt_2);
    return fix_num_wrap_float(flt_1, size);
}


fix_num_t jumpstart_ass_2(uint64_t index_max, uint64_t size, uint64_t layer_count)
{
    assert(index_max > 3);
    uint64_t index_max_prod = index_max / 2;
    uint64_t i_max = (index_max_prod + layer_count - 2) / layer_count;
    uint64_t delta = (index_max + 1) / 2;
    uint64_t pos = size - index_max / 32;

    float_num_t flt_1 = float_num_wrap(-6, pos);
    float_num_t flt_2 = float_num_wrap( 1, pos);
    for(uint64_t i=0; i<i_max; i++)
    {
        uint64_t index = 2 + layer_count * i;

        sig_num_t sig_1 = sig_num_wrap(2 * (index + delta) - 3);
        sig_num_t sig_2 = sig_num_wrap(index);
        for(uint64_t k=1; (k<layer_count) && index + k <= index_max_prod; k++)
        {
            sig_1 = sig_num_mul(sig_1, sig_num_wrap(2 * (index + k + delta) - 3));
            sig_2 = sig_num_mul(sig_2, sig_num_wrap(index + k));
        }

        flt_1 = float_num_mul_sig(flt_1, sig_1);
        flt_2 = float_num_mul_sig(flt_2, sig_2);
    }
    flt_1 = float_num_shr(flt_1, 7 * index_max / 2);
    flt_1 = float_num_div(flt_1, flt_2);
    return fix_num_wrap_float(flt_1, size);
}
