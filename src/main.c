#include <stdio.h>

#include "../mods/clu/header.h"
#include "../mods/macros/assert.h"
#include "../mods/macros/time.h"

#include "../lib/pi/header.h"
#include "../lib/linear/header.h"



void time_1()
{
    for(uint64_t i=1000; i <= 65000; i+=1000)
    {
        printf("\n%lu", i);
        TIME_SETUP
        fix_num_t flt = pi_v1(i);
        TIME_END(t3)
        printf("\t%.2f", t3/1e9);
        fix_num_free(flt);
    }
}

// int main(int argc, char** argv)
int main()
{
    setbuf(stdout, NULL);

    // uint64_t size = 1000;
    // // uint64_t size = 20000000;
    // // float_num_t flt_1 = pi_threads(size, 8, 0);
    // // printf("\n\n");float_num_display_dec(flt_1);
    // // float_num_free(flt_1);

    time_1();

    printf("\n");
    return 0;
}
