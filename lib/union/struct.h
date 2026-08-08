#ifndef UNION_STRUCT_H
#define UNION_STRUCT_H

#include "../../mods/araucaria/header.h"

#define SIG 0
#define FLT 1

STRUCT(union_num)
{
    uint64_t type;
    uint64_t size;
    union
    {
        sig_num_t sig;
        flt_num_t flt;
    } num;
};

#endif
