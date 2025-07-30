#ifndef __LINEAR_STRUCT_H__
#define __LINEAR_STRUCT_H__

#include "../../mods/number/header.h"

STRUCT(union_num)
{
    uint64_t type;
    uint64_t size;
    union
    {
        sig_num_t sig;
        float_num_t flt;
    } num;
};

#endif
