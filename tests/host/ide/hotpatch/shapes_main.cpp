#include <cstdio>
#include "shape.h"
int main()
{
    Square s(3);
    Shape *p = &s;
    std::printf("%ld\n", (long)p->area());
    return 0;
}
