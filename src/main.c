#include <stdio.h>

#include "../mods/clu/header.h"

#include "../lib/pi/header.h"

// int main(int argc, char** argv)
int main()
{
    setbuf(stdout, NULL);

    pi_threads(10000, 0);

    printf("\n");
    return 0;
}