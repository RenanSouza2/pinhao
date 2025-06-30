#ifndef __QUEUE_STRUCT_H__
#define __QUEUE_STRUCT_H__

#include <semaphore.h>

#include "../../mods/macros/struct.h"
#include "../../mods/macros/U64.h"

typedef void (*free_f)(handler_p, uint64_t);

STRUCT(queue)
{
    sem_t sem_f;
    sem_t sem_b;

    uint64_t res_max;
    uint64_t res_size;
    handler_p *res;

    uint64_t start;
    uint64_t end;

    free_f res_free;
};

#endif
