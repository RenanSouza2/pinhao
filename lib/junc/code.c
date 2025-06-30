#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/U64.h"

#include "../queue/header.h"



#ifdef DEBUG
#endif



junc_t junc_init(uint64_t total, uint64_t res_max, uint64_t res_size, free_f res_free)
{
    queue_p queues = malloc(total * sizeof(queue_t));
    for(uint64_t i=0; i<total; i++)
        queues[i] = queue_init(res_max, res_size, res_free);

    return (junc_t)
    {
        .total = total,
        .index = 0,
        .queues = queues
    };
}

void junc_free(junc_p junc)
{
    for(uint64_t i=0; i<junc->total; i++)
        queue_free(&junc->queues[i]);

    free(junc->queues);
}

void junc_get(junc_p junc, handler_p out_res, bool volatile * is_idle)
{
    uint64_t index = junc->index;
    junc->index++;
    if(junc->index == junc->total)
        junc->index = 0;
    queue_get(&junc->queues[index], out_res, is_idle);
}

void junc_post(junc_p junc, handler_p res, bool volatile * is_idle)
{
    uint64_t index = junc->index;
    junc->index++;
    if(junc->index == junc->total)
        junc->index = 0;
    queue_post(&junc->queues[index], res, is_idle);
}
