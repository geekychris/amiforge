#include <stdio.h>
extern int compute(int x);
extern int counter;
int main(void)
{
    int i, sum = 0;
    for (i = 0; i < 5; i++) sum += compute(i);
    printf("sum=%ld counter=%ld\n", (long)sum, (long)counter);
    return 0;
}
