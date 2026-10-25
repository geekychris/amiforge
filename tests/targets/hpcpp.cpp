// hpcpp — C++ debuggee for the Edit and Continue test: a virtual call into shape.cpp,
// which the test patches (tests/targets/hotpatch/shape_v2.cpp).
#include <cstdio>
#include "shape.h"

int main()
{
    Square sq(3);
    Shape *s = &sq;
    for (int i = 0; i < 4; i++) {
        std::printf("area=%ld\n", (long)s->area());
        sq.grow();
    }
    return 0;
}
