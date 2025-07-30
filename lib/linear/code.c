#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/uint.h"
#include "../../mods/number/lib/float/header.h"
#include "../../mods/number/lib/sig/header.h"
#include "../../mods/number/lib/num/header.h"
#include "../../mods/number/lib/num/struct.h"



#ifdef DEBUG
#endif



fix_num_t pi_v1(uint64_t size)
{
    uint64_t index_max = 32 * size + 4;

    fix_num_t fix_1 = fix_num_wrap(1, size);
    fix_num_t fix_m_1_2 = fix_num_div(
        fix_num_wrap(-1, size),
        fix_num_wrap( 2, size)
    );

    fix_num_t fix_b = fix_num_wrap(6, size);
    fix_num_t fix_pi = fix_num_wrap(3, size);
    for(uint64_t i=1; i<index_max; i++)
    {
        fix_num_t fix_a = fix_num_div(
            fix_num_wrap(2 * i - 3, size),
            fix_num_wrap(8 * i, size)
        );
        fix_b = fix_num_mul(fix_b, fix_a);

        fix_num_t fix_c = fix_num_div(
            fix_num_copy(fix_1),
            fix_num_wrap(2 * i + 1, size)
        );
        fix_c = fix_num_add(fix_c, fix_num_copy(fix_m_1_2));
        fix_num_t fix_d = fix_num_mul(fix_num_copy(fix_b), fix_c);

        fix_pi = fix_num_add(fix_pi, fix_d);
    }

    fix_num_free(fix_b);
    fix_num_free(fix_1);
    fix_num_free(fix_m_1_2);

    return fix_pi;
}

fix_num_t pi_v2(uint64_t size)
{
    uint64_t index_max = 32 * size + 4;

    fix_num_t fix_a = fix_num_wrap(6, size);
    fix_num_t fix_pi = fix_num_wrap(3, size);
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



#define SIG 0
#define FLOAT 1

union_num_t union_num_wrap_float(float_num_t flt, uint64_t size)
{
    return (union_num_t)
    {
        .type = FLOAT,
        .size = size,
        .num = {.flt = flt}
    };
}

union_num_t union_num_wrap_sig(sig_num_t sig, uint64_t size)
{
    if(sig.num->count >= size)
    {
        float_num_t flt = float_num_wrap_sig(sig, size);
        return union_num_wrap_float(flt, size);
    }

    return (union_num_t)
    {
        .type = SIG,
        .size = size,
        .num = {.sig = sig}
    };
}

float_num_t union_num_unwrap_float(union_num_t u)
{
    switch (u.type)
    {
        case SIG: return float_num_wrap_sig(u.num.sig, u.size);
        case FLOAT: return u.num.flt;
    }

    exit(EXIT_FAILURE);
}

union_num_t union_num_copy(union_num_t u)
{
    
    switch (u.type)
    {
        case SIG:
        {
            sig_num_t sig = sig_num_copy(u.num.sig);
            return union_num_wrap_sig(sig, u.size);
        }
        break;

        case FLOAT:
        {
            float_num_t flt = float_num_copy(u.num.flt);
            return union_num_wrap_float(flt, u.size);
        }
        break;
    }
    exit(EXIT_FAILURE);
}

void union_num_free(union_num_t u)
{
    
    switch (u.type)
    {
        case SIG:
        {
            sig_num_free(u.num.sig);
            return;
        }
        break;

        case FLOAT:
        {
            float_num_free(u.num.flt);
            return;
        }
        break;
    }
    exit(EXIT_FAILURE);
}



union_num_t union_num_mul(union_num_t u_1, union_num_t u_2)
{
    switch (u_1.type)
    {
        case SIG:
        {
            switch (u_2.type)
            {
                case SIG:
                {
                    sig_num_t sig = sig_num_mul(u_1.num.sig, u_2.num.sig);
                    return union_num_wrap_sig(sig, u_1.size);
                }
                break;

                case FLOAT:
                {
                    float_num_t flt = float_num_mul_sig(u_2.num.flt, u_1.num.sig);
                    return union_num_wrap_float(flt, u_1.size);
                }
                break;
            }
        }
        break;

        case FLOAT:
        {
            switch (u_2.type)
            {
                case SIG:
                {
                    float_num_t flt = float_num_mul_sig(u_1.num.flt, u_2.num.sig);
                    return union_num_wrap_float(flt, u_1.size);
                }
                break;

                case FLOAT:
                {
                    float_num_t flt = float_num_mul(u_1.num.flt, u_2.num.flt);
                    return union_num_wrap_float(flt, u_1.size);
                }
                break;
            }
        }
        break;
    }
    exit(EXIT_FAILURE);
}

union_num_t union_num_add(union_num_t u_1, union_num_t u_2)
{
    switch (u_1.type)
    {
        case SIG:
        {
            switch (u_2.type)
            {
                case SIG:
                {
                    sig_num_t sig = sig_num_add(u_1.num.sig, u_2.num.sig);
                    return union_num_wrap_sig(sig, u_1.size);
                }
                break;

                case FLOAT:
                {
                    float_num_t flt = float_num_add(float_num_wrap_sig(u_1.num.sig, u_1.size), u_2.num.flt);
                    return union_num_wrap_float(flt, u_1.size);
                }
                break;
            }
        }
        break;

        case FLOAT:
        {
            switch (u_2.type)
            {
                case SIG:
                {
                    float_num_t flt = float_num_add(u_1.num.flt, float_num_wrap_sig(u_2.num.sig, u_1.size));
                    return union_num_wrap_float(flt, u_1.size);
                }
                break;

                case FLOAT:
                {
                    float_num_t flt = float_num_add(u_1.num.flt, u_2.num.flt);
                    return union_num_wrap_float(flt, u_1.size);
                }
                break;
            }
        }
        break;
    }
    exit(EXIT_FAILURE);
}

void union_num_display(union_num_t u)
{
    switch (u.type)
    {
        case SIG:
        {
            printf("SIG | ");sig_num_display_dec(u.num.sig);
            return;
        }
        break;

        case FLOAT:
        {
            printf("FLT | ");float_num_display_dec(u.num.flt);
            return;
        }
        break;
    }
    exit(EXIT_FAILURE);
}

void union_num_display_full(char tag[], union_num_t u)
{
    switch (u.type)
    {
        case SIG:
        {
            printf("\nSIG | %s ", tag);
            num_display_opts(u.num.sig.num, NULL, true, true);
            return;
        }
        break;

        case FLOAT:
        {
            printf("\nFLT | %s ", tag);
            num_display_opts(u.num.flt.sig.num, NULL, true, true);
            return;
        }
        break;
    }
    exit(EXIT_FAILURE);
}



void binary_splitting_join(
    union_num_t out[],
    union_num_t res_1[3],
    union_num_t res_2[3]
)
{
    union_num_t u_r_1 = union_num_mul(res_1[2], union_num_copy(res_2[1]));
    union_num_t u_r_2 = union_num_mul(res_2[2], union_num_copy(res_1[0]));

    union_num_t out_0 = union_num_mul(res_1[0], res_2[0]);
    union_num_t out_1 = union_num_mul(res_1[1], res_2[1]);
    union_num_t out_2 = union_num_add(u_r_1, u_r_2);

    out[0] = out_0;
    out[1] = out_1;
    out[2] = out_2;
}

// out vector length 3, returns P, Q, R in that order
void binary_splitting(
    union_num_t out[],
    uint64_t size,
    uint64_t i_0,
    uint64_t i_max,
    uint64_t depth,
    uint64_t max
)
{
    // if(depth == 7 && max)
    //     fprintf(stderr, "\n%.1f %%", 100.0 * i_0 / max);

    assert(i_0 <= i_max);
    if(i_0 == i_max)
    {
        int64_t p = 2 * i_0 - 3;
        int64_t q = 8 * i_0;
        int64_t u = 1 - 2 * i_0;
        int64_t v = 4 * i_0 + 2;

        sig_num_t sig_p = sig_num_wrap(p * v);
        sig_num_t sig_q = sig_num_wrap(q * v);
        sig_num_t sig_r = sig_num_wrap(p * u);

        out[0] = union_num_wrap_sig(sig_p, size);
        out[1] = union_num_wrap_sig(sig_q, size);
        out[2] = union_num_wrap_sig(sig_r, size);

        return;
    }

    uint64_t i_half = (i_0 + i_max) / 2;
    union_num_t res_1[3], res_2[3];
    binary_splitting(res_1, size, i_0       , i_half, depth + 1, max);
    binary_splitting(res_2, size, i_half + 1, i_max , depth + 1, max);
    binary_splitting_join(out, res_1, res_2);
}

float_num_t pi_v3(uint64_t size)
{
    uint64_t index_max = 32 * size + 4;

    union_num_t res[3];
    binary_splitting(res, size, 1, index_max, 0, index_max);
    union_num_free(res[0]);

    float_num_t flt_q = union_num_unwrap_float(res[1]);
    float_num_t flt_r = union_num_unwrap_float(res[2]);

    float_num_t flt_pi = flt_r;
    flt_pi = float_num_mul_sig(flt_pi, sig_num_wrap(6));
    flt_pi = float_num_div(flt_pi, flt_q);
    flt_pi = float_num_add(flt_pi, float_num_wrap(3, size));
    return flt_pi;
}
