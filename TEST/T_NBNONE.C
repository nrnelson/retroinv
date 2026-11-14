/*
 * T_NBNONE.C - unit test for the client's NetBIOS session (NBCLI.C)
 * without a NetBIOS provider.
 *
 * usage: T_NBNONE
 *
 * It links the real provider calls of CNETB.C, so it runs where no
 * provider is loaded: the host build, and the DOS machines of the test
 * runs.  Checks:
 * - nbcli_open gives NBCLI_ENONB, *out NULL and the code NB_OK, for a
 *   name given and for the default one, and on the host holds no
 *   memory afterwards;
 * - nbcli_close(NULL) is accepted.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include "COMPAT.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "NBCLI.H"

static void test_absent(const char *local)
{
    nbcli *n;
    u8 nbrc;
#ifdef COMPAT_HOST
    u16 held;

    held = tm_held();
#endif
    nbrc = NB_RC_PENDING;
    TF_EQ(nbcli_open(&n, "SRV", local, &nbrc), NBCLI_ENONB);
    TF_CHECK(n == NULL);
    TF_EQ(nbrc, NB_OK);
#ifdef COMPAT_HOST
    TM_HELD(held);
#endif
}

int main(void)
{
    tf_begin("T_NBNONE");
    cmem_init();
    test_absent("CLI");
    test_absent(NULL);
    nbcli_close(NULL);
    return tf_done();
}
