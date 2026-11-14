/*
 * TOKEN.C - the reading side of the line protocol: assembling request
 * lines from transport bytes and splitting them into tokens.  The
 * response writer is RESPONSE.C, apart so that a program that only
 * reads requests (THIN, the network-only client) links no writer.
 *
 * Line assembly keeps at most PROTO_MAXLINE payload bytes and one CR in
 * proto_acc.buf, so a payload of PROTO_MAXLINE bytes ended by CR LF
 * still fits; one byte more marks the line over-long, and every later
 * byte up to the LF is dropped.  A CR that is not just before the LF is
 * an ordinary byte of the line, as are NUL and bytes 80..FF.
 *
 * The tokenizer copies each decoded token, NUL-terminated, into
 * proto_req.buf.  Decoding never makes a token longer than its source,
 * and the '=' of a pair becomes the NUL ending its key, so the buffer
 * needs at most one byte per token beyond the line itself.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "ENGINE.H"
#include "PROTO.H"

/* ---------------------------------------------------------------------
 * Line assembly.
 * --------------------------------------------------------------------- */

/* Empty the buffer, wiping every byte the last line may have left: its
   text and the NUL after it (which also covers the CR it dropped), so a
   password in a LOGIN line does not linger until a longer line covers
   it. */
static void acc_clear(proto_acc *a)
{
    u16 n;

    n = (u16)(a->len + 1);
    if (n > sizeof a->buf)
        n = (u16)sizeof a->buf;
    txt_wipe(a->buf, n);
    a->len = 0;
}

void proto_acc_init(proto_acc *a)
{
    a->len = (u16)(sizeof a->buf - 1);
    acc_clear(a);
    a->over = 0;
    a->ready = 0;
}

void proto_acc_wipe(proto_acc *a)
{
    if (a->ready)
        acc_clear(a);
}

int proto_acc_feed(proto_acc *a, int c)
{
    if (a->ready) {
        a->ready = 0;
        acc_clear(a);
    }
    if (c == '\n') {
        if (a->over) {
            a->over = 0;
            acc_clear(a);
            return PROTO_ACC_LONG;
        }
        if (a->len > 0 && a->buf[a->len - 1] == '\r')
            a->len--;
        if (a->len > PROTO_MAXLINE) {
            acc_clear(a);
            return PROTO_ACC_LONG;
        }
        a->buf[a->len] = '\0';
        a->ready = 1;
        return PROTO_ACC_LINE;
    }
    if (a->over)
        return PROTO_ACC_NONE;
    if (a->len >= PROTO_MAXLINE + 1) {
        a->over = 1;
        return PROTO_ACC_NONE;
    }
    a->buf[a->len] = (char)(u8)(c & 0xFF);
    a->len++;
    return PROTO_ACC_NONE;
}

/* ---------------------------------------------------------------------
 * Tokenizer.
 * --------------------------------------------------------------------- */

static int is_sep(char c)
{
    return c == ' ' || c == '\t';
}

/* Decode the quoted text starting after the opening quote at line[*pos]
   into out[*o..]; on return *pos is past the closing quote, which must be
   followed by a separator or the end of the line. */
static int parse_quoted(const char *line, u16 len, u16 *pos, char *out,
                        u16 *o)
{
    u16 i;
    u16 k;
    int h1;
    int h2;
    char c;

    i = *pos;
    k = *o;
    for (;;) {
        if (i >= len)
            return ENG_ESYNTAX;
        c = line[i];
        if (c == '"')
            break;
        if (c == '\\') {
            if (i + 1 >= len)
                return ENG_ESYNTAX;
            c = line[i + 1];
            if (c == '"' || c == '\\') {
                out[k++] = c;
                i = (u16)(i + 2);
            } else if (c == 'n') {
                out[k++] = '\n';
                i = (u16)(i + 2);
            } else if (c == 'x') {
                if (i + 3 >= len)
                    return ENG_ESYNTAX;
                h1 = txt_hexval(line[i + 2]);
                h2 = txt_hexval(line[i + 3]);
                if (h1 < 0 || h2 < 0 || (h1 == 0 && h2 == 0))
                    return ENG_ESYNTAX;
                out[k++] = (char)(u8)(h1 * 16 + h2);
                i = (u16)(i + 4);
            } else {
                return ENG_ESYNTAX;
            }
        } else {
            out[k++] = c;
            i++;
        }
    }
    i++;
    if (i < len && !is_sep(line[i]))
        return ENG_ESYNTAX;
    *pos = i;
    *o = k;
    return ENG_OK;
}

int proto_parse(proto_req *r, const char *line, u16 len)
{
    u16 i;
    u16 o;
    u16 start;
    u16 j;
    u16 eq;
    char *tok;
    char c;
    int rc;

    r->nargs = 0;
    r->npairs = 0;
    r->bad[0] = '\0';
    r->buf[0] = '\0';
    if (len > PROTO_MAXLINE)
        return ENG_ETOOLONG;
    for (i = 0; i < len; i++)
        if (line[i] == '\0')
            return ENG_ESYNTAX;

    i = 0;
    o = 0;
    for (;;) {
        while (i < len && is_sep(line[i]))
            i++;
        if (i >= len)
            break;
        tok = r->buf + o;
        if (line[i] == '"') {
            /* A quoted positional token. */
            if (r->npairs > 0 || r->nargs >= PROTO_MAXARGS)
                return ENG_ESYNTAX;
            i++;
            rc = parse_quoted(line, len, &i, r->buf, &o);
            if (rc != ENG_OK)
                return rc;
            r->buf[o++] = '\0';
            r->arg[r->nargs++] = tok;
            continue;
        }
        /* A bare token up to its end or its first '='. */
        start = i;
        while (i < len && !is_sep(line[i]) && line[i] != '=') {
            if (line[i] == '"')
                return ENG_ESYNTAX;
            i++;
        }
        if (i >= len || line[i] != '=') {
            if (r->npairs > 0 || r->nargs >= PROTO_MAXARGS)
                return ENG_ESYNTAX;
            for (j = start; j < i; j++)
                r->buf[o++] = line[j];
            r->buf[o++] = '\0';
            r->arg[r->nargs++] = tok;
            continue;
        }
        /* A pair: KEY=value or KEY="value". */
        eq = i;
        if (r->npairs >= PROTO_MAXPAIRS)
            return ENG_ESYNTAX;
        if (eq == start || eq - start > PROTO_MAXKEY)
            return ENG_ESYNTAX;
        /* A letter or '_', then letters, digits and '_'. */
        for (j = start; j < eq; j++) {
            c = line[j];
            if (!TXT_ISALPHA(c) && c != '_' &&
                (j == start || !TXT_ISDIGIT(c)))
                return ENG_ESYNTAX;
            r->buf[o++] = (char)TXT_UPPER(c);
        }
        r->buf[o++] = '\0';
        r->pair[r->npairs].key = tok;
        r->pair[r->npairs].val = r->buf + o;
        i++;
        if (i < len && line[i] == '"') {
            i++;
            rc = parse_quoted(line, len, &i, r->buf, &o);
            if (rc != ENG_OK)
                return rc;
        } else {
            while (i < len && !is_sep(line[i])) {
                if (line[i] == '"')
                    return ENG_ESYNTAX;
                r->buf[o++] = line[i++];
            }
        }
        r->buf[o++] = '\0';
        r->npairs++;
    }

    for (i = 0; i < r->npairs; i++)
        for (j = (u16)(i + 1); j < r->npairs; j++)
            if (strcmp(r->pair[i].key, r->pair[j].key) == 0) {
                strcpy(r->bad, r->pair[i].key);
                return ENG_EARG;
            }
    return ENG_OK;
}

const char *proto_get(const proto_req *r, const char *key)
{
    u16 i;

    for (i = 0; i < r->npairs; i++)
        if (strcmp(r->pair[i].key, key) == 0)
            return r->pair[i].val;
    return NULL;
}

int proto_in_list(const char *list, const char *key)
{
    const char *p;
    const char *e;
    u16 klen;
    u16 n;

    klen = (u16)strlen(key);
    p = list;
    for (;;) {
        while (*p == ' ')
            p++;
        e = p;
        while (*e != '\0' && *e != ',')
            e++;
        n = (u16)(e - p);
        while (n > 0 && p[n - 1] == ' ')
            n--;
        if (n > 0 && n == klen && txt_ieqn(p, key, n))
            return 1;
        if (*e == '\0')
            return 0;
        p = e + 1;
    }
}
