/*
 * CNBNAME.C - the program's local NetBIOS name (nb_local_name,
 * COMPAT.H): portable C over nb_add_name, apart from CNETB.C, so that
 * the tests' fake provider serves it as it serves the programs.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"

#define NN_DIGITS       4       /* hex digits of a generated name       */

u8 nb_local_name(char *name, const char *local, const char *prefix,
                 u8 *added)
{
    u16 i;
    u16 n;
    u16 v;
    u16 k;
    u8 num;
    u8 rc;
    int gen;

    gen = local == NULL && dos_machine_name(name) != 0;
    if (gen) {
        txt_copy(name, prefix, NB_NAMELEN - NN_DIGITS);
        n = (u16)strlen(name);
    } else {
        txt_ucopy(name, local != NULL ? local : name, NB_NAMELEN);
    }
    i = 0;
    do {
        if (gen) {
            v = (u16)(tmr_ticks() + i);
            for (k = 0; k < NN_DIGITS; k++, v = (u16)(v << 4))
                name[n + k] = txt_hexdig[v >> 12];
            name[n + k] = '\0';
        }
        rc = nb_add_name(name, &num);
    } while (gen && (rc == NB_RC_NAMEINUSE || rc == NB_RC_DUPNAME) &&
             ++i < NB_NAMETRIES);
    *added = rc == NB_OK;
    return !gen && rc == NB_RC_DUPNAME ? (u8)NB_OK : rc;
}
