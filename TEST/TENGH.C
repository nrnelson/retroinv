/*
 * TENGH.C - engine scaffolding of the unit tests (TENGH.H).
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#include "ENGLOCS.H"
#define TF_PART
#define TF_FILE "TENGH"
#include "TESTFW.H"
#include "T_FILES.H"
#define TE_AUDCUT
#include "TENGH.H"

/* The scratch directory is the data directory, so the engine must take
   every prefix tfs_set_dir accepts. */
extern char te_dir_fits[1];
extern char te_dir_fits[(TFS_MAXDIR == ENG_MAXDIR) ? 1 : 2];

eng_pair te_p[TE_MAXP];
u32 te_index_rec;

static eng *te_eng;             /* the engine of the last te_open       */
static char te_spec[TE_TEXTMAX];        /* te_kv's copy of its spec     */
static char te_text[ENG_MAXVAL];        /* te_rtext's result            */
static char te_row[TE_TEXTMAX];         /* te_aud's result              */
static u32 te_cnt[ENG_NTABLES];         /* record counts at te_snap     */
static int te_marked;                   /* ENG_EDITMARK at te_snap      */

/* ---------------------------------------------------------------------
 * Files and the engine.
 * --------------------------------------------------------------------- */

/* Copy stem+live to stem+saved in the data directory when save, else
   back, keeping the first error in *first; a missing source removes
   the target and is no error. */
static void save_one(int save, const char *stem, const char *live,
                     const char *saved, int *first)
{
    int rc;

    if (save)
        rc = tfs_copy(tfs_path(stem, live), tfs_path(stem, saved));
    else
        rc = tfs_copy(tfs_path(stem, saved), tfs_path(stem, live));
    if (rc != CF_ENOENT && rc != CF_ENOPATH && *first == 0)
        *first = rc;
}

int te_save(int save)
{
    char mark[DOS_PATHMAX];
    const char *stem;
    int first;
    int i;

    first = 0;
    for (i = 0; i < ENG_NTABLES; i++) {
        stem = eng_table_def(i)->name;
        save_one(save, stem, ".DBF", ".SDF", &first);
        save_one(save, stem, ".DBT", ".SDT", &first);
    }
    for (i = 0; i < ENG_NINDEX; i++)
        save_one(save, eng_index_def(i)->name, ".NDX", ".SNX", &first);
    /* txt_path_ext takes the extension without its dot. */
    if (txt_path_ext(mark, (u16)sizeof mark, ENG_EDITMARK, "S$$", 0) == 0)
        save_one(save, "", ENG_EDITMARK, mark, &first);
    return first;
}

int te_open(eng **e, const eng_time *now)
{
    char why[TFS_WHYSZ];
    int rc;

    why[0] = '\0';
    rc = eng_open(e, tfs_dir(), now, why, (u16)sizeof why);
    te_eng = *e;
    if (rc != ENG_OK) {
        if (cf_fault == NULL)
            printf("eng_open: %s\n", why);
        return rc;
    }
    eng_set_actor(*e, "STA1", "TESTER");
    return rc;
}

int te_marker_put(const char *tables)
{
    char text[ENG_MARKMAX];
    const char *s;
    u16 n;

    n = 0;
    for (s = tables; *s != '\0'; s++) {
        if (n + 3u > sizeof text)
            return CF_EINVAL;
        if (*s == '|') {
            text[n++] = '\r';
            text[n++] = '\n';
        } else {
            text[n++] = *s;
        }
    }
    if (n + 2u > sizeof text)
        return CF_EINVAL;
    text[n++] = '\r';
    text[n++] = '\n';
    return tfs_write_all(tfs_path(ENG_EDITMARK, ""), text, n);
}

/* ---------------------------------------------------------------------
 * Pairs, records and audit rows.
 * --------------------------------------------------------------------- */

u16 te_kv(const char *spec)
{
    char *s;
    u16 n;

    if (strlen(spec) >= sizeof te_spec) {
        printf("te_kv: spec too long: %s\n", spec);
        TF_CHECK(0);
        return 0;
    }
    strcpy(te_spec, spec);
    s = te_spec;
    n = 0;
    while (*s != '\0') {
        if (n == TE_MAXP) {
            printf("te_kv: too many pairs: %s\n", spec);
            TF_CHECK(0);
            return 0;
        }
        te_p[n].key = s;
        while (*s != '=' && *s != '|' && *s != '\0')
            s++;
        te_p[n].val = "";
        if (*s == '=') {
            *s++ = '\0';
            te_p[n].val = s;
            while (*s != '|' && *s != '\0')
                s++;
        }
        if (*s == '|')
            *s++ = '\0';
        n++;
    }
    return n;
}

u32 te_loc(const char *code, u32 parent)
{
    char spec[TE_TEXTMAX];
    u32 id;
    int rc;

    sprintf(spec, "LOC_NAME=%s|LOC_CODE=%s|LTYPE=BIN|PARENT_ID=%lu", code,
            code, (unsigned long)parent);
    id = 0;
    rc = te_eng != NULL ? eng_loc_add(te_eng, te_p, te_kv(spec), &id) :
                          ENG_EINTERNAL;
    if (rc != ENG_OK) {
        printf("te_loc %s: %d\n", code, rc);
        TF_CHECK(0);
    }
    return id;
}

/* The number of field name of table t of the open engine, after
   reading record recno (TE_BUF: none); -1 when either fails. */
static int field_at(int t, u32 recno, const char *name)
{
    if (te_eng == NULL ||
        (recno != TE_BUF && eng_rec_read(te_eng, t, recno) != ENG_OK))
        return -1;
    return eng_field_num(te_eng, t, name);
}

const char *te_rtext(int t, u32 recno, const char *name)
{
    int f;

    f = field_at(t, recno, name);
    if (f < 0 || eng_rec_text(te_eng, t, (u16)f, te_text) != ENG_OK)
        strcpy(te_text, "?");
    return te_text;
}

i32 te_rnum(int t, u32 recno, const char *name)
{
    int f;

    f = field_at(t, recno, name);
    return f < 0 ? 0 : eng_rec_num(te_eng, t, (u16)f);
}

u32 te_irec(u32 id)
{
    char key[ENG_IDLEN + 1];
    u32 r;
    int exact;

    if (te_eng == NULL || !eng_id_key(id, key) ||
        ndx_seek(eng_ndx(te_eng, ENG_X_ITEMS1), (const u8 *)key, ENG_IDLEN,
                 &r, &exact) != DBF_OK || !exact)
        return 0;
    return r;
}

int te_raw_put(int t, u32 recno, int x, const char *name,
               const char *text, i32 num)
{
    u8 was[ENG_MAXKEYLEN];
    u8 now[ENG_MAXKEYLEN];
    dbf *d;
    int f;
    int rc;

    if (te_eng == NULL)
        return ENG_EINTERNAL;
    rc = eng_rec_read(te_eng, t, recno);
    if (rc != ENG_OK)
        return rc;
    f = eng_field_num(te_eng, t, name);
    if (f < 0)
        return DBF_ERANGE;
    d = eng_dbf(te_eng, t);
    rc = DBF_OK;
    if (x >= 0)
        rc = kx_eval(eng_kx(te_eng, x), d, was);
    if (rc == DBF_OK)
        rc = text != NULL ? dbf_put_str(d, (u16)f, text)
                          : dbf_put_num(d, (u16)f, num);
    if (rc == DBF_OK)
        rc = dbf_write(d);
    if (rc == DBF_OK && x >= 0)
        rc = kx_eval(eng_kx(te_eng, x), d, now);
    if (rc == DBF_OK && x >= 0)
        rc = ndx_delete(eng_ndx(te_eng, x), was, recno);
    if (rc == DBF_OK && x >= 0)
        rc = ndx_insert(eng_ndx(te_eng, x), now, recno);
    if (rc != DBF_OK)
        return rc;
    return eng_flush(te_eng);
}

u32 te_naud(void)
{
    return te_eng != NULL ? eng_reccount(te_eng, ENG_T_AUDIT) : 0;
}

/* A .DBF header holds its record count as an le32 at this offset. */
#define TE_HDRCOUNT     4

u32 te_disk_count(int t)
{
    u8 n[4];

    if (tfs_peek(tfs_path(eng_table_def(t)->name, ".DBF"), TE_HDRCOUNT, n,
                 (u16)sizeof n) != 0)
        return 0;
    return get_le32(n);
}

int te_cut(u32 from)
{
    u32 lo[TE_MAXCUT];
    u32 hi[TE_MAXCUT];
    u32 id;
    u32 r;
    int k;
    int i;

    k = 0;
    for (r = te_naud(); r > from; r--) {
        if (strcmp(te_rtext(ENG_T_AUDIT, r, "ACTION"), "X") == 0) {
            if (k == TE_MAXCUT)
                return -1;
            lo[k] = (u32)te_rnum(ENG_T_AUDIT, r, "REC_ID");
            hi[k] = (u32)strtoul(te_rtext(ENG_T_AUDIT, r, "NEWVAL"), NULL,
                                 10);
            k++;
            continue;
        }
        id = (u32)te_rnum(ENG_T_AUDIT, r, "AUD_ID");
        for (i = 0; i < k && (id < lo[i] || id > hi[i]); i++)
            ;
        if (k > 0 && i == k)
            return -1;
    }
    return k;
}

const char *te_aud(u32 recno, const char *fields)
{
    char name[DBF_NAMELEN + 1];
    char v[ENG_MAXVAL];
    const char *first;
    const char *s;
    size_t n;
    int f;

    te_row[0] = '\0';
    if (te_eng == NULL || recno == 0 ||
        eng_rec_read(te_eng, ENG_T_AUDIT, recno) != ENG_OK)
        return "?";
    first = fields;
    while (*first == ' ')
        first++;
    for (s = first; *s != '\0';) {
        n = 0;
        while (s[n] != ' ' && s[n] != '\0')
            n++;
        strcpy(v, "?");
        if (n < sizeof name) {
            memcpy(name, s, n);
            name[n] = '\0';
            f = eng_field_num(te_eng, ENG_T_AUDIT, name);
            if (f < 0 ||
                eng_rec_text(te_eng, ENG_T_AUDIT, (u16)f, v) != ENG_OK)
                strcpy(v, "?");
        }
        if (strlen(te_row) + 1 + strlen(v) >= sizeof te_row)
            break;
        if (s != first)
            strcat(te_row, "|");
        strcat(te_row, v);
        s += n;
        while (*s == ' ')
            s++;
    }
    return te_row;
}

/* ---------------------------------------------------------------------
 * Checks.
 * --------------------------------------------------------------------- */

void te_snap(void)
{
    int t;

    for (t = 0; t < ENG_NTABLES; t++)
        te_cnt[t] = te_eng != NULL ? eng_reccount(te_eng, t) : 0;
    te_marked = tfs_exists(tfs_path(ENG_EDITMARK, ""));
}

int te_fail(int rc, int code, const char *field, const char *msg,
            const char *file, int line)
{
    const char *gf;
    const char *gm;
    int ok;
    int t;

    if (te_eng == NULL) {
        printf("line %d: no engine is open\n", line);
        return tf_check(0, file, line, "TE_FAIL");
    }
    if (field == NULL)
        field = "";
    gf = eng_errfield(te_eng);
    gm = eng_errmsg(te_eng);
    ok = rc == code && strcmp(gf, field) == 0 &&
         (msg == NULL || strcmp(gm, msg) == 0);
    if (!ok)
        printf("line %d: got %d %s \"%s\", want %d %s \"%s\"\n", line, rc,
               gf, gm, code, field, msg != NULL ? msg : "");
    for (t = 0; t < ENG_NTABLES; t++)
        if (eng_reccount(te_eng, t) != te_cnt[t]) {
            printf("line %d: %s records %lu, were %lu\n", line,
                   eng_table_def(t)->name,
                   (unsigned long)eng_reccount(te_eng, t),
                   (unsigned long)te_cnt[t]);
            ok = 0;
        }
    if (tfs_exists(tfs_path(ENG_EDITMARK, "")) != te_marked) {
        printf("line %d: the edit marker changed\n", line);
        ok = 0;
    }
    eng_clear_error(te_eng);
    return tf_check(ok, file, line, "TE_FAIL");
}

int te_index_ok(eng *e, int x)
{
    u8 want[ENG_MAXKEYLEN];
    u8 got[ENG_MAXKEYLEN];
    u8 *seen;
    u32 count;
    u32 n;
    u32 r;
    int t;
    int ok;

    t = eng_index_def(x)->table;
    count = eng_reccount(e, t);
    te_index_rec = 0;
    /* One byte per record from malloc: test tables stay far below
       that. */
    if (count >= 0x7FFFu)
        return 0;
    seen = (u8 *)malloc((size_t)count + 1u);
    if (seen == NULL)
        return 0;
    memset(seen, 0, (size_t)count + 1u);
    r = 0;
    ok = ndx_first(eng_ndx(e, x), &r) == DBF_OK;
    n = 0;
    while (ok && r != 0) {
        ok = r <= count && !seen[(size_t)r] &&
             ndx_key(eng_ndx(e, x), got) == DBF_OK &&
             eng_rec_read(e, t, r) == ENG_OK &&
             kx_eval(eng_kx(e, x), eng_dbf(e, t), want) == DBF_OK &&
             memcmp(got, want, eng_index_def(x)->keylen) == 0;
        if (ok) {
            seen[(size_t)r] = 1;
            n++;
            ok = ndx_next(eng_ndx(e, x), &r) == DBF_OK;
        } else {
            te_index_rec = r;
        }
    }
    free(seen);
    return ok && n == count;
}
