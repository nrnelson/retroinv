/*
 * CZERO.C - cf_zero, zeros written over a file.
 *
 * It is a module of its own so that only the programs that write zeros
 * link it.  It calls nothing but cf_write, so it is portable C89.
 */
#define COMPAT_DISK
#include "COMPAT.H"

#define CZ_BLK          64      /* bytes of cz_zeros                    */

/* The zeros, never written: left uninitialised, they sit in the BSS
   and take no room in the program file. */
static u8 cz_zeros[CZ_BLK];

int cf_zero(int h, u32 n)
{
    u16 w;
    int rc;

    rc = 0;
    for (; rc == 0 && n > 0; n -= w) {
        w = n < CZ_BLK ? (u16)n : (u16)CZ_BLK;
        rc = cf_write(h, cz_zeros, w);
    }
    return rc;
}
