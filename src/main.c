#include <stdio.h>

#include "../mods/clu/header.h"
// #include "../mods/macros/assert.h"
// #include "../mods/macros/time.h"

#include "../lib/pi/header.h"



// int main(int argc, char** argv)
int main()
{
    setbuf(stdout, NULL);

    uint64_t size = 5000;
    fix_num_t fix_pi = pi_threads(size, 8);

    printf("\n\n");
    fix_num_display_dec(fix_pi);
    fix_num_free(fix_pi);

    printf("\n");
    return 0;
}
