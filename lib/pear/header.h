#ifndef __PEAR_H__
#define __PEAR_H__

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "../../mods/macros/U64.h"

#include "struct.h"

pthread_t pthread_create_treat(pthread_f fn, handler_p args);
void pthread_lock(pthread_t thread_id, uint64_t cpu);
void pthread_join_treat(pthread_t thread_id);
uint64_t sem_getvalue_treat(sem_t *sem);
void sem_wait_log(sem_t *sem, bool volatile * is_idle);

#endif
