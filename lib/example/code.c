#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "../../mods/clu/header.h"
#include "../../mods/macros/assert.h"
#include "../../mods/macros/U64.h"



#ifdef DEBUG

void example_debug()
{
    printf("\n\nThis function helps to debug");
    printf("\nbut it is not compiled in the final build");
    printf("\n");
}

#endif



void example_hello()
{
    printf("\nHello example library");
}

handler_p example_malloc()
{
    printf("\n\nHello malloc");

    handler_p h = malloc(8);
    assert(h);
    *(uint64_t*)h = 0x1234;
    return h;
}

void example_revert(bool revert)
{
    assert(!revert);
}
