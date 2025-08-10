#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/uint.h"

#include"../union/header.h"



#ifdef DEBUG
#endif



void split_join(
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
void split(union_num_t out[], uint64_t size, uint64_t i_0, uint64_t i_max)
{
    assert(i_0 <= i_max);
    if(i_0 == i_max)
    {
        int128_t p = (int128_t)2 * i_0 - 3;
        int128_t q = (int128_t)8 * i_0;
        int128_t u = (int128_t)1 - 2 * i_0;
        int128_t v = (int128_t)4 * i_0 + 2;

        sig_num_t sig_p = sig_num_wrap_int128(p * v);
        sig_num_t sig_q = sig_num_wrap_int128(q * v);
        sig_num_t sig_r = sig_num_wrap_int128(p * u);

        out[0] = union_num_wrap_sig(sig_p, size);
        out[1] = union_num_wrap_sig(sig_q, size);
        out[2] = union_num_wrap_sig(sig_r, size);

        return;
    }

    uint64_t i_half = (i_0 + i_max) / 2;
    union_num_t res_1[3], res_2[3];
    split(res_1, size, i_0       , i_half);
    split(res_2, size, i_half + 1, i_max );
    split_join(out, res_1, res_2);
}
