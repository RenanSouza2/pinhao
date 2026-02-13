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

    // uint64_t size = 100 * 1000 * 1000;
    uint64_t size = 100 * 1000 * 1000;

    // pi(size);

    prepare(size, 16, 30000, 48000, 8);
    // prepare(size, 20, 1000, 3000);

    printf("\n");
    return 0;
}
