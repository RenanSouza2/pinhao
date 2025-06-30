#ifndef __JUNC_STRUCT_H__
#define __JUNC_STRUCT_H__

#include "../../mods/macros/struct.h"
#include "../../mods/macros/U64.h"

PLACEHOLDER(queue);

STRUCT(junc)
{
    uint64_t total;
    uint64_t index;
    queue_p queues;
};

#endif
