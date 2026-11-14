/*
 * CSCHEMA.C - copies the schema descriptions that CSCHDAT.C keeps
 * outside DGROUP into the caller's near buffers: a field's name, type,
 * width and decimals, an index's key expression, a seed row of a code
 * table, and a numbered failure text.
 *
 * Only an open of the data directory reads the schema rows (to create
 * and check the tables and indexes, and to seed the code tables), and
 * a text is copied only when a call fails with it, so the far reads
 * cost nothing on a request that succeeds.  The field rows of all tables lie
 * in one run, in ENG_T_ order; a table's first row is found from the
 * field counts of SCHEMA.C.
 *
 * A Windows program reads them from its resource CSCH_RES (CSCHDAT.H)
 * instead, all but the seed rows' tables: each call finds the part it
 * needs through the header, locks the resource and unlocks it before it
 * returns, so no far pointer into it outlives the call and Windows may
 * discard it in between, to load it again at the next lock.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include <stddef.h>
#include "DBF.H"
#include "ENGINE.H"
#define ENGTX_STORE
#include "ENGTX.H"
#include "ENGSCH.H"
#ifdef COMPAT_WIN16
#include "CWIN.H"
#include "CSCHDAT.H"
#endif

extern const u8 FARX csch_stbl[];
#ifndef COMPAT_WIN16
extern const char FARX csch_fname[][DBF_NAMELEN + 1];
extern const char FARX csch_ftype[];
extern const u8 FARX csch_flen[];
extern const u8 FARX csch_fdec[];
extern const char FARX csch_scode[][ENG_LTYPELEN + 1];
extern const char FARX csch_sname[][ENG_CODENAMELEN + 1];
extern const struct eng_txs FARX csch_tx;
extern const u8 FARX csch_txlen[];
#endif

#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#ifndef COMPAT_WIN16
#define SCH_INDEX(x, t, expr, keylen, unique, dup) \
    extern const char FARX csch_x_##x[];
#define SCH_SEED(t, code, name)
#include "SCHTAB.H"
#undef SCH_INDEX
#undef SCH_SEED
#endif

/* CSCH_NSEED, the number of seed rows. */
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name) + 1
enum csch_seeds {
    CSCH_NSEED = 0
#include "SCHTAB.H"
};
#undef SCH_INDEX
#undef SCH_SEED

#ifndef COMPAT_WIN16
/* Seed rows give nothing in the include of eng_index_expr's switch,
   which defines only SCH_INDEX itself. */
#define SCH_SEED(t, code, name)
#endif

eng_schema_fault_fn eng_schema_fault = 0;

/* Copy the far text s into out (size bytes); 1 when it fitted with its
   NUL, else 0 with out cut (empty when eng_schema_fault failed it).
   Both sides are walked by pointer: Watcom C 6.5 mis-generates some
   indexed reads of far arrays. */
static int far_copy(FARP(const char) s, char *out, u16 size)
{
    u16 n;

    if (size == 0)
        return 0;
    if (eng_schema_fault != 0 && (*eng_schema_fault)() != 0) {
        *out = '\0';
        return 0;
    }
    for (n = 1; n < size && *s != '\0'; n++)
        *out++ = *s++;
    *out = '\0';
    return *s == '\0';
}

#ifdef COMPAT_WIN16
/* The resource, found and loaded at the first lock and kept as a
   handle, which stays valid while Windows discards and loads the data
   again. */
static HGLOBAL sch_res;

/* Lock the resource: its data, or NULL when it cannot be found, loaded
   or locked.  The caller unlocks it (UnlockResource) before it returns
   when it got the data. */
static FARP(const u8) sch_lock(void)
{
    HRSRC r;

    if (sch_res == NULL) {
        r = FindResource((HINSTANCE)dos_inst, MAKEINTRESOURCE(CSCH_RES),
                         MAKEINTRESOURCE(CSCH_TYPE));
        if (r == NULL)
            return NULL;
        sch_res = LoadResource((HINSTANCE)dos_inst, r);
        if (sch_res == NULL)
            return NULL;
    }
    return (FARP(const u8))LockResource(sch_res);
}

/* Element i of part k of the locked data b. */
static FARP(const char) sch_part(FARP(const u8) b, u16 k, u16 i)
{
    FARP(const u8) e;

    e = b + k * CSCH_ENTRY;
    return (FARP(const char))(b + get_le16(e) + i * get_le16(e + 2));
}
#endif

int eng_schema_field(int t, u16 f, dbf_field *out)
{
    const eng_tdef *td;
#ifdef COMPAT_WIN16
    FARP(const u8) b;
    int ok;
#else
    const char FARX *name;
#endif
    u16 row;
    int i;

    td = eng_table_def(t);
    if (td == NULL || f >= td->nfields)
        return ENG_EINTERNAL;
    row = f;
    for (i = 0; i < t; i++)
        row = (u16)(row + eng_table_def(i)->nfields);
#ifdef COMPAT_WIN16
    b = sch_lock();
    if (b == NULL)
        return ENG_EINTERNAL;
    ok = far_copy(sch_part(b, CSCH_FNAME, row), out->name,
                  (u16)sizeof out->name);
    out->type = *sch_part(b, CSCH_FTYPE, row);
    out->len = (u8)*sch_part(b, CSCH_FLEN, row);
    out->dec = (u8)*sch_part(b, CSCH_FDEC, row);
    out->offset = 0;
    UnlockResource(sch_res);
    return ok ? ENG_OK : ENG_EINTERNAL;
#else
    /* The row's name by explicit pointer arithmetic: Watcom C 6.5 reads
       csch_fname[row][k] from the wrong place when row is a variable. */
    name = &csch_fname[0][0];
    name += row * (DBF_NAMELEN + 1u);
    if (!far_copy(name, out->name, (u16)sizeof out->name))
        return ENG_EINTERNAL;
    out->type = *(csch_ftype + row);
    out->len = *(csch_flen + row);
    out->dec = *(csch_fdec + row);
    out->offset = 0;
    return ENG_OK;
#endif
}

int eng_index_expr(int x, char *out, u16 size)
{
#ifdef COMPAT_WIN16
    FARP(const u8) b;
    int ok;

    b = x >= 0 && x < ENG_NINDEX ? sch_lock() : NULL;
    if (b == NULL) {
        if (size != 0)
            out[0] = '\0';
        return ENG_EINTERNAL;
    }
    ok = far_copy(sch_part(b, (u16)(CSCH_X0 + x), 0), out, size);
    UnlockResource(sch_res);
    return ok ? ENG_OK : ENG_EINTERNAL;
#else
    const char FARX *s;

    switch (x) {
#define SCH_INDEX(x, t, expr, keylen, unique, dup) \
    case ENG_X_##x: s = csch_x_##x; break;
#include "SCHTAB.H"
#undef SCH_INDEX
    default:
        if (size != 0)
            out[0] = '\0';
        return ENG_EINTERNAL;
    }
    return far_copy(s, out, size) ? ENG_OK : ENG_EINTERNAL;
#endif
}

u16 eng_seed_rows(void)
{
    return CSCH_NSEED;
}

void eng_seed_row(u16 i, int *t, char *code, char *name, u16 *sortord)
{
    const u8 FARX *s;
#ifdef COMPAT_WIN16
    FARP(const u8) b;
#endif
    u16 n;

    if (i >= CSCH_NSEED) {
        *t = -1;
        code[0] = '\0';
        name[0] = '\0';
        *sortord = 0;
        return;
    }
    s = csch_stbl + i;
    *t = *s;
    for (n = 1; n <= i && *(s - n) == *s; n++)
        ;
    *sortord = n;
#ifdef COMPAT_WIN16
    b = sch_lock();
    if (b == NULL) {
        code[0] = '\0';
        name[0] = '\0';
        return;
    }
    far_copy(sch_part(b, CSCH_SCODE, i), code, ENG_LTYPELEN + 1);
    far_copy(sch_part(b, CSCH_SNAME, i), name, ENG_CODENAMELEN + 1);
    UnlockResource(sch_res);
#else
    far_copy(&csch_scode[0][0] + i * (ENG_LTYPELEN + 1u), code,
             ENG_LTYPELEN + 1);
    far_copy(&csch_sname[0][0] + i * (ENG_CODENAMELEN + 1u), name,
             ENG_CODENAMELEN + 1);
#endif
}

void eng_tx_copy(u16 tx, char *out)
{
    FARP(const char) s;
    FARP(const u8) len;
#ifdef COMPAT_WIN16
    FARP(const u8) b;

    b = tx >= 1 && tx <= ENG_NTX ? sch_lock() : NULL;
    if (b == NULL) {
        out[0] = '\0';
        return;
    }
    s = sch_part(b, CSCH_TX, 0);
    len = (FARP(const u8))sch_part(b, CSCH_TXLEN, 0);
#else
    if (tx < 1 || tx > ENG_NTX) {
        out[0] = '\0';
        return;
    }
    s = (const char FARX *)&csch_tx;
    len = csch_txlen;
#endif
    for (; --tx > 0; len++)
        s += *len;
    far_copy(s, out, ENG_TXMAX + 1);
#ifdef COMPAT_WIN16
    UnlockResource(sch_res);
#endif
}

#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_SEED
