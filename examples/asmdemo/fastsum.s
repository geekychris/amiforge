# fastsum.s — PowerPC assembler called from C.
#
#   int fastsum(const int *values, int count)    r3 = values, r4 = count
#   returns the sum in r3. Step through it with F8 (step instruction) and
#   watch r3..r6 change in the Registers panel.

        .text
        .globl  fastsum
        .type   fastsum, @function
fastsum:
        li      5, 0            # r5 = sum
        cmpwi   4, 0
        ble     2f              # nothing to add
        mtctr   4               # loop count in CTR
1:      lwz     6, 0(3)         # r6 = *values
        addi    3, 3, 4         # values++
        add     5, 5, 6         # sum += r6
        bdnz    1b
2:      mr      3, 5            # return sum
        blr
        .size   fastsum, .-fastsum
