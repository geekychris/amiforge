/* version 3: patched over version 2 */
int counter = 7;
static int calls;

int compute(int x)
{
    calls++;
    counter++;
    return x * 5 + 2000 + calls * 0;
}
