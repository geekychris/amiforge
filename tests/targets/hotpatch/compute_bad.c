/* a new static variable: needs a restart */
int counter = 7;
static int calls;
static int extra;

int compute(int x)
{
    calls++;
    counter++;
    extra++;
    return x + extra;
}
