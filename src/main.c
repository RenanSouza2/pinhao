#include <stdio.h>

#include "../mods/clu/header.h" // IWYU pragma: keep
// #include "../mods/macros/assert.h"
// #include "../mods/macros/fork.h"
// #include "../mods/macros/time.h"
// #include "../mods/number/lib/num/struct.h"

#include "../lib/big/header.h"
// #include "../lib/linear/linear/header.h"



[[maybe_unused]]
static void pi(uint64_t size)
{
    flt_num_t flt_pi = pi_big(size);
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

    // clu_log_level_set(CLU_LOG_DYNAMIC);

    // pi(10'000);
    pi(1'000'000);
    // pi(200'000'000);

    // prepare(20, 0, 30, 6);
    // prepare(21, 2, 15, 6);
    // prepare(22, 0, 76, 6);
    // prepare(23, 663, 762, 12);
    // prepare(24, 8, 380, 1);
    // prepare(25, 16, 190, 4);
    // prepare(26, 10, 95, 4);
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

// 14m33.453s