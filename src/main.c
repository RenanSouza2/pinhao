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
static void pi(uint64_t size, uint64_t n_process, uint64_t mem_launch, uint64_t mem_max)
{
    assert(n_process);

    long n_proc_avail = sysconf(_SC_NPROCESSORS_ONLN);
    if(n_proc_avail > 0 && n_process > (uint64_t)n_proc_avail)
    {
        n_process = (uint64_t)n_proc_avail;
    }

    // ram_budget_bytes is per worker: divides the band by the clamped n_process
    araucaria_disk_config_t config = {
        .disk_path = "cache/tmp",
        .disk_threshold_bytes = mem_max / 2,
        .ram_budget_bytes = mem_max / n_process
    };
    araucaria_disk_config_set(&config);

    // bare digits "31415...", digit k at byte offset k
    char path[64];
    snprintf(path, sizeof(path), "cache/res/pi_" U64P(015) ".txt", size);
    FILE *fp = fopen(path, "w");
    assert(fp);

    flt_num_t flt_pi = pi_tree(size, n_process, mem_launch, mem_max);
    tprintf("[%17.6f] %-20s|", get_wall_time(), "display begin");
    TIME_SETUP
    fxd_num_t fxd_pi = fxd_num_wrap_flt(flt_pi, flt_pi.size - 1);
    fxd_num_write_dec_threads(fp, fxd_pi, n_process);
    int res = fclose(fp);
    assert(res == 0);
    TIME_END(t1)
    tprintf("[%17.6f] %-20s| %7.1f", get_wall_time(), "display end", dtime(t1));
    fxd_num_free(fxd_pi);
}

// int main(int argc, char** argv)
int main(void)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("\nbegin");

    uint64_t mem_launch = U64(20) * 1024 * 1024 * 1024;
    uint64_t mem_max = U64(25) * 1024 * 1024 * 1024;

    pi(4'000'000'000, 16, mem_launch, mem_max);

    printf("\n");
    return 0;
}
