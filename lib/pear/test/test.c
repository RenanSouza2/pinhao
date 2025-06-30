
#include "../debug.h"
#include "../../../testrc.h"
#include "../../../mods/macros/test.h"

#include "../../../mods/macros/U64.h"



void test_pear_hello(bool show)
{
    TEST_FN_OPEN

    TEST_CASE_OPEN(1)
    {
    }
    TEST_CASE_CLOSE

    TEST_FN_CLOSE
}



void test_pear()
{
    TEST_LIB

    bool show = false;

    test_pear_hello(show);

    TEST_ASSERT_MEM_EMPTY
}



int main()
{
    setbuf(stdout, NULL);
    test_pear();
    printf("\n\n\tTest successful\n\n");
    return 0;
}
