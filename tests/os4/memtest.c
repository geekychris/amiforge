/* memtest — checks newlib's memcpy/memmove/memset on this machine (the QEMU sam460ex guest
 * is known to lose data in large memcpys). Prints one line per failing case. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>

static unsigned char *src, *dst;
static int fails;

static void check(const char *what, size_t size, size_t so, size_t doff)
{
    size_t i, bad = 0, first = (size_t)-1;
    for (i = 0; i < size; i++)
        if (dst[doff + i] != src[so + i]) { if (first == (size_t)-1) first = i; bad++; }
    if (bad) {
        printf("FAIL %s size=%lu src+%lu dst+%lu: %lu bytes wrong, first at %lu\n", what,
               (unsigned long)size, (unsigned long)so, (unsigned long)doff, (unsigned long)bad, (unsigned long)first);
        fails++;
    }
}

int main(void)
{
    static const size_t sizes[] = { 4096, 65536, 200000, 262144, 300000, 400000, 600000, 1000000, 2000000 };
    size_t max = 2000000 + 64, s, a, b;
    src = malloc(max);
    dst = malloc(max);
    if (!src || !dst) { printf("no memory\n"); return 20; }
    for (s = 0; s < max; s++) src[s] = (unsigned char)(s * 7 + (s >> 9));
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (a = 0; a < 8; a += 3)
            for (b = 0; b < 8; b += 5) {
                memset(dst, 0xEE, max);
                memcpy(dst + b, src + a, sizes[s]);
                check("memcpy", sizes[s], a, b);
                memset(dst, 0xEE, max);
                memmove(dst + b, src + a, sizes[s]);
                check("memmove", sizes[s], a, b);
                memset(dst, 0xEE, max);
                IExec->CopyMem(src + a, dst + b, sizes[s]);
                check("CopyMem", sizes[s], a, b);
                if (a == 0 && b == 0) {
                    memset(dst, 0xEE, max);
                    IExec->CopyMemQuick(src, dst, sizes[s] & ~3);
                    check("CopyMemQuick", sizes[s] & ~3, 0, 0);
                }
            }
    printf("memtest: %d failure%s\n", fails, fails == 1 ? "" : "s");
    return fails ? 5 : 0;
}
