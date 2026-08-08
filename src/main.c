#include <stdio.h>

#include "../mods/clu/header.h" // IWYU pragma: keep
// #include "../mods/macros/assert.h"
// #include "../mods/macros/fork.h"
// #include "../mods/macros/time.h"
// #include "../mods/araucaria/lib/num/struct.h"

// #define CACHE "/mnt/wsl/external_workspace/cache"
// #include "../lib/linear/linear/header.h"
// #include "../lib/big/header.h"
#include "../lib/tree/header.h"



[[maybe_unused]]
static void pi(uint64_t size)
{
    flt_num_t flt_pi = pi_tree(size, 1);
    printf("\n\n");flt_num_display_dec(flt_pi);
    flt_num_free(flt_pi);
}

// int main(int argc, char** argv)
int main(void)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("\nbegin");

    // char disk_path[] = "./cache/tmp";
    // char disk_path[] = "/mnt/wsl/external_workspace/cache/tmp";
    // num_config_t config = {
    //     .disk_path = disk_path,
    //     .disk_threshold = (uint64_t)1024 * 1024 * 1024
    // };
    // num_config_set(&config);

    pi(1'000'000);

    printf("\n");
    return 0;
}
