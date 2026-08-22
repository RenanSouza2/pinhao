#include <stdio.h>
#include <unistd.h>

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
static void pi(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max, uint64_t mem_solo)
{
    long n_proc_avail = sysconf(_SC_NPROCESSORS_ONLN);
    if(n_proc_avail > 0 && n_process > (uint64_t)n_proc_avail)
    {
        n_process = (uint64_t)n_proc_avail;
    }

    flt_num_t flt_pi = pi_tree(size, n_process, mem_launch, mem_max, mem_solo);
    printf("\n\n");
    tprintf("[%17.6f] %-20s|", get_wall_time(), "display begin");
    TIME_SETUP
    flt_num_display_dec_threads(flt_pi, n_process);
    TIME_END(t1)
    tprintf("[%17.6f] %-20s| %7.1f", get_wall_time(), "display end", dtime(t1));
    flt_num_free(flt_pi);
}

// int main(int argc, char** argv)
int main(void)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("\nbegin");

    // araucaria_disk_config_t config = {
    //     .disk_path = "/mnt/wsl/workspace/tmp",
    //     .disk_threshold_bytes = 2'048'000'000 // bytes per number
    // };
    // araucaria_disk_config_set(&config);

    // Tasks are launched while estimated worker memory is below mem_launch.
    // The first slot may overshoot up to mem_max while other tasks run; past
    // mem_solo it launches only with no other task running.
    uint64_t mem_launch = U64(15) * 1024 * 1024 * 1024;
    uint64_t mem_max = U64(20) * 1024 * 1024 * 1024;
    uint64_t mem_solo = U64(25) * 1024 * 1024 * 1024;
    pi(128'000'000, 16, mem_launch, mem_max, mem_solo);

    printf("\n");
    return 0;
}
