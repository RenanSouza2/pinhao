#include <stdio.h>
#include <time.h>

#include "../mods/clu/header.h"
#include "../mods/clu/header.h"
#include "../mods/macros/assert.h"
#include "../mods/number/lib/sig/header.h"
#include "../mods/number/lib/num/header.h"
#include "../mods/number/lib/num/struct.h"

#include "../lib/pi/header.h"
#include "../lib/jumpstart/header.h"



uint64_t get_time()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return time.tv_sec * (uint64_t)1e9 + time.tv_nsec;
}

#define TIME_SETUP uint64_t _time_begin, _time_end;

#define TIME_START _time_begin = get_time();

#define TIME_END(TIME_VAR)                          \
    _time_end = get_time();                         \
    uint64_t TIME_VAR = _time_end - _time_begin;    \

// int main(int argc, char** argv)
int main()
{
    setbuf(stdout, NULL);

    uint64_t size = 2;
    fix_num_t fix_pi = pi_threads(size);
    printf("\n\n");fix_num_display_dec(fix_pi);
    fix_num_free(fix_pi);

    printf("\n");
    return 0;
}
