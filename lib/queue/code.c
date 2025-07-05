#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/U64.h"
#include "../../mods/macros/threads.h"

#include "../pear/header.h"



#ifdef DEBUG
#endif



queue_t queue_init(uint64_t res_max, uint64_t res_size, free_f res_free)
{
    assert(res_max > 1);

    sem_t sem_f, sem_b;
    sem_init(&sem_f, 0, 0);
    sem_init(&sem_b, 0, res_max);

    handler_p *res = malloc(res_max * sizeof(handler_p));
    assert(res);

    for(uint64_t i=0; i<res_max; i++)
    {
        res[i] = malloc(res_size);
        assert(res[i]);
    }

    return (queue_t)
    {
        .sem_f = sem_f,
        .sem_b = sem_b,
        .res_max = res_max,
        .res_size = res_size,
        .res = res,
        .start = 0,
        .end = 0,
        .res_free = res_free
    };
}

uint64_t queue_get_occupancy(queue_p q)
{
    return sem_getvalue_treat(&q->sem_f);
}

void queue_send(queue_p q, handler_p res, bool volatile * is_idle)
{
    sem_wait_log(&q->sem_b, is_idle);
    uint64_t index = q->end;
    q->end++;
    if(q->end == q->res_max)
        q->end = 0;
    memcpy(q->res[index], res, q->res_size);
    TREAT(sem_post(&q->sem_f));
}

void queue_recv(queue_p q, handler_p out_res, bool volatile * is_idle)
{
    sem_wait_log(&q->sem_f, is_idle);
    handler_p res = q->res[q->start];
    memcpy(out_res, res, q->res_size);
    q->start++;
    if(q->start == q->res_max)
        q->start = 0;
    TREAT(sem_post(&q->sem_b));
}

/* returns true if H ownership returns to caller */
bool queue_unstuck(queue_p q, handler_p h)
{
    if(sem_getvalue_treat(&q->sem_f) == 0)
    {
        queue_send(q, h, NULL);
        return false;
    }

    if(sem_getvalue_treat(&q->sem_b) == 0)
    {
        q->res_free(h, q->res_size);
        queue_recv(q, h, NULL);
        return true;
    }

    return true;
}

void queue_free(queue_p q)
{
    handler_p h = malloc(q->res_size);
    assert(h);

    while(queue_get_occupancy(q))
    {
        queue_recv(q, h, NULL);
        q->res_free(h, q->res_size);
    }
    free(h);

    for(uint64_t i=0; i<q->res_max; i++)
        free(q->res[i]);

    free(q->res);

    TREAT(sem_destroy(&q->sem_f));
    TREAT(sem_destroy(&q->sem_b));
}
