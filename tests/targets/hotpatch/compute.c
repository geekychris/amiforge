/* version 1 (built into hptarget) */
int counter = 7;
static int calls;

int compute(int x)
{
    calls++;
    counter++;
    return x * 2;
}
