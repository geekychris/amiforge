/* dice.c - a Boing BASIC extension in C (the AmiForge manual's exercise 5): a class Dice and a
 * function Percent. Its descriptor is dice.bbx; Dice.bbp links it with Extension=dice. */
#include "boing_ext.h"

/* an instance: the object header first, then the class's own data */
typedef struct dice { bb_obj hdr; long sides, rolls; uint32_t seed; } dice;   /* 32 bits everywhere: the same rolls on the Amiga and elsewhere */

enum { M_ROLL, P_SIDES, P_ROLLS, P_SEED };
static const bb_member dice_members[] = {
    {"Roll", M_ROLL, BB_INV_METHOD | BB_INV_GET},
    {"Sides", P_SIDES, BB_INV_GET | BB_INV_LET},
    {"Rolls", P_ROLLS, BB_INV_GET},
    {"Seed", P_SEED, BB_INV_GET | BB_INV_LET},
};

static void dice_invoke(bb_obj *self, int32_t id, int kind, int argc, bb_value *argv, bb_value *ret)
{
    dice *d = (dice *)self;
    switch (id) {
    case M_ROLL:
        /* a small linear congruential generator: the same seed gives the same rolls */
        d->seed = d->seed * 1103515245u + 12345u;
        d->rolls++;
        *ret = bb_ret_long((long)((d->seed >> 16) % (uint32_t)d->sides) + 1);
        return;
    case P_SIDES:
        if (kind & BB_INV_LET) {
            long n = bb_arg_long(argv, argc - 1);       /* the new value comes last */
            if (n < 2) bb_raise(5, NULL);               /* invalid procedure call */
            d->sides = n;
        } else *ret = bb_ret_long(d->sides);
        return;
    case P_ROLLS: *ret = bb_ret_long(d->rolls); return;
    case P_SEED:
        if (kind & BB_INV_LET) d->seed = (uint32_t)bb_arg_long(argv, argc - 1);
        else *ret = bb_ret_long((long)d->seed);
        return;
    }
    bb_raise(438, NULL);
}

BB_CLASS(dice_class, "Dice", dice, dice_members, dice_invoke, NULL);

static bb_obj *new_dice(void)
{
    dice *d = (dice *)bb_obj_new(&dice_class);
    d->sides = 6;
    d->seed = 1;
    return &d->hdr;
}

/* Percent(part, whole): part as a whole percentage of whole */
static bb_value percent(int argc, bb_value *argv)
{
    bb_long part = bb_arg_long(argv, 0), whole = bb_arg_long(argv, 1);
    (void)argc;
    return bb_ret_long(whole ? (part * 100 + whole / 2) / whole : 0);
}

BB_EXTENSION(dice)
{
    bb_register_class("Dice", &dice_class, new_dice);
    bb_register_function("Percent", percent);
}
