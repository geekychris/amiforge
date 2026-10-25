/* hptarget — debuggee for the Edit and Continue test (tests/os4/hotpatchtest.c). Linked with
 * tests/targets/hotpatch/compute.c; the test patches in compute_v2.c and compute_v3.c. */
#include <stdio.h>

extern int compute(int x);
extern int counter;

int main(void)
{
    int i;
    for (i = 0; i < 10; i++) {
        int r = compute(i);
        printf("i=%ld r=%ld\n", (long)i, (long)r);
    }
    printf("counter=%ld\n", (long)counter);
    return 0;
}
