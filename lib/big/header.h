#ifndef BIG_H
#define BIG_H

#include "../../mods/number/header.h"
#include "../union/struct.h"

flt_num_t pi_big(uint64_t size);
void prepare(
    uint64_t span,
    uint64_t begin,
    uint64_t end,
    uint64_t n_process
);

#endif
