#include <stdio.h>

#include "../mods/clu/header.h" // IWYU pragma: keep
#include "../mods/macros/assert.h" // IWYU pragma: keep
// #include "../mods/macros/fork.h"
#include "../mods/macros/time.h"
#include "../mods/araucaria/lib/num/struct.h"

// #define CACHE "/mnt/wsl/external_workspace/cache"
// #include "../lib/linear/linear/header.h"
// #include "../lib/big/header.h"
#include "../lib/tree/header.h"



[[maybe_unused]]
static void pi(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    flt_num_t flt_pi = pi_tree(size, n_process, mem_launch, mem_max);
    printf("\n\n");
    tprintf("              %-20s|", "display begin");
    TIME_SETUP
    flt_num_display_dec(flt_pi);
    TIME_END(t1)
    tprintf("              %-20s| %7.1f", "display end", dtime(t1));
    flt_num_free(flt_pi);
}

// int main(int argc, char** argv)
int main(void)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("\nbegin");

    // araucaria_disk_config_t config = {
    //     .disk_path = "/mnt/wsl/workspace/tmp",
    //     .disk_threshold_bytes = 2'048'000'000 // ~2GiB per number; see derivation in conversation
    // };
    // araucaria_disk_config_set(&config);

    // Tasks are launched while estimated worker memory is below mem_launch;
    // a launched task may overshoot up to mem_max, which is never crossed.
    uint64_t mem_launch = U64(15) * 1024 * 1024 * 1024;
    uint64_t mem_max = U64(20) * 1024 * 1024 * 1024;
    pi(64'000'000, 16, mem_launch, mem_max);

    printf("\n");
    return 0;
}
