/* ${NAME}: the library's functions (the "main" interface in ${LOWER}.xml).
 * ${DATE}, ${AUTHOR}
 *
 * Each function gets the interface it was called through as Self; the library base is
 * Self->Data.LibBase. Add a function: describe it in ${LOWER}.xml, run idltool (see the
 * comment there), write it here, and add it to main_v1_vectors in ${LOWER}_vectors.c's order.
 */
#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/${LOWER}.h>

#include "${LOWER}_vectors.h"

uint32 _${IDENT}_Obtain(struct ${IDENT}IFace *Self)
{
    return ++Self->Data.RefCount;
}

uint32 _${IDENT}_Release(struct ${IDENT}IFace *Self)
{
    return --Self->Data.RefCount;
}

int32 _${IDENT}_Add(struct ${IDENT}IFace *Self, int32 a, int32 b)
{
    (void)Self;
    return a + b;
}

int32 _${IDENT}_Multiply(struct ${IDENT}IFace *Self, int32 a, int32 b)
{
    (void)Self;
    return a * b;
}

CONST_STRPTR _${IDENT}_Hello(struct ${IDENT}IFace *Self)
{
    (void)Self;
    return "Hello from ${LOWER}.library!";
}
