#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/struct.h"
#include "../../mods/macros/threads.h"
#include "../../mods/macros/U64.h"



#ifdef DEBUG
#endif



pthread_t pthread_create_treat(pthread_f fn, handler_p args)
{
    pthread_t thread_id;
    TREAT(pthread_create(&thread_id, NULL, fn, args));
    return thread_id;
}

void pthread_lock(pthread_t thread_id, uint64_t cpu)
{
    cpu_set_t cpuset;
    __CPU_ZERO_S(sizeof (cpu_set_t), &cpuset);
    __CPU_SET_S(cpu, sizeof (cpu_set_t), &cpuset);
    TREAT(pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), &cpuset));
}

void pthread_join_treat(pthread_t thread_id)
{
    TREAT(pthread_join(thread_id, NULL));
}

uint64_t sem_getvalue_treat(sem_t *sem)
{
    int value;
    TREAT(sem_getvalue(sem, &value));
    return value;
}

void sem_wait_log(sem_t *sem, bool volatile * is_idle)
{
    if(is_idle)
    {
        if(sem_trywait(sem))
        {
            *is_idle = true;
            TREAT(sem_wait(sem));
            *is_idle = false;
        }
    } else {
        TREAT(sem_wait(sem));
    }
}
