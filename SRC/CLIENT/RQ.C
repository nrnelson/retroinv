/*
 * RQ.C - building protocol requests and reading their responses (RQ.H).
 *
 * The quoting is the one the protocol's response writer uses, and the
 * splitting of F, R and K lines the one its tokenizer applies to
 * requests, so a value survives the trip in either direction unchanged.
 *
 * rq_send decodes each response line as the link delivers it, never
 * longer than LINK_MAXRESP bytes, into a buffer of LINK_MAXRESP + 1
 * bytes on its own stack: a decoded value is never longer than its
 * source, and each value gains only its NUL, for which the separator or
 * the quotes before it leave room.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"

/* The shortest field name of the schema (MFR, QTY and the like). */
#define RQ_MINNAME      3

/* RQ_MAXVALS holds every field of a full FIELDS= list: each name has at
   least RQ_MINNAME characters and a comma after all but the last.  The
   array is declared twice, the second time with size 2 when that fails:
   the declarations then disagree, which every compiler refuses. */
extern char rq_maxvals_ok[1];
extern char rq_maxvals_ok[((LINK_MAXFSPEC + 1) / (RQ_MINNAME + 1) <=
                           RQ_MAXVALS) ? 1 : 2];

u8 rq_busy;

/* ---------------------------------------------------------------------
 * Building.
 * --------------------------------------------------------------------- */

/* 1 when the n-byte value v must be sent in quotes. */
static int must_quote(const char *v, u16 n)
{
    u16 i;
    u8 c;

    if (n == 0)
        return 1;
    for (i = 0; i < n; i++) {
        c = (u8)v[i];
        if (c == ' ' || c == '"' || c == '\\' || c == '=' || c < 0x20 ||
            c == 0x7F)
            return 1;
    }
    return 0;
}

u16 rq_cost(const char *val, u16 n)
{
    u16 i;
    u16 k;
    u8 c;

    if (!must_quote(val, n))
        return n;
    k = 2;
    for (i = 0; i < n; i++) {
        c = (u8)val[i];
        if (c == '"' || c == '\\' || c == '\n')
            k = (u16)(k + 2);
        else if (c < 0x20 || c == 0x7F)
            k = (u16)(k + 4);
        else
            k++;
        if (k > LINK_MAXLINE)
            return (u16)(LINK_MAXLINE + 1);
    }
    return k;
}

/* Append the n-byte value v, quoted and escaped when quote is 1.  The
   callers pass rq_cost(v, n) != n: a bare value costs exactly n bytes,
   a quoted one that fits at least n + 2. */
static void put_val(rq *q, const char *v, u16 n, int quote)
{
    u16 i;
    u8 c;
    char *o;

    o = q->line + q->len;
    if (!quote) {
        memcpy(o, v, n);
        o += n;
    } else {
        *o++ = '"';
        for (i = 0; i < n; i++) {
            c = (u8)v[i];
            if (c == '"' || c == '\\') {
                *o++ = '\\';
                *o++ = (char)c;
            } else if (c == '\n') {
                *o++ = '\\';
                *o++ = 'n';
            } else if (c < 0x20 || c == 0x7F) {
                *o++ = '\\';
                *o++ = 'x';
                *o++ = txt_hexdig[c >> 4];
                *o++ = txt_hexdig[c & 15];
            } else {
                *o++ = (char)c;
            }
        }
        *o++ = '"';
    }
    *o = '\0';
    q->len = (u16)(o - q->line);
}

void rq_begin(rq *q, const char *words)
{
    size_t n;

    n = strlen(words);
    q->over = 0;
    q->len = 0;
    q->line[0] = '\0';
    if (n > LINK_MAXLINE) {
        q->over = 1;
        return;
    }
    memcpy(q->line, words, n + 1);
    q->len = (u16)n;
}

u16 rq_room(const rq *q)
{
    return (u16)(LINK_MAXLINE - q->len);
}

void rq_word(rq *q, const char *w)
{
    u16 n;
    u16 c;

    n = (u16)strlen(w);
    c = rq_cost(w, n);
    if ((u32)c + 1 > rq_room(q)) {
        q->over = 1;
        return;
    }
    q->line[q->len++] = ' ';
    put_val(q, w, n, c != n);
}

int rq_trypairn(rq *q, const char *key, const char *val, u16 n)
{
    u16 k;
    u16 c;

    k = (u16)strlen(key);
    c = rq_cost(val, n);
    if ((u32)k + c + 2 > rq_room(q))
        return 0;
    q->line[q->len++] = ' ';
    memcpy(q->line + q->len, key, k);
    q->len = (u16)(q->len + k);
    q->line[q->len++] = '=';
    put_val(q, val, n, c != n);
    return 1;
}

int rq_trypair(rq *q, const char *key, const char *val)
{
    return rq_trypairn(q, key, val, (u16)strlen(val));
}

void rq_pairn(rq *q, const char *key, const char *val, u16 n)
{
    if (!rq_trypairn(q, key, val, n))
        q->over = 1;
}

void rq_pair(rq *q, const char *key, const char *val)
{
    rq_pairn(q, key, val, (u16)strlen(val));
}

void rq_pairu(rq *q, const char *key, u32 v)
{
    char d[TXT_U32LEN + 1];

    txt_u32(d, v);
    rq_pair(q, key, d);
}

/* ---------------------------------------------------------------------
 * Decoding.
 * --------------------------------------------------------------------- */

#define ST_FIRST        0       /* waiting for the first line           */
#define ST_DATA         1       /* inside +DATA                         */
#define ST_DONE         2       /* the response is complete             */

typedef struct {
    rq_fn   fn;
    void   *arg;
    rq_res *res;
    u8      state;
    u8      stop;               /* the callback stopped the response    */
    u8      bad;                /* the response could not be read       */
    char    buf[LINK_MAXRESP + 1];
    rq_line ln;
} rq_dec;

static int is_sep(char c)
{
    return c == ' ' || c == '\t';
}

/* Decode the quoted text after the quote at *s into *o; *s past the
   closing quote.  0 when malformed.  A \x00 ends the value there. */
static int dec_quoted(const char **s, char **o)
{
    const char *p;
    char *d;
    int h1;
    int h2;
    int cut;
    char c;

    p = *s;
    d = *o;
    cut = 0;
    for (;;) {
        c = *p++;
        if (c == '\0')
            return 0;
        if (c == '"')
            break;
        if (c == '\\') {
            c = *p++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'x') {
                h1 = txt_hexval(p[0]);
                h2 = h1 < 0 ? -1 : txt_hexval(p[1]);
                if (h2 < 0)
                    return 0;
                p += 2;
                c = (char)(u8)(h1 * 16 + h2);
                if (c == '\0')
                    cut = 1;
            } else if (c != '"' && c != '\\') {
                return 0;
            }
        }
        if (!cut)
            *d++ = c;
    }
    *s = p;
    *o = d;
    return 1;
}

/* Split s into values as the protocol's tokenizer does: a K line's
   KEY=value gives the key and the value as two values. */
static int dec_split(rq_dec *x, const char *s, int kv)
{
    char *o;
    rq_line *ln;
    char *start;

    o = x->buf;
    ln = &x->ln;
    ln->n = 0;
    ln->over = 0;
    for (;;) {
        while (is_sep(*s))
            s++;
        if (*s == '\0')
            return 1;
        start = o;
        if (kv && ln->n == 0) {
            while (*s != '\0' && *s != '=' && !is_sep(*s))
                *o++ = *s++;
            if (*s != '=')
                return 0;
            s++;
            *o++ = '\0';
            ln->v[ln->n++] = start;
            start = o;
        }
        if (*s == '"') {
            s++;
            if (!dec_quoted(&s, &o))
                return 0;
            if (*s != '\0' && !is_sep(*s))
                return 0;
        } else {
            while (*s != '\0' && !is_sep(*s))
                *o++ = *s++;
        }
        *o++ = '\0';
        if (ln->n < RQ_MAXVALS)
            ln->v[ln->n++] = start;
        else
            ln->over++;
        if (kv)
            return 1;
    }
}

/* The text s of an L or H line into d, unescaped (\\ and \xHH) but not
   split. */
static void dec_text(char *d, const char *s)
{
    u16 i;
    u16 k;
    int h1;
    int h2;
    char c;

    i = 0;
    k = 0;
    for (;;) {
        c = s[i];
        if (c == '\0')
            break;
        i++;
        if (c == '\\' && s[i] == '\\') {
            i++;
        } else if (c == '\\' && s[i] == 'x') {
            h1 = txt_hexval(s[i + 1]);
            h2 = h1 < 0 ? -1 : txt_hexval(s[i + 2]);
            if (h2 >= 0) {
                if (h1 == 0 && h2 == 0)
                    break;
                c = (char)(u8)(h1 * 16 + h2);
                i = (u16)(i + 3);
            }
        }
        d[k++] = c;
    }
    d[k] = '\0';
}

/* The value of the up to four digits at s, *end past them, or NULL
   when s has none. */
static u16 dec_num(const char *s, const char **end)
{
    u16 v;
    u16 k;

    v = 0;
    k = 0;
    while (TXT_ISDIGIT(*s) && k < 4) {
        v = (u16)(v * 10 + (u16)(*s - '0'));
        s++;
        k++;
    }
    *end = k == 0 ? NULL : s;
    return v;
}

/* The first line of a response, s of len bytes. */
static void dec_first(rq_dec *x, const char *s, u16 len)
{
    rq_res *r;
    const char *p;
    const char *f;
    size_t n;
    u16 code;

    r = x->res;
    if (strncmp(s, "+OK", 3) == 0 && (s[3] == '\0' || s[3] == ' ')) {
        r->code = RQ_OK;
        txt_copy(r->text, s[3] == ' ' ? s + 4 : "", (u16)sizeof r->text);
        x->state = ST_DONE;
        return;
    }
    if (strcmp(s, "+DATA") == 0) {
        r->code = RQ_OK;
        r->data = 1;
        x->state = ST_DATA;
        return;
    }
    if (strncmp(s, "-ERR ", 5) == 0) {
        code = dec_num(s + 5, &p);
        if (p != NULL && (*p == '\0' || *p == ' ')) {
            r->code = (int)code;
            if (*p == ' ')
                p++;
            n = (size_t)(len - (p - s));
            f = NULL;
            if (n >= 7) {
                for (f = p + n; f > p && f[-1] != ' '; f--)
                    ;
                if (strncmp(f, "FIELD=", 6) != 0)
                    f = NULL;
            }
            if (f != NULL) {
                txt_copy(r->field, f + 6, (u16)sizeof r->field);
                n = (size_t)(f - p);
                while (n > 0 && p[n - 1] == ' ')
                    n--;
            }
            if (n > RQ_TEXTLEN)
                n = RQ_TEXTLEN;
            memcpy(r->text, p, n);
            r->text[n] = '\0';
            x->state = ST_DONE;
            return;
        }
    }
    x->bad = 1;
}

/* One data line; nonzero to stop the response. */
static int dec_data(rq_dec *x, const char *s)
{
    rq_res *r;
    const char *t;
    const char *end;
    char kind;

    r = x->res;
    if (s[0] == '.') {
        if (s[1] == '\0') {
            x->state = ST_DONE;
            return 0;
        }
        s++;
    }
    kind = s[0];
    if (kind == '\0' || (s[1] != '\0' && s[1] != ' ')) {
        x->bad = 1;
        return 1;
    }
    t = s + 1;
    if (*t == ' ')
        t++;
    x->ln.kind = kind;
    x->ln.n = 0;
    x->ln.over = 0;
    switch (kind) {
    case 'C':
        txt_copy(r->more, t, (u16)sizeof r->more);
        return 0;
    case 'L':
    case 'H':
        dec_text(x->buf, t);
        x->ln.n = 1;
        x->ln.v[0] = x->buf;
        break;
    case 'P':
        break;
    case 'K':
    case 'F':
    case 'R':
        if (!dec_split(x, s + 1, kind == 'K')) {
            x->bad = 1;
            return 1;
        }
        if (kind == 'K' && x->ln.n == 2 && strcmp(x->ln.v[0], "ERROR") == 0) {
            r->code = (int)dec_num(x->ln.v[1], &end);
            if (r->code == RQ_OK)
                r->code = RQ_EINTERNAL;
            return 0;
        }
        break;
    default:
        return 0;
    }
    if (x->fn != NULL && x->fn(x->arg, &x->ln) != 0) {
        x->stop = 1;
        return 1;
    }
    return 0;
}

/* link_line_fn of rq_send: decode one response line into arg, an
   rq_dec; nonzero to stop the response. */
static int dec_line(void *arg, const char *line, u16 len)
{
    rq_dec *x;

    x = (rq_dec *)arg;
    if (x->state == ST_FIRST) {
        dec_first(x, line, len);
        return x->bad;
    }
    if (x->state == ST_DATA)
        return dec_data(x, line);
    x->bad = 1;
    return 1;
}

int rq_send(link *l, rq *q, rq_fn fn, void *arg, rq_res *res)
{
    rq_dec x;
    int rc;

    res->code = RQ_EINTERNAL;
    res->data = 0;
    res->closed = 0;
    res->text[0] = '\0';
    res->field[0] = '\0';
    res->more[0] = '\0';
    if (q->over) {
        res->code = RQ_ETOOLONG;
    } else if (!rq_busy) {
        x.fn = fn;
        x.arg = arg;
        x.res = res;
        x.state = ST_FIRST;
        x.stop = 0;
        x.bad = 0;
        rq_busy = 1;
        rc = link_send(l, q->line, q->len, dec_line, &x);
        rq_busy = 0;
        if (rc == LINK_CLOSED || rc == LINK_ELOST)
            res->closed = 1;
        if (rc == LINK_ELOST)
            res->code = RQ_ELOST;
        else if (x.stop)
            res->code = RQ_ESTOP;
        else if (x.bad)
            res->code = RQ_EINTERNAL;
        else if (x.state == ST_FIRST)
            res->code = rc == LINK_CLOSED ? RQ_ECLOSED : RQ_EINTERNAL;
        else if (x.state != ST_DONE)
            res->code = RQ_EINTERNAL;
    }
    txt_wipe(q->line, (u16)sizeof q->line);
    q->len = 0;
    return res->code;
}

int rq_okval(const rq_res *res, const char *key, char *buf, u16 size)
{
    char tmp[RQ_TEXTLEN + 1];
    const char *s;
    char *o;
    size_t k;

    if (size == 0)
        return 0;
    buf[0] = '\0';
    s = res->text;
    k = strlen(key);
    while (*s != '\0') {
        while (is_sep(*s))
            s++;
        if (strncmp(s, key, k) == 0 && s[k] == '=') {
            s += k + 1;
            o = tmp;
            if (*s == '"') {
                s++;
                if (!dec_quoted(&s, &o))
                    return 0;
            } else {
                while (*s != '\0' && !is_sep(*s))
                    *o++ = *s++;
                /* A bare value running into the end of a text that was
                   cut may itself have been cut. */
                if (*s == '\0' && strlen(res->text) >= RQ_TEXTLEN)
                    return 0;
            }
            *o = '\0';
            txt_copy(buf, tmp, size);
            return 1;
        }
        while (*s != '\0' && !is_sep(*s)) {
            if (*s == '"') {
                s++;
                while (*s != '\0' && *s != '"') {
                    if (*s == '\\' && s[1] != '\0')
                        s++;
                    s++;
                }
                if (*s == '"')
                    s++;
            } else {
                s++;
            }
        }
    }
    return 0;
}
