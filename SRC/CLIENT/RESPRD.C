/*
 * RESPRD.C - whole protocol responses read from a client's byte stream
 * (RESPRD.H).
 *
 * Complete lines are delivered from the line buffer in place; the part
 * of a line still open is moved to the front before the next fill,
 * which takes as much as the buffer has room for.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RESPRD.H"

static const char k_data[] = "+DATA";

int rr_response(rr_buf *r, rr_fill_fn fill, void *farg, rr_line_fn fn,
                void *arg)
{
    char *s;
    char *lf;
    u16 avail;
    u16 used;
    u16 len;
    int first;
    int data;
    int done;
    int fail;
    char c;

    first = 1;
    data = 0;
    fail = 0;
    for (;;) {
        s = r->line + r->lpos;
        avail = (u16)(r->llen - r->lpos);
        lf = (char *)memchr(s, '\n', (size_t)avail);
        if (lf != NULL) {
            used = (u16)(lf - s + 1);
            len = (u16)(used - 1);
            if (len > 0 && s[len - 1] == '\r')
                len--;
        } else if (avail > LINK_MAXRESP) {
            used = LINK_MAXRESP;
            len = LINK_MAXRESP;
        } else {
            memmove(r->line, s, (size_t)avail);
            r->lpos = 0;
            used = fill(farg, r->line + avail,
                         (u16)(sizeof r->line - avail));
            r->llen = (u16)(avail + used);
            if (used == 0)
                return RR_ELOST;
            continue;
        }
        if (first)
            data = len == sizeof k_data - 1 &&
                   memcmp(s, k_data, sizeof k_data - 1) == 0;
        first = 0;
        if (!fail && fn != NULL &&
            (len > 0 || lf == NULL || !r->cont)) {
            c = s[len];
            s[len] = '\0';
            fail = fn(arg, s, len) != 0;
            s[len] = c;
        }
        done = lf != NULL &&
               (!data || (!r->cont && len == 1 && s[0] == '.'));
        r->cont = lf == NULL;
        r->lpos = (u16)(r->lpos + used);
        if (done)
            return fail ? RR_EOUTPUT : RR_OK;
    }
}

int rr_request(const char *line, u16 len)
{
    u16 i;

    if (len > LINK_MAXLINE)
        return 1;
    for (i = 0; i < len; i++)
        if (line[i] != ' ' && line[i] != '\t')
            return 1;
    return 0;
}
