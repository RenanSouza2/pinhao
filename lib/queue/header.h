#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <stdbool.h>

#include "struct.h"

queue_t queue_init(uint64_t res_max, uint64_t res_size, free_f res_free);
uint64_t queue_get_occupancy(queue_p q);
void queue_send(queue_p q, handler_p res, bool volatile * is_idle);
void queue_recv(queue_p q, handler_p out_res, bool volatile * is_idle);
bool queue_unstuck(queue_p q, handler_p h);
void queue_free(queue_p q);

#endif
