/*
 * MIOTEXT.C - a record's notes as one text, its lines broken by CR LF
 * (MEMOIO.H).
 *
 * For a client that edits the memo as one text, as a Windows edit
 * control holds it; INV's editor works on the lines and does not link
 * this.
 */
#include <string.h>
#include "COMPAT.H"
#include "MEMOIO.H"

void mio_text(const mio *m, char *out, u16 outsz)
{
    char lb[MEMO_SLOT];
    u16 room;
    u16 at;
    u16 i;
    u16 k;

    room = (u16)(outsz - 1);
    at = 0;
    for (i = 0; i < m->n && i < MEMO_MAXLINES; i++) {
        if (i > 0) {
            if (room - at < 2)
                break;
            out[at++] = '\r';
            out[at++] = '\n';
        }
        mio_get(m, i, lb);
        k = m->len[i];
        if (k > room - at)
            k = (u16)(room - at);
        memcpy(out + at, lb, k);
        at = (u16)(at + k);
    }
    out[at] = '\0';
}

void mio_set_text(mio *m, const char *t)
{
    const char *s;

    mio_empty(m);
    if (*t == '\0')
        return;
    for (;;) {
        s = t + strcspn(t, "\r\n");
        mio_add_line(m, t, (u16)(s - t));
        if (*s == '\0')
            return;
        t = s + (s[0] == '\r' && s[1] == '\n' ? 2 : 1);
    }
}
