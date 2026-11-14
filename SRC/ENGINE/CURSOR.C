/*
 * CURSOR.C - paging cursors and bounded scans.
 *
 * A scan over an index keeps the index positioned so that the next
 * ndx_next gives the next entry to examine: after eng_scan_open on the
 * entry before the first candidate (or before the first entry), and
 * after each examined entry on that entry.  s->key and s->recno hold the
 * last entry examined, or the cursor the scan was opened with; recno 0
 * means neither, and its cursor text is "", which as AFTER= starts the
 * same scan from the beginning again.  A scan in record order keeps only
 * s->recno; in reverse record order (ENG_SCAN_RRECORDS) the next record
 * is the one below s->recno, or the last one when s->recno is 0.  A
 * bound (s->blen, s->bound) ends an index scan at the first entry whose
 * key does not start below it, before that entry is examined.
 *
 * Opening on a cursor seeks the first entry above (key, recno) in key
 * and record-number order (ndx_seek_after): a descent of the index that
 * binary-searches the child pages of a run of equal keys, so a few page
 * reads whatever the run's length.  The scan so continues strictly after
 * (key, recno) whether or not that entry still exists, and records
 * inserted or deleted between pages neither repeat nor hide the others.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGTX.H"

#define SC_CLOSED       0       /* not opened: a zeroed scan            */
#define SC_ON           1       /* positioned for the next entry        */
#define SC_END          2       /* the range is exhausted               */
/* A scan whose index read or record read failed holds that ENG_E code
   (ENG_ESYNTAX or above) as its state: its place is lost or its next
   record unreadable, so every later step fails with the same code
   rather than starting over, skipping the record, or ending. */

#define RECNO_DIGITS    8       /* hex digits of a cursor's recno       */

static int bad_cursor(eng *e)
{
    return eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_BADCUR);
}

/* ENG_EINTERNAL for a scan the caller set up wrongly. */
static int bad_scan(eng *e)
{
    return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_BADSCAN);
}

/* 1 for a scan in record order, either way. */
#define IN_RECORDS(s) ((s)->ix == ENG_SCAN_RECORDS || \
                       (s)->ix == ENG_SCAN_RRECORDS)

/* Key length of the scan's index (0 in record order). */
static u16 scan_keylen(const eng_scan *s)
{
    if (IN_RECORDS(s))
        return 0;
    return eng_index_def(s->ix)->keylen;
}

/* Name the scan's cursors carry. */
static const char *scan_name(const eng_scan *s)
{
    if (IN_RECORDS(s))
        return eng_table_def(s->table)->name;
    return eng_index_def(s->ix)->name;
}

/* Parse cursor text c of name and key length keylen into key (padded
   with spaces to the key length) and *recno; ENG_ECURSOR when it breaks
   a rule. */
static int parse_text(eng *e, const char *name, u16 keylen, const char *c,
                      u8 *key, u32 *recno)
{
    const char *p;
    u16 n;
    u16 i;
    int hi;
    int lo;

    for (n = 0; c[n] != '\0' && c[n] != ':'; n++)
        if (name[n] == '\0' || TXT_UPPER(c[n]) != name[n])
            return bad_cursor(e);
    if (c[n] != ':' || name[n] != '\0')
        return bad_cursor(e);
    p = c + n + 1;
    memset(key, ' ', keylen);
    for (i = 0; p[0] != ':'; i++, p += 2) {
        if (i >= keylen)
            return bad_cursor(e);
        hi = txt_hexval(p[0]);
        lo = hi < 0 ? -1 : txt_hexval(p[1]);
        if (lo < 0)
            return bad_cursor(e);
        key[i] = (u8)(hi * 16 + lo);
    }
    p++;
    *recno = 0;
    for (n = 0; p[n] != '\0'; n++) {
        hi = txt_hexval(p[n]);
        if (hi < 0 || n >= RECNO_DIGITS)
            return bad_cursor(e);
        *recno = *recno * 16 + (u32)hi;
    }
    if (n == 0 || *recno == 0)
        return bad_cursor(e);
    return ENG_OK;
}

/* Parse cursor text c for scan s, whose key must start with the scan's
   prefix. */
static int parse_cursor(eng *e, eng_scan *s, const char *c, u8 *key,
                        u32 *recno)
{
    int rc;

    rc = parse_text(e, scan_name(s), scan_keylen(s), c, key, recno);
    if (rc == ENG_OK && s->plen > 0 &&
        memcmp(key, s->prefix, s->plen) != 0)
        rc = bad_cursor(e);
    return rc;
}

/* 1 when the key at cur is in the scan's range: it starts with the
   prefix and, with a bound, is below the bound. */
static int in_range(eng_scan *s, const u8 *cur)
{
    if (s->plen > 0 && memcmp(cur, s->prefix, s->plen) != 0)
        return 0;
    return s->blen == 0 || memcmp(cur, s->bound, s->blen) < 0;
}

/* ENG_EINTERNAL for a bound the scan cannot have: longer than
   ENG_BOUNDLEN or than the index's key (any bound in record order). */
static int bad_bound(eng *e, eng_scan *s)
{
    if (s->blen > ENG_BOUNDLEN || s->blen > scan_keylen(s))
        return bad_scan(e);
    return ENG_OK;
}

/* Step the index back once, so that the next ndx_next gives the entry it
   is on now. */
static int step_back(eng *e, ndx *x)
{
    u32 r;

    return eng_dbferr(e, ndx_prev(x, &r), 1);
}

/* Record the failed index call rc (a DBF_E code) and leave the scan
   failed with its ENG_E code. */
static int scan_fail(eng *e, eng_scan *s, int rc)
{
    s->state = eng_dbferr(e, rc, 1);
    return s->state;
}

/* The code of a failed scan.  The field and text of the original
   failure stay as they are; only when they were cleared since is a
   text recorded again. */
static int still_failed(eng *e, eng_scan *s)
{
    if (eng_errtx(e) != ENG_TX_NONE || eng_errfield(e)[0] != '\0')
        return s->state;
    return eng_fail(e, s->state, NULL, ENG_TX_SCANFAIL);
}

int eng_scan_open(eng *e, eng_scan *s, int t, int ix,
                  const u8 *prefix, u16 plen, const char *after)
{
    const eng_xdef *xd;
    ndx *x;
    u32 crec;
    u32 r;
    int exact;
    int rc;

    memset(s, 0, sizeof *s);
    if (t < 0 || t >= ENG_NTABLES)
        return bad_scan(e);
    if (ix == ENG_SCAN_RECORDS || ix == ENG_SCAN_RRECORDS) {
        if (plen != 0)
            return bad_scan(e);
    } else {
        if (ix < 0 || ix >= ENG_NINDEX)
            return bad_scan(e);
        xd = eng_index_def(ix);
        if (xd->table != t || plen > xd->keylen)
            return bad_scan(e);
    }
    s->table = t;
    s->ix = ix;
    s->plen = plen;
    if (plen > 0)
        memcpy(s->prefix, prefix, plen);
    memset(s->key, ' ', sizeof s->key);
    crec = 0;
    if (after != NULL && after[0] != '\0') {
        rc = parse_cursor(e, s, after, s->key, &crec);
        if (rc != ENG_OK)
            return rc;
    }
    s->recno = crec;
    s->state = SC_ON;
    if (IN_RECORDS(s))
        return ENG_OK;

    x = eng_ndx(e, ix);
    if (crec == 0)
        rc = ndx_seek(x, s->prefix, plen, &r, &exact);
    else
        rc = ndx_seek_after(x, s->key, crec, &r);
    if (rc != DBF_OK)
        return scan_fail(e, s, rc);
    if (r == 0) {
        s->state = SC_END;
        return ENG_OK;
    }
    rc = step_back(e, x);
    if (rc != ENG_OK)
        s->state = rc;
    return rc;
}

int eng_scan_next(eng *e, eng_scan *s, u32 *recno)
{
    u8 cur[ENG_MAXKEYLEN];
    ndx *x;
    u32 r;
    int rc;

    *recno = 0;
    if (s->state == SC_CLOSED)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOSCAN);
    if (s->state >= ENG_ESYNTAX)
        return still_failed(e, s);
    rc = bad_bound(e, s);
    if (rc != ENG_OK)
        return rc;
    x = IN_RECORDS(s) ? NULL : eng_ndx(e, s->ix);
    while (s->state == SC_ON && s->reads < ENG_MAXREADS) {
        if (s->ix == ENG_SCAN_RRECORDS) {
            r = eng_reccount(e, s->table);
            if (s->recno != 0 && s->recno <= r)
                r = s->recno - 1;
            if (r == 0) {
                s->state = SC_END;
                break;
            }
        } else if (x == NULL) {
            if (s->recno >= eng_reccount(e, s->table)) {
                s->state = SC_END;
                break;
            }
            r = s->recno + 1;
        } else {
            rc = ndx_next(x, &r);
            if (rc == DBF_OK && r != 0)
                rc = ndx_key(x, cur);
            if (rc != DBF_OK)
                return scan_fail(e, s, rc);
            if (r == 0 || !in_range(s, cur)) {
                s->state = SC_END;
                break;
            }
        }
        /* A record that cannot be read fails the scan before it moves,
           so the cursor of the last entry examined stays before it. */
        rc = eng_rec_read(e, s->table, r);
        if (rc != ENG_OK) {
            s->state = rc;
            return rc;
        }
        if (x != NULL)
            memcpy(s->key, cur, ndx_keylen(x));
        s->recno = r;
        s->reads++;
        if (dbf_is_deleted(eng_dbf(e, s->table)))
            continue;
        if (s->match != NULL && !s->match(e, s))
            continue;
        *recno = r;
        return ENG_OK;
    }
    return ENG_OK;
}

int eng_scan_more(eng *e, eng_scan *s, int *more)
{
    u8 cur[ENG_MAXKEYLEN];
    ndx *x;
    u32 r;
    int in;
    int rc;

    *more = 0;
    if (s->state == SC_CLOSED)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOSCAN);
    if (s->state >= ENG_ESYNTAX)
        return still_failed(e, s);
    rc = bad_bound(e, s);
    if (rc != ENG_OK)
        return rc;
    if (s->state != SC_ON)
        return ENG_OK;
    if (s->ix == ENG_SCAN_RECORDS) {
        *more = s->recno < eng_reccount(e, s->table) ? 1 : 0;
        return ENG_OK;
    }
    if (s->ix == ENG_SCAN_RRECORDS) {
        *more = s->recno != 1 && eng_reccount(e, s->table) > 0 ? 1 : 0;
        return ENG_OK;
    }
    x = eng_ndx(e, s->ix);
    rc = ndx_next(x, &r);
    if (rc != DBF_OK)
        return scan_fail(e, s, rc);
    if (r == 0)
        return ENG_OK;
    rc = ndx_key(x, cur);
    if (rc != DBF_OK)
        return scan_fail(e, s, rc);
    in = in_range(s, cur);
    rc = ndx_prev(x, &r);
    if (rc != DBF_OK)
        return scan_fail(e, s, rc);
    *more = in;
    return ENG_OK;
}

int eng_scan_any(eng *e, int t, int ix, const u8 *key, u16 len,
                 int *found)
{
    eng_scan s;
    u32 r;
    int more;
    int rc;

    *found = 0;
    rc = eng_scan_open(e, &s, t, ix, key, len, NULL);
    if (rc == ENG_OK)
        rc = eng_scan_next(e, &s, &r);
    if (rc != ENG_OK)
        return rc;
    if (r != 0) {
        *found = 1;
        return ENG_OK;
    }
    rc = eng_scan_more(e, &s, &more);
    if (rc == ENG_OK && more)
        *found = 2;
    return rc;
}

/* The cursor text of entry (key, recno) of the index or table name,
   whose keys are len bytes, into out (ENG_CURSORMAX bytes). */
static void cursor_text(const char *name, const u8 *key, u16 len,
                        u32 recno, char *out)
{
    char tmp[RECNO_DIGITS];
    u32 v;
    u16 n;
    u16 i;

    for (n = 0; name[n] != '\0'; n++)
        out[n] = name[n];
    out[n++] = ':';
    while (len > 0 && key[len - 1] == ' ')
        len--;
    for (i = 0; i < len; i++) {
        out[n++] = txt_hexdig[key[i] >> 4];
        out[n++] = txt_hexdig[key[i] & 15];
    }
    out[n++] = ':';
    v = recno;
    i = 0;
    while (v != 0) {
        tmp[i++] = txt_hexdig[(int)(v & 15)];
        v >>= 4;
    }
    while (i > 0)
        out[n++] = tmp[--i];
    out[n] = '\0';
}

void eng_scan_cursor(const eng_scan *s, char *out)
{
    out[0] = '\0';
    if (s->state == SC_CLOSED || s->recno == 0)
        return;
    cursor_text(scan_name(s), s->key, scan_keylen(s), s->recno, out);
}

void eng_cursor_text(int ix, const u8 *key, u32 recno, char *out)
{
    const eng_xdef *xd;

    out[0] = '\0';
    xd = eng_index_def(ix);
    if (xd == NULL || recno == 0)
        return;
    cursor_text(xd->name, key, xd->keylen, recno, out);
}

int eng_cursor_parse(eng *e, int ix, const char *after, u8 *key,
                     u32 *recno)
{
    const eng_xdef *xd;

    *recno = 0;
    xd = eng_index_def(ix);
    if (xd == NULL)
        return bad_scan(e);
    if (after == NULL)
        return bad_cursor(e);
    return parse_text(e, xd->name, xd->keylen, after, key, recno);
}
