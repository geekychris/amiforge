#include "aftest.h"
int main()
{
    int n = 0;
    for (auto &c : aftest::cases()) {
        int before = aftest::failures();
        c.fn();
        n++;
        if (aftest::failures() != before) std::printf("%s: FAILED\n", c.name);
    }
    std::printf("%d tests, %d failures\n", n, aftest::failures());
    return aftest::failures() ? 1 : 0;
}
