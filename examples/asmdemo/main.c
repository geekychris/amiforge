/* asmdemo — C calling a PowerPC assembler function (fastsum.s). */
#include <stdio.h>

int fastsum(const int *values, int count);

int main(void)
{
    int values[] = { 3, 1, 4, 1, 5, 9, 2, 6 };
    int n = sizeof values / sizeof values[0];
    int sum = fastsum(values, n);

    printf("fastsum of %d values = %d\n", n, sum);
    return sum == 31 ? 0 : 1;
}
