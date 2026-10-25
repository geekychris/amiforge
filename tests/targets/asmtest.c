/* Debuggee: C calling a hand-written PPC assembler function (asmtest_add3.S). */
#include <stdio.h>

int add3(int a, int b, int c);   /* in asmtest_add3.S */

int main(void)
{
    int r = add3(1, 2, 3);       /* @asm_call */
    printf("add3=%d\n", r);
    return r;                    /* @asm_ret */
}
