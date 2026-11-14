/*
 * MAINT.C - maintenance records (MAINT.DBF, its memo file and MAINT1).
 *
 * The rules kept here are those of ENGMAINT.H.  MAINT has no index on
 * MAINT_ID: records are found by eng_id_find, a binary search over the
 * record numbers.  The rules' failures carry no text of their own: the
 * protocol sends the standard text of the code with the field name.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGNOTE.H"
#include "ENGITEM.H"
#include "ENGMAINT.H"

#define F_MAINTID   ENG_FIELD(MAINT, MAINT_ID)
#define F_ITEMID    ENG_FIELD(MAINT, ITEM_ID)
#define F_MDATE     ENG_FIELD(MAINT, MDATE)
#define F_MTYPE     ENG_FIELD(MAINT, MTYPE)
#define F_DETAIL    ENG_FIELD(MAINT, DETAIL)

#define MTYPELEN    2           /* MAINT.MTYPE                          */

/* MTYPE codes, MTYPELEN characters each: repair, cleaning, recap, test,
   modification, inspection. */
static const char mtypes[] = "RPCLRCTSMDIN";

static const char k_item[] = "ITEM";
static const char k_notes[] = "NOTES";
static const char k_detail[] = "DETAIL";

/* A failure naming the DETAIL field is reported as naming NOTES. */
static int as_notes(eng *e, int rc)
{
    if (rc != ENG_OK && strcmp(eng_errfield(e), k_detail) == 0)
        return eng_fail(e, rc, k_notes, eng_errtx(e));
    return rc;
}

/* Store the pairs in the open edit with NOTES standing for DETAIL, all
   through one eng_edit_apply, so that every key is checked (fields
   flagged ENG_F_RO refused) before any value is stored; then check the
   required fields and MTYPE. */
static int apply(eng *e, const eng_pair *p, u16 n)
{
    eng_pair q[ENG_MAXPAIRS];
    char v[MTYPELEN + 1];
    int notes;
    int detail;
    u16 i;
    int rc;

    if (n > ENG_MAXPAIRS)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_MANYPAIRS);
    notes = 0;
    detail = 0;
    for (i = 0; i < n; i++) {
        q[i].key = p[i].key;
        q[i].val = p[i].val;
        if (txt_ieq(p[i].key, k_notes)) {
            q[i].key = k_detail;
            notes = 1;
        } else if (txt_ieq(p[i].key, k_detail)) {
            detail = 1;
        }
    }
    if (notes && detail)
        return eng_fail(e, ENG_EARG, k_notes, ENG_TX_NONE);
    rc = eng_edit_apply(e, q, n, ENG_F_RO);
    if (notes)
        rc = as_notes(e, rc);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_MAINT, F_MTYPE, v, (u16)sizeof v);
    if (strlen(v) == MTYPELEN)
        for (i = 0; mtypes[i] != '\0'; i += MTYPELEN)
            if (v[0] == mtypes[i] && v[1] == mtypes[i + 1])
                return ENG_OK;
    return eng_fail(e, ENG_EARG, "MTYPE", ENG_TX_NONE);
}

int eng_maint_add(eng *e, u32 item, const eng_pair *p, u16 n, u32 *id)
{
    char today[ENG_DATELEN + 1];
    u32 nid;
    u32 r;
    int rc;

    *id = 0;
    nid = 0;
    rc = eng_item_need(e, item, k_item, &r);
    if (rc != ENG_OK)
        return rc;
    eng_today(e, today);
    rc = eng_edit_begin(e, ENG_T_MAINT, 0);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putnumf(e, F_ITEMID, (i32)item);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_MDATE, today);
    if (rc == ENG_OK)
        rc = apply(e, p, n);
    if (rc == ENG_OK)
        rc = eng_seq_next(e, ENG_T_MAINT, 1, &nid);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_MAINTID, (i32)nid);
    rc = eng_edit_finish(e, rc, nid);
    if (rc == ENG_OK)
        *id = nid;
    return rc;
}

int eng_maint_get(eng *e, u32 id, u32 *recno)
{
    int rc;

    rc = eng_id_find(e, ENG_T_MAINT, id, recno);
    if (rc != ENG_OK)
        return rc;
    if (*recno == 0)
        return eng_fail(e, ENG_ENOTFOUND, "ID", ENG_TX_NONE);
    return ENG_OK;
}

int eng_maint_set(eng *e, u32 id, const eng_pair *p, u16 n)
{
    u32 r;
    int rc;

    rc = eng_maint_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_MAINT, r);
    if (rc != ENG_OK)
        return rc;
    return eng_edit_finish(e, apply(e, p, n), id);
}

int eng_maint_del(eng *e, u32 id)
{
    u32 r;
    int rc;

    rc = eng_maint_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_MAINT, r);
    if (rc != ENG_OK)
        return rc;
    eng_edit_delete(e, 1);
    return eng_edit_finish(e, ENG_OK, id);
}

int eng_maint_notes(eng *e, u32 id, u16 *len)
{
    u32 r;
    int rc;

    *len = 0;
    rc = eng_maint_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    return as_notes(e, eng_memo_read(e, ENG_T_MAINT, F_DETAIL, len));
}

int eng_maint_addnote(eng *e, u32 id, const char *text, u8 how)
{
    u32 r;
    int rc;

    rc = eng_maint_get(e, id, &r);
    if (rc == ENG_OK)
        rc = as_notes(e, eng_edit_begin_note(e, ENG_T_MAINT, F_DETAIL, r,
                                             text, how));
    return ENG_NOTE_WRITES(how) ? eng_edit_finish(e, rc, id) : rc;
}

int eng_maint_scan(eng *e, eng_scan *s, u32 item, const char *after)
{
    char key[ENG_IDLEN + 1];
    u32 r;
    int rc;

    rc = eng_item_need(e, item, k_item, &r);
    if (rc != ENG_OK)
        return rc;
    eng_id_key(item, key);      /* a live item's ID is in range         */
    return eng_scan_open(e, s, ENG_T_MAINT, ENG_X_MAINT1, (const u8 *)key,
                         ENG_IDLEN, after);
}

int eng_maint_recent(eng *e, eng_scan *s, const char *after)
{
    return eng_scan_open(e, s, ENG_T_MAINT, ENG_SCAN_RRECORDS, NULL, 0,
                         after);
}
