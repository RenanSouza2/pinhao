#include <stdio.h>

#include "../mods/clu/header.h" // IWYU pragma: keep
#include "../mods/macros/assert.h" // IWYU pragma: keep
// #include "../mods/macros/fork.h"
#include "../mods/macros/time.h"
// #include "../mods/araucaria/lib/num/struct.h"

// #define CACHE "/mnt/wsl/external_workspace/cache"
// #include "../lib/linear/linear/header.h"
// #include "../lib/big/header.h"
#include "../lib/tree/header.h"



[[maybe_unused]]
static void pi(uint64_t size, uint64_t n_process)
{
    flt_num_t flt_pi = pi_tree(size, n_process);
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

    // char disk_path[] = "./cache/tmp";
    // char disk_path[] = "/mnt/wsl/external_workspace/cache/tmp";
    // num_config_t config = {
    //     .disk_path = disk_path,
    //     .disk_threshold = (uint64_t)1024 * 1024 * 1024
    // };
    // num_config_set(&config);

#if defined(__APPLE__)
    uint64_t n_threads = 8;
#else
    uint64_t n_threads = 16;
#endif

    pi(128'000'000, n_threads);

    printf("\n");
    return 0;
}
