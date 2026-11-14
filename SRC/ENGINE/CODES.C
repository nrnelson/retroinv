/*
 * CODES.C - the code tables: categories (CATS.DBF) and location types
 * (LTYPES.DBF).
 *
 * The rules kept here are those of ENGCODE.H.  The two tables are one
 * implementation over the table number: their fields stand at the same
 * places, and what differs (the texts, the table and field that use a
 * code) is a row of codetabs.  A table never holds more than
 * ENG_MAXCODES records, deleted ones included, so every lookup reads at
 * most that many: an ADD on a full table takes over the first deleted
 * record (eng_edit_begin_reuse) and is refused only when every record is
 * live.  Uniqueness of a code is checked before eng_edit_begin, as the
 * commit does not check it.  AUDIT.REC_ID is the record number because
 * every edit ends with eng_edit_finish with recid 0.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGEDIT.H"
#include "ENGCODE.H"

/* Fields used here: those of CATS, which LTYPES has at the same places,
   its code the wider. */
#define F_CODE      ENG_FIELD(CATS, CAT_CODE)
#define F_SORT      ENG_FIELD(CATS, SORTORD)

extern char codes_alike[1];
extern char codes_alike[(ENG_FIELD(CATS, CAT_CODE) ==
                         ENG_FIELD(LTYPES, LT_CODE) &&
                         ENG_FIELD(CATS, CAT_NAME) ==
                         ENG_FIELD(LTYPES, LT_NAME) &&
                         ENG_FIELD(CATS, SORTORD) ==
                         ENG_FIELD(LTYPES, SORTORD) &&
                         ENG_CATLEN <= ENG_LTYPELEN) ? 1 : 2];

int eng_code_norm(const char *code, u16 width, int loc, char *out)
{
    u16 i;
    char c;

    if (code == NULL)
        return 0;
    for (i = 0; code[i] != '\0'; i++) {
        if (i >= width)
            return 0;
        c = (char)TXT_UPPER(code[i]);
        if (!(TXT_ISALPHA(c) ||
              ((TXT_ISDIGIT(c) || (loc && c == '-')) && (!loc || i > 0))))
            return 0;
        out[i] = c;
    }
    out[i] = '\0';
    return i > 0;
}

/* What differs between the code tables: the texts of their failures
   (numbers that fit a byte), and the table and field whose live rows
   keep a code in use. */
typedef struct {
    u8  nosuch;
    u8  bad;
    u8  dup;
    u8  full;
    u8  used;
    u8  ut;
    u8  uf;
} codetab;

extern char codes_tx_fit[1];
extern char codes_tx_fit[(ENG_TX_NOSUCHCAT <= 0xFF && ENG_TX_BADCAT <= 0xFF &&
                          ENG_TX_CATDUP <= 0xFF && ENG_TX_CATSFULL <= 0xFF &&
                          ENG_TX_CATUSED <= 0xFF &&
                          ENG_TX_NOSUCHLT <= 0xFF && ENG_TX_BADLT <= 0xFF &&
                          ENG_TX_LTDUP <= 0xFF && ENG_TX_LTFULL <= 0xFF &&
                          ENG_TX_LTUSED <= 0xFF) ? 1 : 2];

static const codetab codetabs[2] = {
    { ENG_TX_NOSUCHCAT, ENG_TX_BADCAT, ENG_TX_CATDUP, ENG_TX_CATSFULL,
      ENG_TX_CATUSED, ENG_T_ITEMS, ENG_FIELD(ITEMS, CATEGORY) },
    { ENG_TX_NOSUCHLT, ENG_TX_BADLT, ENG_TX_LTDUP, ENG_TX_LTFULL,
      ENG_TX_LTUSED, ENG_T_LOCS, ENG_FIELD(LOCS, LTYPE) }
};

/* The row of code table t, or NULL after recording ENG_EINTERNAL. */
static const codetab *code_tab(eng *e, int t)
{
    if (t != ENG_T_CATS && t != ENG_T_LTYPES) {
        eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_BADTABLE);
        return NULL;
    }
    return &codetabs[t == ENG_T_LTYPES];
}

/* Records of table t a lookup reads: every record, bounded by
   ENG_MAXCODES. */
static u32 code_records(eng *e, int t)
{
    u32 n;

    n = eng_reccount(e, t);
    return n > ENG_MAXCODES ? (u32)ENG_MAXCODES : n;
}

/* Find the live row of table t with code key (upper-case, unpadded) and
   leave it in the buffer; *recno is 0 when there is none.  Unless
   firstdel is NULL, *firstdel receives the first deleted record read on
   the way (0 when none); when the key is not found, every record was
   read. */
static int code_find(eng *e, int t, const char *key, u32 *recno,
                     u32 *firstdel)
{
    dbf *d;
    char c[ENG_LTYPELEN + 1];
    u32 n;
    u32 r;
    int rc;

    d = eng_dbf(e, t);
    n = code_records(e, t);
    *recno = 0;
    if (firstdel != NULL)
        *firstdel = 0;
    for (r = 1; r <= n; r++) {
        rc = eng_rec_read(e, t, r);
        if (rc != ENG_OK)
            return rc;
        if (dbf_is_deleted(d)) {
            if (firstdel != NULL && *firstdel == 0)
                *firstdel = r;
            continue;
        }
        if (dbf_get_str(d, F_CODE, c, (u16)sizeof c) == DBF_OK &&
            strcmp(c, key) == 0) {
            *recno = r;
            return ENG_OK;
        }
    }
    return ENG_OK;
}

/* eng_code_get, also leaving the code as code_find's key in key of
   ENG_LTYPELEN + 1 bytes. */
static int code_get(eng *e, int t, const char *code, char *key,
                    u32 *recno)
{
    const codetab *ct;
    int rc;

    *recno = 0;
    ct = code_tab(e, t);
    if (ct == NULL)
        return ENG_EINTERNAL;
    if (!eng_code_norm(code, eng_field(e, t, F_CODE)->len, 0, key))
        return eng_fail(e, ENG_ENOTFOUND, NULL, ct->nosuch);
    rc = code_find(e, t, key, recno, NULL);
    if (rc == ENG_OK && *recno == 0)
        rc = eng_fail(e, ENG_ENOTFOUND, NULL, ct->nosuch);
    return rc;
}

int eng_code_get(eng *e, int t, const char *code, u32 *recno)
{
    char key[ENG_LTYPELEN + 1];

    return code_get(e, t, code, key, recno);
}

int eng_code_list(eng *e, int t, u32 *recnos, u16 *n)
{
    dbf *d;
    i32 ord[ENG_MAXCODES];
    char code[ENG_MAXCODES][ENG_LTYPELEN + 1];
    char ck[ENG_LTYPELEN + 1];
    u32 cnt;
    u32 r;
    i32 sord;
    int blank;
    u16 i;
    u16 j;
    int rc;

    *n = 0;
    if (code_tab(e, t) == NULL)
        return ENG_EINTERNAL;
    d = eng_dbf(e, t);
    cnt = code_records(e, t);
    for (r = 1; r <= cnt; r++) {
        rc = eng_rec_read(e, t, r);
        if (rc != ENG_OK)
            return rc;
        if (dbf_is_deleted(d))
            continue;
        if (dbf_get_num(d, F_SORT, &sord, &blank) != DBF_OK)
            sord = 0;
        if (dbf_get_str(d, F_CODE, ck, (u16)sizeof ck) != DBF_OK)
            ck[0] = '\0';
        /* Insertion by SORTORD, then code. */
        for (j = *n; j > 0; j--) {
            i = (u16)(j - 1);
            if (ord[i] < sord ||
                (ord[i] == sord && strcmp(code[i], ck) <= 0))
                break;
            ord[j] = ord[i];
            strcpy(code[j], code[i]);
            recnos[j] = recnos[i];
        }
        ord[j] = sord;
        strcpy(code[j], ck);
        recnos[j] = r;
        (*n)++;
    }
    return ENG_OK;
}

/* SORTORD of the edited record of table t must not be blank: a row
   always has a place in its list. */
static int sortord_ok(eng *e, int t)
{
    i32 v;
    int blank;

    if (dbf_get_num(eng_dbf(e, t), F_SORT, &v, &blank) != DBF_OK || blank)
        return eng_fail(e, ENG_EARG, "SORTORD", ENG_TX_NOSORT);
    return ENG_OK;
}

int eng_code_add(eng *e, int t, const char *code, const eng_pair *p,
                 u16 n)
{
    const codetab *ct;
    char key[ENG_LTYPELEN + 1];
    u32 r;
    u32 reuse;
    int rc;

    ct = code_tab(e, t);
    if (ct == NULL)
        return ENG_EINTERNAL;
    if (!eng_code_norm(code, eng_field(e, t, F_CODE)->len, 0, key))
        return eng_fail(e, ENG_EARG, eng_field_name(e, t, F_CODE), ct->bad);
    rc = code_find(e, t, key, &r, &reuse);
    if (rc != ENG_OK)
        return rc;
    if (r != 0)
        return eng_fail(e, ENG_EDUP, eng_field_name(e, t, F_CODE), ct->dup);
    if (eng_reccount(e, t) >= ENG_MAXCODES) {
        if (reuse == 0)
            return eng_fail(e, ENG_ECONSTR, NULL, ct->full);
        rc = eng_edit_begin_reuse(e, t, reuse);
    } else {
        rc = eng_edit_begin(e, t, 0);
    }
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putnumf(e, F_SORT, ENG_CODESORTDEF);
    if (rc == ENG_OK)
        rc = eng_edit_apply(e, p, n, ENG_F_RO);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_CODE, key);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    if (rc == ENG_OK)
        rc = sortord_ok(e, t);
    return eng_edit_finish(e, rc, 0);
}

int eng_code_set(eng *e, int t, const char *code, const eng_pair *p,
                 u16 n)
{
    u32 r;
    int rc;

    rc = eng_code_get(e, t, code, &r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, t, r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_apply(e, p, n, ENG_F_RO);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    if (rc == ENG_OK)
        rc = sortord_ok(e, t);
    return eng_edit_finish(e, rc, 0);
}

int eng_code_del(eng *e, int t, const char *code)
{
    const codetab *ct;
    dbf *u;
    char key[ENG_LTYPELEN + 1];
    char c[ENG_LTYPELEN + 1];
    u32 r;
    u32 i;
    u32 cnt;
    int rc;

    rc = code_get(e, t, code, key, &r);
    if (rc != ENG_OK)
        return rc;
    ct = &codetabs[t == ENG_T_LTYPES];
    u = eng_dbf(e, ct->ut);
    cnt = eng_reccount(e, ct->ut);
    for (i = 1; i <= cnt; i++) {
        rc = eng_rec_read(e, ct->ut, i);
        if (rc != ENG_OK)
            return rc;
        if (dbf_is_deleted(u))
            continue;
        if (dbf_get_str(u, ct->uf, c, (u16)sizeof c) == DBF_OK &&
            strcmp(c, key) == 0)
            return eng_fail(e, ENG_ECONSTR, NULL, ct->used);
    }
    rc = eng_edit_begin(e, t, r);
    if (rc != ENG_OK)
        return rc;
    eng_edit_delete(e, 1);
    return eng_edit_finish(e, ENG_OK, 0);
}
