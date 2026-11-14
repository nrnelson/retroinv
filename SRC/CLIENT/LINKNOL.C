/*
 * LINKNOL.C - the link without its LOCAL backend: what a program built
 * without the engine, the dispatcher and the DBF layer links in place of
 * LINKLOC.C.
 *
 * Such a program reaches the server over its remote backends only.  It
 * has no LOCAL backend: link_open_local opens nothing, so no link holds
 * an engine, and eng_close, which LINK.C calls for every link, has none
 * to close.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "ENGINE.H"
#include "LINK.H"

int link_open_local(link **out, const char *dir, const link_clock *now,
                    char *why, u16 whysz)
{
    *out = NULL;
    /* dir is never NULL (LINK.H): the test only keeps dir and now, which
       nothing here reads, referenced. */
    if (why != NULL && (dir != NULL || now != NULL))
        txt_copy(why, "no engine in this program: it needs a server",
                 whysz);
    return ENG_ENOTAVAIL;
}

int eng_close(eng *e)
{
    return e == NULL ? ENG_OK : ENG_EINTERNAL;
}
