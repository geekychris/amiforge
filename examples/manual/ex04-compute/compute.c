/* compute - the manual's exercise 4 (Edit and Continue): change compute() while the program is
 * stopped, press Ctrl-F9, and carry on. This is the program after the change; it starts as
 * "return x * 2;". */
#include <stdio.h>
#include <proto/dos.h>

int counter = 0;

int compute(int x)
{
    counter++;
    return x * 2 + 1000;
}

int main(void)
{
    for (int i = 1; i <= 10; i++) {
        int r = compute(i);
        printf("call %d: compute(%d) = %d, counter = %d\n", i, i, r, counter);
        IDOS->Delay(25);      /* half a second, so there's time to stop it */
    }
    return 0;
}
