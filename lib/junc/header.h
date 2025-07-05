#ifndef __JUNC_H__
#define __JUNC_H__

#include <stdbool.h>

#include "struct.h"
#include "../queue/struct.h"

junc_t junc_init(uint64_t total, uint64_t res_max, uint64_t res_size, free_f res_free);
void junc_free(junc_p junc);
void junc_recv(junc_p junc, handler_p out_res, bool volatile * is_idle);
void junc_send(junc_p junc, handler_p res, bool volatile * is_idle);

#endif
