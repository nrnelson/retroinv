/*
 * MEMOIO.C - the notes of a record as a client holds them, loaded and
 * saved over the link (MEMOIO.H).
 *
 * A slot holds its line's bytes alone, len giving their number, so each
 * line is read and written in one call of its own length.  Loading
 * builds each line in the mio's own buffer and stores it in its slot
 * when it ends; a load with no block (mio_count) only counts.  The
 * memo as one text is MIOTEXT.C's, apart so that only the clients that
 * edit it so link it.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MEMOIO.H"

/* The command word of the memo requests, also the field SET writes. */
static const char w_notes[] = "NOTES";

static u16 slot(u16 i)
{
    return (u16)(i * MEMO_COLS);
}

/* ---------------------------------------------------------------------
 * Loading.
 * --------------------------------------------------------------------- */
void mio_empty(mio *m)
{
    m->n = 0;
    m->bytes = 0;
    m->changed = 0;
    m->trunc = 0;
    m->ll = 0;
}

static void ld_flush(mio *m)
{
    if (m->n < MEMO_MAXLINES) {
        if (m->b != NULL)
            cmem_put(m->b, slot(m->n), m->line, m->ll);
        m->len[m->n] = m->ll;
    }
    m->bytes = (u16)(m->bytes + m->ll + (m->n > 0 ? 2 : 0));
    m->n++;
    m->ll = 0;
}

static void ld_put(mio *m, char c)
{
    if (m->ll == MEMO_COLS) {
        m->changed = 1;
        ld_flush(m);
    }
    m->line[m->ll++] = c;
}

void mio_add_line(mio *m, const char *s, u16 n)
{
    u8 c;

    for (; n > 0; n--) {
        c = (u8)*s++;
        if (c == '\t') {
            m->changed = 1;
            do
                ld_put(m, ' ');
            while (m->ll % MEMO_TAB != 0 && m->ll < MEMO_COLS);
        } else if (c < 0x20 || c == 0x7F) {
            m->changed = 1;
            ld_put(m, '?');
        } else {
            ld_put(m, (char)c);
        }
    }
    ld_flush(m);
}

static int ld_line(void *arg, const rq_line *ln)
{
    mio *m;

    m = (mio *)arg;
    if (ln->kind == 'K') {
        if (ln->n == 2 && strcmp(ln->v[0], "TRUNCATED") == 0 &&
            ln->v[1][0] == '1' && ln->v[1][1] == '\0')
            m->trunc = 1;
        return 0;
    }
    if (ln->kind == 'L' && ln->n > 0)
        mio_add_line(m, ln->v[0], (u16)strlen(ln->v[0]));
    return 0;
}

int mio_open(mio *m)
{
    mio_empty(m);
    m->b = cmem_alloc((u16)(MEMO_MAXLINES * MEMO_COLS));
    return m->b != NULL ? RQ_OK : RQ_EINTERNAL;
}

void mio_close(mio *m)
{
    cmem_free(m->b);
}

int mio_load(mio *m, link *l, const char *table, const char *id,
             rq_res *res)
{
    rq q;

    mio_empty(m);
    rq_begin(&q, table);
    rq_word(&q, w_notes);
    rq_word(&q, id);
    return rq_send(l, &q, ld_line, m, res);
}

int mio_count(link *l, const char *table, const char *id, u16 *lines,
              rq_res *res)
{
    mio m;
    int rc;

    m.b = NULL;
    rc = mio_load(&m, l, table, id, res);
    *lines = rc == RQ_OK ? m.n : 0;
    return rc;
}

u16 mio_check(mio *m)
{
    if (m->n > MEMO_MAXLINES) {
        m->n = MEMO_MAXLINES;
        return MM_TOOMANY;
    }
    if (m->bytes > LINK_MAXMEMO)
        return MM_TOOLONG;
    if (m->trunc)
        return MM_TRUNC;
    return m->changed ? MM_CHANGED : 0;
}

/* ---------------------------------------------------------------------
 * The lines.
 * --------------------------------------------------------------------- */
void mio_get(const mio *m, u16 i, char *lb)
{
    cmem_get(m->b, slot(i), lb, m->len[i]);
    lb[m->len[i]] = '\0';
}

void mio_put(mio *m, u16 i, const char *lb, u16 len)
{
    cmem_put(m->b, slot(i), lb, len);
    m->bytes = (u16)(m->bytes - m->len[i] + len);
    m->len[i] = (u8)len;
}

void mio_insert(mio *m, u16 i)
{
    if (i < m->n)
        cmem_move(m->b, slot((u16)(i + 1)), slot(i),
                  (u16)((m->n - i) * MEMO_COLS));
    memmove(m->len + i + 1, m->len + i, (size_t)(m->n - i));
    if (m->n > 0)
        m->bytes = (u16)(m->bytes + 2);
    m->n++;
    m->len[i] = 0;
}

void mio_delete(mio *m, u16 i)
{
    m->bytes = (u16)(m->bytes - m->len[i] - (m->n > 1 ? 2 : 0));
    if (i + 1 < m->n)
        cmem_move(m->b, slot(i), slot((u16)(i + 1)),
                  (u16)((m->n - i - 1) * MEMO_COLS));
    m->n--;
    memmove(m->len + i, m->len + i + 1, (size_t)(m->n - i));
}

/* ---------------------------------------------------------------------
 * Saving.
 * --------------------------------------------------------------------- */

/* One line on its own fits a request.  The longest request for one is
   "MAINT NOTES <id> PART=" with an ID of LINK_IDLEN digits and the line
   quoted, its MEMO_COLS bytes each escaped as two: "SET <id> NOTES=" is
   as long as "NOTES <id> NEW=", END= as PART=, and ITEM or LOC with an
   ID is no longer.  Each sizeof counts a NUL, hence the + 2.
   The two declarations below disagree, which every compiler refuses,
   when the line would not fit. */
extern char memo_line_fits[1];
extern char memo_line_fits[(sizeof "MAINT NOTES " + LINK_IDLEN +
                            sizeof " PART=\"\"" + 2 * MEMO_COLS <=
                            LINK_MAXLINE + 2) ? 1 : 2];

/* Line k of m into lb, without its NUL; its length less its trailing
   spaces. */
static u16 get_trim(const mio *m, u16 k, char *lb)
{
    u16 ll;

    ll = m->len[k];
    cmem_get(m->b, slot(k), lb, ll);
    while (ll > 0 && lb[ll - 1] == ' ')
        ll--;
    return ll;
}

/* Send lines from..n-1 of the memo in one request, with as many lines
   as fit, and always the first (memo_line_fits); *sent receives how
   many went.  From line 0 it is "<table> NOTES <id> NEW=...", or, when
   every line fits, "<table> SET <id> NOTES=...", which is as long; from
   a later line "<table> NOTES <id> PART=...", or END=, one byte
   shorter, for the last lines.  From the second line on the value holds
   a '\n' and goes quoted, so its cost on the line is kept as the lines
   are added: the bytes each takes inside quotes. */
static int send_part(link *l, const char *table, const char *id,
                     const mio *m, u16 from, u16 n, u16 *sent,
                     rq_res *res)
{
    rq q;
    char txt[LINK_MAXLINE + MEMO_SLOT]; /* each byte costs 1 or more on
                                   the line, so a value that fits the
                                   request fits in LINK_MAXLINE, and
                                   the line read after it in the rest   */
    u16 room;                   /* bytes the value may take in quotes   */
    u16 tl;
    u16 cost;                   /* bytes of txt inside quotes           */
    u16 c;
    u16 at;
    u16 k;
    u16 ll;
    const char *word;
    const char *key;

    rq_begin(&q, table);
    /* Less " NOTES <id>" (an ID or a location token, never quoted), the
       key with its ' ' and '=', and the quotes. */
    room = (u16)(rq_room(&q) - strlen(id) -
                 (sizeof " NOTES  NEW=\"\"" - 1) - (from > 0));
    tl = 0;
    cost = 0;
    for (k = from; k < n; k++) {
        at = tl;
        if (k > from)
            at++;               /* room for the '\n' before it         */
        ll = get_trim(m, k, txt + at);
        c = rq_cost(txt + at, ll);
        if (c != ll)
            c = (u16)(c - 2);   /* the line was costed with its quotes */
        if (k > from) {
            c = (u16)(c + 2);   /* the '\n' before it, escaped         */
            if (cost + c > room)
                break;
            txt[tl] = '\n';
        }
        cost = (u16)(cost + c);
        tl = (u16)(at + ll);
    }
    *sent = (u16)(k - from);
    word = w_notes;
    key = k < n ? "PART" : "END";
    if (from == 0) {
        key = "NEW";
        if (k == n) {
            word = "SET";
            key = w_notes;
        }
    }
    rq_word(&q, word);
    rq_word(&q, id);
    rq_pairn(&q, key, txt, tl);
    return rq_send(l, &q, NULL, NULL, res);
}

/* The lines worth sending: trailing spaces and empty lines dropped. */
static u16 send_lines(const mio *m)
{
    char lb[MEMO_SLOT];
    u16 n;

    n = m->n < MEMO_MAXLINES ? m->n : MEMO_MAXLINES;
    while (n > 0 && get_trim(m, (u16)(n - 1), lb) == 0)
        n--;
    return n;
}

int mio_save(mio *m, link *l, const char *table, const char *id,
             u16 *done, u16 *total, rq_res *res)
{
    u16 sent;
    int rc;

    *total = send_lines(m);
    *done = 0;
    do {
        rc = send_part(l, table, id, m, *done, *total, &sent, res);
        if (rc == RQ_ELOST && *done + sent == *total)
            *done = *total;     /* the request that writes went out     */
        if (rc != RQ_OK)
            return rc;
        *done = (u16)(*done + sent);
    } while (*done < *total);
    return RQ_OK;
}

u16 mio_failmsg(int rc, u16 done, u16 total, const rq_res *res)
{
    if (rc == RQ_EARG && txt_ieq(res->field, "NEW"))
        return MM_OLDSRV;
    if (rc == RQ_ELOST && done == total)
        return MM_MAYBE;
    return done > 0 ? MM_PARTIAL : 0;
}
