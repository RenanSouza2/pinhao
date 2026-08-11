#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include "debug.h" // IWYU pragma: keep
#include "../../mods/clu/header.h" // IWYU pragma: keep
#include "../../mods/macros/assert.h"
#include "../../mods/macros/fork.h"
#include "../../mods/macros/stdbit.h" // IWYU pragma: keep
#include "../../mods/macros/uint.h"
#include "../../mods/macros/time.h"
#include "../../mods/macros/threads.h"
#include "../../mods/araucaria/header.h"

#include "../union/header.h"



#ifdef DEBUG
#endif



#define PIECE_SIZE 20
#define PATH_MAX_LEN 256

#ifndef CACHE
#define CACHE "cache"
#endif


static void split_sig_join(
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
// NOLINTBEGIN(readability-magic-numbers)
static void split_sig(sig_num_t out[3], uint64_t i_0, uint64_t span)
{
    if(span == 0)
    {
        int128_t p = ((int128_t)2 * i_0) - 3;
        int128_t q = ((int128_t)8 * i_0);
        int128_t u = (int128_t)1 - ((int128_t)2 * i_0);
        int128_t v = ((int128_t)2 * i_0) + 1;

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
// NOLINTEND(readability-magic-numbers)



static void sig_res_path_set(char path[PATH_MAX_LEN], uint64_t i_0, uint64_t span)
{
    uint64_t i_max = i_0 + B(span) - 1;
    snprintf(path, PATH_MAX_LEN, CACHE "/pieces/p_" U64P(015) "_" U64P(02) "_" U64P(015) ".bin", i_0, span, i_max);
}

static file_t sig_res_open_write(uint64_t i_0, uint64_t span)
{
    char path[PATH_MAX_LEN];
    sig_res_path_set(path, i_0, span);
    return file_write_open(path, 3);
}

static void sig_res_save(sig_num_t res[3], uint64_t i_0, uint64_t span)
{
    file_t fp = sig_res_open_write(i_0, span);
    for(uint64_t i=0; i<3; i++)
    {
        file_write_sig_num(&fp, res[i]);
    }

    file_write_close(&fp);

    sig_num_free(res[0]);
    sig_num_free(res[1]);
    sig_num_free(res[2]);
}

void split_piece(uint64_t i_0, uint64_t span)
{
    sig_num_t res[3];
    split_sig(res, i_0, span);
    sig_res_save(res, i_0, span);
}



static void sig_res_delete(uint64_t i_0, uint64_t span)
{
    char path[PATH_MAX_LEN];
    sig_res_path_set(path, i_0, span);
    remove(path);
}

static FILE* sig_res_try_open_read(uint64_t i_0, uint64_t span)
{
    char path[PATH_MAX_LEN];
    sig_res_path_set(path, i_0, span);
    return file_read_open(path);
}

static bool sig_res_try_load(sig_num_p out, uint64_t i_0, uint64_t span, uint64_t index)
{
    FILE *fp = sig_res_try_open_read(i_0, span);
    if(fp == NULL)
    {
        return false;
    }

    *out = file_read_sig_num(fp, index);
    fclose(fp);
    return true;
}

static sig_num_t sig_res_load(uint64_t i_0, uint64_t span, uint64_t index)
{
    sig_num_t res;
    assert(sig_res_try_load(&res, i_0, span, index));
    return res;
}

static bool sig_res_is_stored(uint64_t i_0, uint64_t span)
{
    FILE *fp = sig_res_try_open_read(i_0, span);
    if(fp == NULL)
    {
        return false;
    }

    fclose(fp);
    return true;
}

// Get the size of the number at the given index (0=P, 1=Q, 2=R)
static uint64_t sig_res_get_size(uint64_t i_0, uint64_t span, uint64_t index)
{
    FILE *fp = sig_res_try_open_read(i_0, span);
    assert(fp);

    file_read_move_to_index(fp, index);
    file_read_uint64(fp);
    uint64_t size = file_read_uint64(fp);
    fclose(fp);

    return size;
}



static void union_res_path_set(
    char path[PATH_MAX_LEN],
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth
)
{
    uint64_t i_max = i_0 + remainder - 1;
    snprintf(path, PATH_MAX_LEN, CACHE "/numbers/u_" U64P(015) "_" U64P(015) "_" U64P(02) "_" U64P(015) ".bin", size, i_0, depth, i_max);
}

static void union_res_delete(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    char path[PATH_MAX_LEN];
    union_res_path_set(path, size, i_0, remainder, depth);
    remove(path);
}

static FILE* union_res_try_open_read(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    char path[PATH_MAX_LEN];
    union_res_path_set(path, size, i_0, remainder, depth);
    return file_read_open(path);
}

static file_t union_res_open_write(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    char path[PATH_MAX_LEN];
    union_res_path_set(path, size, i_0, remainder, depth);
    return file_write_open(path, 3);
}

static union_num_t union_res_load(
    uint64_t size,
    uint64_t i_0,
    uint64_t remainder,
    uint64_t depth,
    uint64_t index
)
{
    FILE *fp = union_res_try_open_read(size, i_0, remainder, depth);
    assert(fp);

    union_num_t u = file_read_union_num(fp, index);
    fclose(fp);
    return u;
}

static bool union_res_is_stored(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    FILE *fp = union_res_try_open_read(size, i_0, remainder, depth);
    if(fp == NULL)
    {
        return false;
    }

    fclose(fp);
    return true;
}

// Exact limb count of a stored union_num at the given index (0=P, 1=Q, 2=R), read
// from its file header instead of loaded in full: a SIG-typed entry's count sits
// right after the signal field; a FLT-typed entry is already fixed-precision at `size`.
static uint64_t union_res_op_size(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth, uint64_t index)
{
    FILE *fp = union_res_try_open_read(size, i_0, remainder, depth);
    assert(fp);

    file_read_move_to_index(fp, index);
    uint64_t type = file_read_uint64(fp);
    file_read_uint64(fp); // union_num.size (fixed working precision, unused here)

    uint64_t op_size = size;
    if(type == SIG)
    {
        file_read_uint64(fp); // sig_num.signal
        op_size = file_read_uint64(fp); // sig_num.count
    }

    fclose(fp);
    return op_size;
}



static void split_span_res_path_set(
    char path[PATH_MAX_LEN],
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
)
{
    union_res_path_set(path, size, i_0, B(span), depth);
}

static void split_span_res_delete(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    char path[PATH_MAX_LEN];
    split_span_res_path_set(path, size, i_0, span, depth);
    remove(path);
}

static union_num_t split_span_res_load(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth,
    uint64_t index
)
{
    sig_num_t sig;
    if(sig_res_try_load(&sig, i_0, span, index))
    {
        return union_num_wrap_sig(sig, size);
    }

    return union_res_load(size, i_0, B(span), depth, index);
}

bool split_span_res_is_stored(
    uint64_t size,
    uint64_t i_0,
    uint64_t span,
    uint64_t depth
)
{
    if(sig_res_is_stored(i_0, span))
    {
        return true;
    }

    uint64_t remainder = B(span);
    return union_res_is_stored(size, i_0, remainder, depth);
}

// Real op size for a span node's already-stored result -- mirrors
// split_span_res_is_stored's SIG-vs-union check but returns the exact size instead.
uint64_t split_span_res_op_size(uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth, uint64_t index)
{
    if(sig_res_is_stored(i_0, span))
    {
        return sig_res_get_size(i_0, span, index);
    }

    uint64_t remainder = B(span);
    return union_res_op_size(size, i_0, remainder, depth, index);
}

static bool split_span_res_is_sig(uint64_t size, uint64_t i_0, uint64_t span)
{
    // size, i_0 , span - 1, depth + 1
    if(!sig_res_is_stored(i_0, span - 1))
    {
        return false;
    }

    // size, i_0 + B(span - 1), span - 1, depth + 1
    if(!sig_res_is_stored(i_0 + B(span - 1), span - 1))
    {
        return false;
    }

    uint64_t size_1 = sig_res_get_size(i_0, span - 1, 0);
    return size_1 < size;
}



// All processes read/write cache/*.bin over the same physical disk; this
// lock turns concurrent I/O from every forked worker into a single queue so
// a spinning disk isn't seeking between unrelated offsets. Held around a
// matched pair of loads (both operands read in before releasing) and around
// each write, but never across a JOIN_MUL -- so multiplication still
// overlaps other processes' I/O. When a write is immediately followed by the
// next term's pair of loads (nothing but a JOIN_HEADER log line in between),
// the lock is kept held across that write-then-read boundary instead of
// being released and immediately re-acquired -- see JOIN_WRITE_HOLD.
static int g_disk_lock_fd = -1;

static void disk_lock(void)
{
    if(g_disk_lock_fd < 0)
    {
        g_disk_lock_fd = open(CACHE "/disk.lock", O_CREAT | O_RDWR, 0644);
        assert(g_disk_lock_fd >= 0);
    }
    int res = flock(g_disk_lock_fd, LOCK_EX);
    assert(res == 0);
}

static void disk_unlock(void)
{
    int res = flock(g_disk_lock_fd, LOCK_UN);
    assert(res == 0);
}

// A join runs four cross multiplications between the two children (P1xP2,
// Q1xQ2, P1xR2, R1xQ2 -- see node_estimate_memory in lib/tree/code.c). Each
// term is announced with a header line first, then broken into its
// individual phases -- loading each operand separately, multiplying, writing
// -- each timed on its own, nested under the caller's overall
// "joining"/"joined" line. INDEX/PID identify the task and process a log
// line came from, so concurrent joins' interleaved output on stderr stays
// attributable.
#define JOIN_HEADER(TERM, INDEX, PID, I_0, SPAN_ARG, DEPTH) \
    tprintf("[" U64P(2) "][%7d][%17.6f] mul %-16s| " U64P(10) " " U64P(10) " " U64P(3) "", INDEX, PID, get_wall_time(), TERM, I_0, SPAN_ARG, DEPTH)

#define JOIN_PHASE(BEGIN, END, INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT)                                               \
    do {                                                                                                             \
        tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", INDEX, PID, get_wall_time(), BEGIN, I_0, SPAN_ARG, DEPTH); \
        TIME_SETUP                                                                                                   \
        STMT                                                                                                         \
        TIME_END(_t)                                                                                                 \
        tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", INDEX, PID, get_wall_time(), END, I_0, SPAN_ARG, DEPTH, dtime(_t)); \
    } while(0)

#define JOIN_LOAD_LABELED(BEGIN, END, LABEL, INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT)                                              \
    do {                                                                                                                          \
        tprintf("[" U64P(2) "][%7d][%17.6f] %-11s%-9s| " U64P(10) " " U64P(10) " " U64P(3) "", INDEX, PID, get_wall_time(), BEGIN, LABEL, I_0, SPAN_ARG, DEPTH); \
        TIME_SETUP                                                                                                                \
        STMT                                                                                                                      \
        TIME_END(_t)                                                                                                              \
        tprintf("[" U64P(2) "][%7d][%17.6f] %-11s%-9s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", INDEX, PID, get_wall_time(), END, LABEL, I_0, SPAN_ARG, DEPTH, dtime(_t)); \
    } while(0)

#define JOIN_LOAD(OP, INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT) JOIN_LOAD_LABELED("loading", "loaded", OP, INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT)
#define JOIN_MUL(INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT)      JOIN_PHASE("multiplying", "multiplied", INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT)

// Timed like any other phase, so a long "locking" -> "locked" gap in the log
// (and in the dashboard's join_micro column) reads as lock contention rather
// than silently vanishing into disk_lock()'s blocking flock() call.
#define JOIN_LOCK(INDEX, PID, I_0, SPAN_ARG, DEPTH) JOIN_PHASE("locking", "locked", INDEX, PID, I_0, SPAN_ARG, DEPTH, disk_lock();)

#define JOIN_WRITE(INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT) \
    do { \
        JOIN_LOCK(INDEX, PID, I_0, SPAN_ARG, DEPTH); \
        JOIN_PHASE("writing", "written", INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT); \
        disk_unlock(); \
    } while(0)

// Like JOIN_WRITE, but leaves the lock held instead of releasing it: for a
// write immediately followed (across just a JOIN_HEADER log line, no I/O in
// between) by the next term's operand reads, so that write-then-read pair
// stays under one continuous lock hold instead of unlocking and immediately
// re-locking. The paired read must skip its own JOIN_LOCK and release the
// lock itself (still via a plain disk_unlock()) once its loads are done.
#define JOIN_WRITE_HOLD(INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT) \
    do { \
        JOIN_LOCK(INDEX, PID, I_0, SPAN_ARG, DEPTH); \
        JOIN_PHASE("writing", "written", INDEX, PID, I_0, SPAN_ARG, DEPTH, STMT); \
    } while(0)

void split_span_res_join(uint64_t index, uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    int pid = (int)getpid();

    if(split_span_res_is_sig(size, i_0, span))
    {
        file_t fp = sig_res_open_write(i_0, span);
        static const char *const p_terms[2] = {"P1xP2", "Q1xQ2"};
        static const char *const p_op_1[2] = {"P1", "Q1"};
        static const char *const p_op_2[2] = {"P2", "Q2"};
        for(uint64_t i=0; i<2; i++)
        {
            JOIN_HEADER(p_terms[i], index, pid, i_0, span, depth);

            sig_num_t sig_1;
            sig_num_t sig_2;
            if(i == 0)
            {
                JOIN_LOCK(index, pid, i_0, span, depth);
            }
            JOIN_LOAD(p_op_1[i], index, pid, i_0, span, depth,
                sig_1 = sig_res_load(i_0, span - 1, i);
            );
            JOIN_LOAD(p_op_2[i], index, pid, i_0, span, depth,
                sig_2 = sig_res_load(i_0 + B(span - 1), span - 1, i);
            );
            disk_unlock();

            sig_num_t sig;
            JOIN_MUL(index, pid, i_0, span, depth,
                sig = sig_num_mul(sig_1, sig_2);
            );

            JOIN_WRITE_HOLD(index, pid, i_0, span, depth,
                file_write_sig_num(&fp, sig);
            );
            sig_num_free(sig);
        }

        JOIN_HEADER("P1xR2", index, pid, i_0, span, depth);

        sig_num_t sig_1;
        sig_num_t sig_2;
        JOIN_LOAD("P1", index, pid, i_0, span, depth,
            sig_1 = sig_res_load(i_0, span - 1, 0);
        );
        JOIN_LOAD("R2", index, pid, i_0, span, depth,
            sig_2 = sig_res_load(i_0 + B(span - 1), span - 1, 2);
        );
        disk_unlock();

        sig_num_t sig_r_1;
        JOIN_MUL(index, pid, i_0, span, depth,
            sig_r_1 = sig_num_mul(sig_1, sig_2);
        );

        JOIN_HEADER("R1xQ2", index, pid, i_0, span, depth);

        JOIN_LOCK(index, pid, i_0, span, depth);
        JOIN_LOAD("R1", index, pid, i_0, span, depth,
            sig_1 = sig_res_load(i_0, span - 1, 2);
        );
        JOIN_LOAD("Q2", index, pid, i_0, span, depth,
            sig_2 = sig_res_load(i_0 + B(span - 1), span - 1, 1);
        );
        disk_unlock();

        sig_num_t sig_r_2;
        JOIN_MUL(index, pid, i_0, span, depth,
            sig_r_2 = sig_num_mul(sig_1, sig_2);
        );
        JOIN_WRITE(index, pid, i_0, span, depth,
            sig_r_1 = sig_num_add(sig_r_1, sig_r_2);
            file_write_sig_num(&fp, sig_r_1);
        );
        sig_num_free(sig_r_1);

        file_write_close(&fp);

        sig_res_delete(i_0, span - 1);
        sig_res_delete(i_0 + B(span - 1), span - 1);
        return;
    }

    file_t fp = union_res_open_write(size, i_0, B(span), depth);
    static const char *const p_terms[2] = {"P1xP2", "Q1xQ2"};
    static const char *const p_op_1[2] = {"P1", "Q1"};
    static const char *const p_op_2[2] = {"P2", "Q2"};
    for(uint64_t i=0; i<2; i++)
    {
        JOIN_HEADER(p_terms[i], index, pid, i_0, span, depth);

        union_num_t u_1;
        union_num_t u_2;
        if(i == 0)
        {
            JOIN_LOCK(index, pid, i_0, span, depth);
        }
        JOIN_LOAD(p_op_1[i], index, pid, i_0, span, depth,
            u_1 = split_span_res_load(size, i_0, span - 1, depth + 1, i);
        );
        JOIN_LOAD(p_op_2[i], index, pid, i_0, span, depth,
            u_2 = split_span_res_load(size, i_0 + B(span - 1), span - 1, depth + 1, i);
        );
        disk_unlock();

        union_num_t u;
        JOIN_MUL(index, pid, i_0, span, depth,
            u = union_num_mul(u_1, u_2);
        );

        JOIN_WRITE_HOLD(index, pid, i_0, span, depth,
            file_write_union_num(&fp, u);
        );
        union_num_free(u);
    }

    JOIN_HEADER("P1xR2", index, pid, i_0, span, depth);

    union_num_t u_1;
    union_num_t u_2;
    JOIN_LOAD("P1", index, pid, i_0, span, depth,
        u_1 = split_span_res_load(size, i_0, span - 1, depth + 1, 0);
    );
    JOIN_LOAD("R2", index, pid, i_0, span, depth,
        u_2 = split_span_res_load(size, i_0 + B(span - 1), span - 1, depth + 1, 2);
    );
    disk_unlock();

    union_num_t u_r_1;
    JOIN_MUL(index, pid, i_0, span, depth,
        u_r_1 = union_num_mul(u_1, u_2);
        if(araucaria_disk_config_is_set())
        {
            u_r_1 = union_num_realloc_disk(u_r_1);
        }
    );

    JOIN_HEADER("R1xQ2", index, pid, i_0, span, depth);

    JOIN_LOCK(index, pid, i_0, span, depth);
    JOIN_LOAD("R1", index, pid, i_0, span, depth,
        u_1 = split_span_res_load(size, i_0, span - 1, depth + 1, 2);
    );
    JOIN_LOAD("Q2", index, pid, i_0, span, depth,
        u_2 = split_span_res_load(size, i_0 + B(span - 1), span - 1, depth + 1, 1);
    );
    disk_unlock();

    union_num_t u_r_2;
    JOIN_MUL(index, pid, i_0, span, depth,
        u_r_2 = union_num_mul(u_1, u_2);
    );
    JOIN_WRITE(index, pid, i_0, span, depth,
        u_r_1 = union_num_add(u_r_1, u_r_2);
        file_write_union_num(&fp, u_r_1);
    );
    union_num_free(u_r_1);

    file_write_close(&fp);

    split_span_res_delete(size, i_0, span - 1, depth + 1);
    split_span_res_delete(size, i_0 + B(span - 1), span - 1, depth + 1);
}

// out vector length 3, returns P, Q, R in that order
static void split_span(uint64_t index, uint64_t size, uint64_t i_0, uint64_t span, uint64_t depth)
{
    int pid = (int)getpid();

    assert(span >= PIECE_SIZE);
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, span, depth);

    if(split_span_res_is_stored(size, i_0, span, depth))
    {
        return;
    }

    if(span == PIECE_SIZE)
    {
        split_piece(i_0, span);
        return;
    }

    split_span(index, size, i_0              , span - 1, depth + 1);
    split_span(index, size, i_0 + B(span - 1), span - 1, depth + 1);

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "joining", i_0, span, depth);
    TIME_SETUP
    split_span_res_join(index, size, i_0, span, depth);
    TIME_END(t1)
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, span, depth, dtime(t1));
}



static union_num_t split_big_res_load(
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

    return union_res_load(size, i_0, remainder, depth, index);
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

// Real op size for a big node's already-stored result -- mirrors
// split_big_res_is_stored's span-collapse check but returns the exact size instead.
uint64_t split_big_res_op_size(uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth, uint64_t index)
{
    if(stdc_count_ones(remainder) == 1)
    {
        uint64_t span = stdc_bit_width(remainder) - 1;
        return split_span_res_op_size(size, i_0, span, depth, index);
    }

    return union_res_op_size(size, i_0, remainder, depth, index);
}

void split_big_res_join(uint64_t index, uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    int pid = (int)getpid();

    file_t fp = union_res_open_write(size, i_0, remainder, depth);

    uint64_t span = stdc_bit_width(remainder) - 1;
    static const char *const p_terms[2] = {"P1xP2", "Q1xQ2"};
    static const char *const p_op_1[2] = {"P1", "Q1"};
    static const char *const p_op_2[2] = {"P2", "Q2"};
    for(uint64_t i=0; i<2; i++)
    {
        JOIN_HEADER(p_terms[i], index, pid, i_0, remainder, depth);

        union_num_t u_1;
        union_num_t u_2;
        if(i == 0)
        {
            JOIN_LOCK(index, pid, i_0, remainder, depth);
        }
        JOIN_LOAD(p_op_1[i], index, pid, i_0, remainder, depth,
            u_1 = split_span_res_load(size, i_0, span, depth + 1, i);
        );
        JOIN_LOAD(p_op_2[i], index, pid, i_0, remainder, depth,
            u_2 = split_big_res_load(size, i_0 + B(span), remainder - B(span), depth + 1, i);
        );
        disk_unlock();

        union_num_t u;
        JOIN_MUL(index, pid, i_0, remainder, depth,
            u = union_num_mul(u_1, u_2);
        );

        JOIN_WRITE_HOLD(index, pid, i_0, remainder, depth,
            file_write_union_num(&fp, u);
        );
        union_num_free(u);
    }

    JOIN_HEADER("P1xR2", index, pid, i_0, remainder, depth);

    union_num_t u_1;
    union_num_t u_2;
    JOIN_LOAD("P1", index, pid, i_0, remainder, depth,
        u_1 = split_span_res_load(size, i_0, span, depth + 1, 0);
    );
    JOIN_LOAD("R2", index, pid, i_0, remainder, depth,
        u_2 = split_big_res_load(size, i_0 + B(span), remainder - B(span), depth + 1, 2);
    );
    disk_unlock();

    union_num_t u_r_1;
    JOIN_MUL(index, pid, i_0, remainder, depth,
        u_r_1 = union_num_mul(u_1, u_2);
        if(araucaria_disk_config_is_set())
        {
            u_r_1 = union_num_realloc_disk(u_r_1);
        }
    );

    JOIN_HEADER("R1xQ2", index, pid, i_0, remainder, depth);

    JOIN_LOCK(index, pid, i_0, remainder, depth);
    JOIN_LOAD("R1", index, pid, i_0, remainder, depth,
        u_1 = split_span_res_load(size, i_0, span, depth + 1, 2);
    );
    JOIN_LOAD("Q2", index, pid, i_0, remainder, depth,
        u_2 = split_big_res_load(size, i_0 + B(span), remainder - B(span), depth + 1, 1);
    );
    disk_unlock();

    union_num_t u_r_2;
    JOIN_MUL(index, pid, i_0, remainder, depth,
        u_r_2 = union_num_mul(u_1, u_2);
    );

    union_num_t u;
    JOIN_WRITE(index, pid, i_0, remainder, depth,
        u = union_num_add(u_r_1, u_r_2);
        file_write_union_num(&fp, u);
    );
    union_num_free(u);

    file_write_close(&fp);

    split_span_res_delete(size, i_0, span, depth + 1);
    union_res_delete(size, i_0 + B(span), remainder - B(span), depth + 1);
}

// out vector length 3, returns P, Q, R in that order
static void split_big(uint64_t index, uint64_t size, uint64_t i_0, uint64_t remainder, uint64_t depth)
{
    int pid = (int)getpid();

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "begin", i_0, remainder, depth);

    if(split_big_res_is_stored(size, i_0, remainder, depth))
    {
        return;
    }

    uint64_t span = stdc_bit_width(remainder) - 1;
    if(stdc_count_ones(remainder) == 1)
    {
        split_span(index, size, i_0, span, depth);
        return;
    }

    split_span(index, size, i_0, span, depth + 1);
    split_big(index, size, i_0 + B(span), remainder - B(span), depth + 1);

    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) "", index, pid, get_wall_time(), "joining", i_0, span, depth);
    TIME_SETUP
    split_big_res_join(index, size, i_0, remainder, depth);
    TIME_END(t1)
    tprintf("[" U64P(2) "][%7d][%17.6f] %-20s| " U64P(10) " " U64P(10) " " U64P(3) " | %7.1f", index, pid, get_wall_time(), "joined", i_0, span, depth, dtime(t1));
}



static void pi_path_set(char path[PATH_MAX_LEN], uint64_t size)
{
    snprintf(path, PATH_MAX_LEN, CACHE "/res/pi_" U64P(015) ".bin", size);
}

static void pi_save(uint64_t size, flt_num_t flt_pi)
{
    char name[PATH_MAX_LEN];
    pi_path_set(name, size);
    flt_num_save(name, flt_pi);
}

bool pi_is_stored(uint64_t size)
{
    char name[PATH_MAX_LEN];
    pi_path_set(name, size);
    FILE *fp = file_read_open(name);
    if(fp == NULL)
    {
        return false;
    }

    fclose(fp);
    return true;
}

flt_num_t pi_load(uint64_t size)
{
    char name[PATH_MAX_LEN];
    pi_path_set(name, size);
    return flt_num_load(name);
}

uint64_t get_index_max(uint64_t size, uint64_t piece_size)
{
    // NOLINTNEXTLINE(readability-magic-numbers)
    uint64_t index_max = (32 * size) + 4;
    uint64_t aux = index_max & (B(piece_size) - 1);
    if(aux == 0)
    {
        return index_max;
    }

    return index_max + B(piece_size) - aux;
}

flt_num_t pi_finish(uint64_t size, uint64_t piece_size)
{
    uint64_t index_max = get_index_max(size, piece_size);

    union_num_t u_q = split_big_res_load(size, 1, index_max, 0, 1);
    union_num_t u_r = split_big_res_load(size, 1, index_max, 0, 2);

    flt_num_t flt_q = union_num_unwrap_flt(u_q);
    flt_num_t flt_r = union_num_unwrap_flt(u_r);

    flt_num_t flt_pi = flt_r;
    flt_pi = flt_num_mul_sig(flt_pi, sig_num_wrap(3));

    tprintf("              %-20s|", "dividing");
    TIME_SETUP
    flt_pi = flt_num_div(flt_pi, flt_q);
    TIME_END(t1)
    tprintf("              %-20s| %7.1f", "divided", dtime(t1));

    flt_pi = flt_num_add(flt_pi, flt_num_wrap(3, size));
    pi_save(size, flt_pi);
    union_res_delete(size, 1, index_max, 0);
    return flt_pi;
}

flt_num_t pi_big(uint64_t size)
{
    if(pi_is_stored(size))
    {
        tprintf("              %-20s|", "pi already stored");
        return pi_load(size);
    }

    uint64_t index_max = get_index_max(size, PIECE_SIZE);
    split_big(0, size, 1, index_max, 0);
    tprintf("              %-20s|", "binary split solved");

    return pi_finish(size, PIECE_SIZE);
}

void prepare(
    uint64_t span,
    uint64_t begin,
    uint64_t end,
    uint64_t n_process
)
{
    printf("\nspan: " U64P() "\tbegin: " U64P() "\tend: " U64P() "", span, begin, end);
    printf("\ni_0: " U64P() "\ti_max: " U64P() "", (begin * B(span)) + 1, (end + 1) * B(span));


    constexpr uint64_t max_process = 16;
    assert(n_process <= max_process);

    pid_t pid[max_process];
    for(uint64_t i=0; i<n_process; i++)
    {
        pid[i] = fork_safe();
        if(pid[i])
        {
            continue;
        }

        for(uint64_t j=begin + i; j<end; j += n_process)
        {
            printf("\ni: " U64P() " / " U64P() "", j, end);

            uint64_t i_0 = (j * B(span)) + 1;
            split_span(i, UINT64_MAX, i_0, span, 0);
        }

        fprintf(stderr, "\nprocess " U64P() " finished\n", i);
        exit(EXIT_SUCCESS);
    }

    for(uint64_t i=0; i<n_process; i++)
    {
        waitpid_safe(pid[i], NULL);
    }
}
