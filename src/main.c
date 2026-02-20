#include <stdio.h>

#include "../mods/clu/header.h"
#include "../mods/macros/assert.h"
#include "../mods/macros/time.h"
#include "../mods/number/lib/num/struct.h"

#include "../lib/big/header.h"
#include "../lib/split/header.h"
#include "../lib/linear/header.h"
#include "../lib/union/header.h"



void time_1(void)
{
    for(uint64_t i=1000; i <= 65000; i+=1000)
    {
        printf("\n" U64P() "", i);
        TIME_SETUP
        fxd_num_t flt = pi_v1(i);
        TIME_END(t3)
        printf("\t%.2f", (double)t3 / 1e9);
        fxd_num_free(flt);
    }
}

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

    uint64_t size = 100 * 1000 * 1000;

    // pi(size);

    // prepare(size, 16, 48000, 48829, 6);
    // prepare(size, 17, 24408, 24414, 6);
    // prepare(size, 18, 12204, 12207, 6);
    // prepare(size, 19, 6102, 6103, 6);
    // prepare(size, 20, 3000, 3051, 6);
    // prepare(size, 21, 1520, 1525, 6);
    // prepare(size, 22, 760, 762, 6);
    // prepare(size, 23, 380, 381, 6);
    // prepare(size, 24, 192, 1904, 12);
    // prepare(size, 25, 94, 95, 6);
    // prepare(size, 26, 48, 476, 2);
    // prepare(size, 27, 14, 23, 2);
    // prepare(2 * size, 28, 0, 12, 1);

    printf("\n");
    return 0;
}
