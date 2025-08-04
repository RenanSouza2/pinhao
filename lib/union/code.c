#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/uint.h"
#include "../../mods/number/lib/num/struct.h"



#ifdef DEBUG
#endif



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

        case FLT:
        {
            printf("FLT | ");flt_num_display_dec(u.num.flt);
            return;
        }
        break;
    }
    exit(EXIT_FAILURE);
}

void union_num_display_full(char tag[], union_num_t u)
{
    printf("\n%s: ", tag);
    switch (u.type)
    {
        case SIG:
        {
            printf("SIG |");
            num_display_opts(u.num.sig.num, NULL, true, true);
            return;
        }
        break;

        case FLT:
        {
            printf("FLT | exponent: %ld | ", u.num.flt.exponent);
            num_display_opts(u.num.flt.sig.num, NULL, true, true);
            return;
        }
        break;
    }
    exit(EXIT_FAILURE);
}



union_num_t union_num_wrap_flt(flt_num_t flt, uint64_t size)
{
    return (union_num_t)
    {
        .type = FLT,
        .size = size,
        .num = {.flt = flt}
    };
}

union_num_t union_num_wrap_sig(sig_num_t sig, uint64_t size)
{
    if(sig.num->count >= size)
    {
        flt_num_t flt = flt_num_wrap_sig(sig, size);
        return union_num_wrap_flt(flt, size);
    }

    return (union_num_t)
    {
        .type = SIG,
        .size = size,
        .num = {.sig = sig}
    };
}

flt_num_t union_num_unwrap_flt(union_num_t u)
{
    switch (u.type)
    {
        case SIG: return flt_num_wrap_sig(u.num.sig, u.size);
        case FLT: return u.num.flt;
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

        case FLT:
        {
            flt_num_t flt = flt_num_copy(u.num.flt);
            return union_num_wrap_flt(flt, u.size);
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

        case FLT:
        {
            flt_num_free(u.num.flt);
            return;
        }
        break;
    }
    exit(EXIT_FAILURE);
}



void union_num_file_write(FILE *fp, union_num_t u)
{
    fprintf(fp, " %lx %lx", u.type, u.size);
    switch (u.type)
    {
        case SIG:
        sig_num_file_write(fp, u.num.sig);
        break;

        case FLT:
        flt_num_file_write(fp, u.num.flt);
        break;
    
        default: assert(false);
    }
}

void union_num_file_read(FILE *fp, union_num_p u)
{
    uint64_t type, size;
    assert(fscanf(fp, "%lx %lx", &type, &size) == 2);
    switch (type)
    {
        case SIG:
        {
            sig_num_t sig = sig_num_file_read(fp);
            *u = union_num_wrap_sig(sig, size);
            return;
        }

        case FLT:
        {
            flt_num_t flt = flt_num_file_read(fp);
            *u = union_num_wrap_flt(flt, size);
            return;
        }
    }
    assert(false);
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

                case FLT:
                {
                    flt_num_t flt = flt_num_add(flt_num_wrap_sig(u_1.num.sig, u_1.size), u_2.num.flt);
                    return union_num_wrap_flt(flt, u_1.size);
                }
                break;
            }
        }
        break;

        case FLT:
        {
            switch (u_2.type)
            {
                case SIG:
                {
                    flt_num_t flt = flt_num_add(u_1.num.flt, flt_num_wrap_sig(u_2.num.sig, u_1.size));
                    return union_num_wrap_flt(flt, u_1.size);
                }
                break;

                case FLT:
                {
                    flt_num_t flt = flt_num_add(u_1.num.flt, u_2.num.flt);
                    return union_num_wrap_flt(flt, u_1.size);
                }
                break;
            }
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

                case FLT:
                {
                    flt_num_t flt = flt_num_mul_sig(u_2.num.flt, u_1.num.sig);
                    return union_num_wrap_flt(flt, u_1.size);
                }
                break;
            }
        }
        break;

        case FLT:
        {
            switch (u_2.type)
            {
                case SIG:
                {
                    flt_num_t flt = flt_num_mul_sig(u_1.num.flt, u_2.num.sig);
                    return union_num_wrap_flt(flt, u_1.size);
                }
                break;

                case FLT:
                {
                    flt_num_t flt = flt_num_mul(u_1.num.flt, u_2.num.flt);
                    return union_num_wrap_flt(flt, u_1.size);
                }
                break;
            }
        }
        break;
    }
    exit(EXIT_FAILURE);
}
