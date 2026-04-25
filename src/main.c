#include <stdio.h>

#include "../mods/clu/header.h"
#include "../mods/macros/assert.h"
#include "../mods/macros/fork.h"
#include "../mods/macros/time.h"
#include "../mods/number/lib/num/struct.h"

#include "../lib/big/header.h"
// #include "../lib/linear/linear/header.h"



void pi(uint64_t size)
{
    flt_num_t flt_pi = pi_big(size);
    printf("\n\n");flt_num_display_dec(flt_pi);
    flt_num_free(flt_pi);
}

// int main(int argc, char** argv)
int main(void)
{
    setbuf(stdout, NULL);
    printf("\nbegin");

    // clu_log_level_set(CLU_LOG_DYNAMIC);

    pi(1024 * 1024);

    // prepare(16, 48000, 48829, 6);
    // prepare(17, 24408, 24414, 6);
    // prepare(18, 12204, 12207, 6);
    // prepare(19, 6102, 6103, 6);
    // prepare(20, 0, 30, 6);
    // prepare(21, 2, 15, 6);
    // prepare(22, 0, 76, 6);
    // prepare(23, 4, 38, 6);
    // prepare(24, 0, 19, 6);
    // prepare(25, 94, 95, 6);
    // prepare(26, 60, 476, 12);
    // prepare(27, 24, 238, 1);
    // prepare(28, 1, 119, 12);

    // printf("\n\nmax_occupancy: : " U64P() "", clu_get_max_occupancy());
    // assert(clu_mem_is_empty());

    // flt_num_t flt_pi = pi_v3(1024 * 1024);
    // flt_num_display_dec(flt_pi);
    // flt_num_free(flt_pi);

    printf("\n");
    return 0;
}

// gcc 13

// v10, new lib: 34m32.111s
// v9, old lib: 35m42.555s
// v9: new lib: 32m55.631s
