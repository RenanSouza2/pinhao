#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/stdbit.h"
#include "../../mods/macros/uint.h"
#include "../../mods/macros/time.h"
#include "../../mods/number/lib/num/struct.h"
#include "../../mods/number/header.h"

#include "../union/header.h"
#include "../split/header.h"



#ifdef DEBUG
#endif



#define PIECE_SIZE 16
#define CACHE "cache_del"



void split_sig_join(
    sig_num_t out[3],
    sig_num_t res_1[3],
    sig_num_t res_2[3]
)
{
    sig_num_t sig_r_1 = sig_num_mul(res_1[2], sig_num_copy(res_2[1]));
    sig_num_t sig_r_2 = sig_num_mul(res_2[2], sig_num_copy(res_1[0]));

    sig_num_t out_0 = sig_num_mul(res_1[0], res_2[0]);
    sig_num_t out_1 = sig_num_mul(res_1[1], res_2[1]);
    sig_num_t out_2 = sig_num_add(sig_r_1, sig_r_2);

    out[0] = out_0;
    out[1] = out_1;
    out[2] = out_2;
}

// out vector length 3, returns P, Q, R in that order
void split_sig(sig_num_t out[3], uint64_t i_0, uint64_t span)
{
    if(span == 0)
    {
        int128_t p = (int128_t)2 * i_0 - 3;
        int128_t q = (int128_t)8 * i_0;
        int128_t u = (int128_t)1 - 2 * i_0;
        int128_t v = (int128_t)4 * i_0 + 2;

        out[0] = sig_num_wrap_int128(p * v);
        out[1] = sig_num_wrap_int128(q * v);
        out[2] = sig_num_wrap_int128(p * u);
        return;
    }

    sig_num_t res_1[3], res_2[3];
    split_sig(res_1, i_0              , span - 1);
    split_sig(res_2, i_0 + B(span - 1), span - 1);
    split_sig_join(out, res_1, res_2);
}



void sig_res_path_set(char path[100], uint64_t i_0, uint64_t span)
{
    uint64_t i_max = i_0 + B(span) - 1;
    span = span - PIECE_SIZE;
    snprintf(path, 100, CACHE "/pieces/p_" U64P(015) "_" U64P(02) "_" U64P(015) ".txt", i_0, span, i_max);
}

void sig_res_delete(uint64_t i_0, uint64_t span)
{
    char path[100];
    sig_res_path_set(path, i_0, span);
    remove(path);
}

FILE* sig_res_try_open_read(uint64_t i_0, uint64_t span)
{
    char path[100];
    sig_res_path_set(path, i_0, span);
    return file_read_open(path);
}

file_t sig_res_open_write(uint64_t i_0, uint64_t span)
{
    char path[100];
    sig_res_path_set(path, i_0, span);
    return file_write_open(path, 3);
}

void sig_res_save(sig_num_t res[3], uint64_t i_0, uint64_t span)
{
    file_t fp = sig_res_open_write(i_0, span);
    for(uint64_t i=0; i<3; i++)
        file_write_sig_num(&fp, res[i]);

    file_write_close(&fp);

    sig_num_free(res[0]);
    sig_num_free(res[1]);
    sig_num_free(res[2]);
}

bool sig_res_try_load(sig_num_p out, uint64_t i_0, uint64_t span, uint64_t index)
{
    FILE *fp = sig_res_try_open_read(i_0, span);
    if(fp == NULL)
        return false;

    *out = file_read_sig_num(fp, index);
    fclose(fp);
    return true;
}

sig_num_t sig_res_load(uint64_t i_0, uint64_t span, uint64_t index)
{
    sig_num_t res;
    assert(sig_res_try_load(&res, i_0, span, index));
    return res;
}

bool sig_res_is_stored(uint64_t i_0, uint64_t span)
{
    FILE *fp = sig_res_try_open_read(i_0, span);
    if(fp == NULL)
        return false;

    fclose(fp);
    return true;
}

// Get the size of the first number
uint64_t sig_res_get_size(uint64_t i_0, uint64_t span)
{
    FILE *fp = sig_res_try_open_read(i_0, span);
    assert(fp);

    file_read_move_to_index(fp, 0);
    file_read_uint64(fp);
    uint64_t size = file_read_uint64(fp);
    fclose(fp);

    return size;
}



void union_res_path_set(
    char path[100],
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
)
{
    uint64_t i_max = i_0 + remainder - 1;
    snprintf(path, 100, CACHE "/numbers/u_" U64P(015) "_" U64P(015) "_" U64P(02) "_" U64P(015) ".txt", size, i_0, depth, i_max);
}

void union_res_delete(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    char path[100];
    union_res_path_set(path, size, i_0, remainder, depth);
    remove(path);
}

FILE* union_res_try_open_read(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    char path[100];
    union_res_path_set(path, size, i_0, remainder, depth);
    return file_read_open(path);
}

file_t union_res_open_write(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    char path[100];
    union_res_path_set(path, size, i_0, remainder, depth);
    return file_write_open(path, 3);
}

union_num_t union_res_load(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth,
    uint64_t index,
    uint64_t del
)
{
    FILE *fp = union_res_try_open_read(size, i_0, remainder, depth + del);
    assert(fp);

    union_num_t u = file_read_union_num(fp, index);
    fclose(fp);
    return u;
}

bool union_res_is_stored(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    FILE *fp = union_res_try_open_read(size, i_0, span, depth);
    if(fp == NULL)
        return false;
        
    fclose(fp);
    return true;
}



void split_span_res_path_set(
    char path[100],
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
)
{
    union_res_path_set(path, size, i_0, B(span), depth);
}

void split_span_res_delete(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    char path[100];
    split_span_res_path_set(path, size, i_0, span, depth);
    remove(path);
}

union_num_t split_span_res_load(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth,
    uint64_t index
)
{
    sig_num_t sig;
    if(sig_res_try_load(&sig, i_0, span, index))
        return union_num_wrap_sig(sig, size);

    return union_res_load(size, i_0, B(span), depth, index, 0);
}

bool split_span_res_is_stored(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    return sig_res_is_stored(i_0, span) || union_res_try_open_read(size, i_0, span, depth);
}

bool split_span_res_is_sig(uint64_t size, uint64_t i_0, uint64_t span)
{
    // size, i_0 , span - 1, depth + 1
    if(!sig_res_is_stored(i_0, span - 1))
        return false;

    // size, i_0 + B(span - 1), span - 1, depth + 1
    if(!sig_res_is_stored(i_0 + B(span - 1), span - 1))
        return false;

    uint64_t size_1 = sig_res_get_size(i_0, span - 1);
    return size_1 < size;
}



#define PIECE_SPAN 16

void split_span_res_join(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    if(split_span_res_is_sig(size, i_0, span))
    {
        file_t fp = sig_res_open_write(i_0, span);
        for(uint64_t i=0; i<2; i++)
        {
            sig_num_t sig_1 = sig_res_load(i_0, span - 1, i);
            sig_num_t sig_2 = sig_res_load(i_0 + B(span - 1), span - 1, i);
            sig_num_t sig = sig_num_mul(sig_1, sig_2);
            file_write_sig_num(&fp, sig);
            sig_num_free(sig);
        }

        sig_num_t sig_1 = sig_res_load(i_0, span - 1, 0);
        sig_num_t sig_2 = sig_res_load(i_0 + B(span - 1), span - 1, 2);
        sig_num_t sig_r_1 = sig_num_mul(sig_1, sig_2);

        sig_1 = sig_res_load(i_0, span - 1, 2);
        sig_2 = sig_res_load(i_0 + B(span - 1), span - 1, 1);
        sig_num_t sig_r_2 = sig_num_mul(sig_1, sig_2);

        sig_r_1 = sig_num_add(sig_r_1, sig_r_2);
        file_write_sig_num(&fp, sig_r_1);
        sig_num_free(sig_r_1);
        
        file_write_close(&fp);

        sig_res_delete(i_0, span - 1);
        sig_res_delete(i_0 + B(span - 1), span - 1);
        return;
    }

    file_t fp = union_res_open_write(size, i_0, B(span), depth);
    for(uint64_t i=0; i<2; i++)
    {
        union_num_t u_1 = split_span_res_load(size, i_0, span - 1, depth + 1, i);
        union_num_t u_2 = split_span_res_load(size, i_0 + B(span - 1), span - 1, depth + 1, i);
        union_num_t u = union_num_mul(u_1, u_2);
        file_write_union_num(&fp, u);
        union_num_free(u);
    }

    union_num_t u_1 = split_span_res_load(size, i_0, span - 1, depth + 1, 0);
    union_num_t u_2 = split_span_res_load(size, i_0 + B(span - 1), span - 1, depth + 1, 2);
    union_num_t u_r_1 = union_num_mul(u_1, u_2);

    u_1 = split_span_res_load(size, i_0, span - 1, depth + 1, 2);
    u_2 = split_span_res_load(size, i_0 + B(span - 1), span - 1, depth + 1, 1);
    union_num_t u_r_2 = union_num_mul(u_1, u_2);
    
    u_r_1 = union_num_add(u_r_1, u_r_2);
    file_write_union_num(&fp, u_r_1);
    union_num_free(u_r_1);

    file_write_close(&fp);

    split_span_res_delete(size, i_0, span - 1, depth + 1);
    split_span_res_delete(size, i_0 + B(span - 1), span - 1, depth + 1);
}

// out vector length 3, returns P, Q, R in that order
void split_span(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    assert(span >= PIECE_SPAN);
    tprintf("begin | " U64P() " " U64P() " " U64P() "", i_0, span, depth);

    if(split_span_res_is_stored(size, i_0, span, depth))
        return;

    if(span == PIECE_SPAN)
    {
        sig_num_t res[3];
        TIME_SETUP
        split_sig(res, i_0, span);
        TIME_END(t1)
        fprintf(stderr, "\t\t%.1f", (double)t1 / 1e9);
        sig_res_save(res, i_0, span);
        return;
    }

    split_span(size, i_0              , span - 1, depth + 1);
    split_span(size, i_0 + B(span - 1), span - 1, depth + 1);

    tprintf("joining | " U64P() " " U64P() " " U64P() "", i_0, span, depth);
    TIME_SETUP
    split_span_res_join(size, i_0, span, depth);
    TIME_END(t1)
    fprintf(stderr, "\t\t%.1f", (double)t1 / 1e9);
}



union_num_t split_big_res_load(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth,
    uint64_t index
)
{
    if(stdc_count_ones(remainder) == 1)
    {
        uint64_t span = stdc_bit_width(remainder) - 1;
        return split_span_res_load(size, i_0, span, depth, index);
    }

    return union_res_load(size, i_0, remainder, depth, index, 0);
}

bool split_big_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
)
{
    if(stdc_count_ones(remainder) == 1)
    {
        uint64_t span = stdc_bit_width(remainder) - 1;
        return split_span_res_is_stored(size, i_0, span, depth);
    }

    return union_res_is_stored(size, i_0, remainder, depth);
}

void split_big_res_join(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    file_t fp = union_res_open_write(size, i_0, remainder, depth);
    
    uint64_t span = stdc_bit_width(remainder) - 1;
    for(uint64_t i=0; i<2; i++)
    {
        union_num_t u_1 = split_span_res_load(size, i_0, span, depth + 1, i);
        union_num_t u_2 = split_big_res_load(size, i_0 + B(span), remainder - B(span), depth + 1, i);
        union_num_t u = union_num_mul(u_1, u_2);

        file_write_union_num(&fp, u);
        union_num_free(u);
    }

    union_num_t u_1 = split_span_res_load(size, i_0, span, depth + 1, 0);
    union_num_t u_2 = split_big_res_load(size, i_0 + B(span), remainder - B(span), depth + 1, 2);
    union_num_t u_r_1 = union_num_mul(u_1, u_2);

    u_1 = split_span_res_load(size, i_0, span, depth + 1, 2);
    u_2 = split_big_res_load(size, i_0 + B(span), remainder - B(span), depth + 1, 1);
    union_num_t u_r_2 = union_num_mul(u_1, u_2);

    union_num_t u = union_num_add(u_r_1, u_r_2);
    file_write_union_num(&fp, u);
    union_num_free(u);

    file_write_close(&fp);

    split_span_res_delete(size, i_0, span, depth + 1);
    union_res_delete(size, i_0 + B(span), remainder - B(span), depth + 1);
}

// out vector length 3, returns P, Q, R in that order
void split_big(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    tprintf("begin | " U64P() " " U64P() " " U64P() "", i_0, remainder, depth)

    if(split_big_res_is_stored(size, i_0, remainder, depth))
        return;

    uint64_t span = stdc_bit_width(remainder) - 1;
    if(stdc_count_ones(remainder) == 1)
    {
        split_span(size, i_0, span, depth);
        return;
    }

    split_span(size, i_0, span, depth + 1);
    split_big(size, i_0 + B(span), remainder - B(span), depth + 1);

    tprintf("joining | " U64P() " " U64P() " " U64P() "", i_0, span, depth);
    TIME_SETUP
    split_big_res_join(size, i_0, remainder, depth);
    TIME_END(t1)
    fprintf(stderr, "\t\t%.1f", (double)t1 / 1e9);
}



flt_num_t pi_big(uint64_t size)
{
    uint64_t index_max = 32 * size + 4;
    uint64_t aux = index_max & (B(PIECE_SIZE) - 1);
    if(aux) index_max += B(PIECE_SIZE) - aux;

    split_big(size, 1, index_max, 0);
    tprintf("solved");

    union_num_t u_q = split_big_res_load(size, 1, index_max, 0, 1);
    union_num_t u_r = split_big_res_load(size, 1, index_max, 0, 2);

    flt_num_t flt_q = union_num_unwrap_flt(u_q);
    flt_num_t flt_r = union_num_unwrap_flt(u_r);
    
    flt_num_t flt_pi = flt_r;
    flt_pi = flt_num_mul_sig(flt_pi, sig_num_wrap(6));
    flt_pi = flt_num_div(flt_pi, flt_q);
    flt_pi = flt_num_add(flt_pi, flt_num_wrap(3, size));
    return flt_pi;
}

void prepare(uint64_t size, uint64_t span, uint64_t begin, uint64_t end)
{
    uint64_t i_max = 32 * size + 4;

    uint64_t aux = i_max & (B(PIECE_SIZE) - 1);
    if(aux)
        i_max += B(PIECE_SIZE) - aux;

    printf("\nspan: " U64P() "\tbegin: " U64P() "\tend: " U64P() "", span, begin, end);
    printf("\ni_0: " U64P() "\ti_max: " U64P() "", begin * B(span) + 1, (end + 1) * B(span));
    for(uint64_t i=begin; i<end; i++)
    {
        printf("\ni: " U64P() "", i);

        uint64_t i_0 = i * B(span) + 1;
        split_span(size, i_0, span, 0);
    }
}
