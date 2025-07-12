#ifndef __PEAR_H__
#define __PEAR_H__

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "../../mods/macros/U64.h"

#include "struct.h"

void dbg(char format[], ...);

pthread_t pthread_create_treat(pthread_f fn, handler_p args);
handler_p pthread_join_treat(pthread_t thread_id);
void pthread_lock(pthread_t thread_id, uint64_t cpu);

#endif
