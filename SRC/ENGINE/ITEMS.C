/*
 * ITEMS.C - items (ITEMS.DBF, its memo file and indexes ITEMS1..ITEMS5).
 *
 * The rules kept on every change are those of ENGITEM.H.  An undeleted
 * item with a parent takes the parent's current location because the
 * parent may have moved while the item was deleted.
 *
 * Between eng_edit_begin and the commit the ITEMS buffer belongs to the
 * edit, so every lookup in ITEMS (the item's old values, its children,
 * its parent) is made before eng_edit_begin; the checks made during the
 * edit look up other tables only (CATS, LOCS) or read the edited
 * buffer's fields.
 *
 * Tags (TAGS) are words of 1 to ENG_ITAGLEN characters of A-Z a-z 0-9
 * and - _ . / +, separated by commas or spaces in any run; every commit
 * stores them in the order given, joined by single commas, a repeat
 * (A-Z folded) left out, so the stored text is never longer than the
 * given one.  Every comparison of tags folds A-Z, and a word another
 * program left that is no tag is matched as it is (TAG=) or skipped
 * (ITEM TAGS).
 *
 * ITEM SPLIT changes two records, so it is a batch, ordered so that the
 * new item's I row, the U row of the item split and then the new item
 * reach the disk before the item split is rewritten; eng_item_heal finds
 * a split cut off between the two by those rows.
 *
 * Field numbers are the schema's constants (ENG_FIELD); the keys built
 * here rely on the schema's widths, which every open table has
 * (eng_open_files refuses any other layout).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENGFLD_ITEMS
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGNOTE.H"
#include "ENGCODE.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
#include "ENGLOAN.H"

/* Fields used here. */
#define F_ITEMID    ENG_FIELD(ITEMS, ITEM_ID)
#define F_CATEGORY  ENG_FIELD(ITEMS, CATEGORY)
#define F_ITEMTYPE  ENG_FIELD(ITEMS, ITEMTYPE)
#define F_MFR       ENG_FIELD(ITEMS, MFR)
#define F_MODEL     ENG_FIELD(ITEMS, MODEL)
#define F_SERIALNO  ENG_FIELD(ITEMS, SERIALNO)
#define F_DESCR     ENG_FIELD(ITEMS, DESCR)
#define F_COND      ENG_FIELD(ITEMS, COND)
#define F_WORKS     ENG_FIELD(ITEMS, WORKS)
#define F_QTY       ENG_FIELD(ITEMS, QTY)
#define F_ONLOAN    ENG_FIELD(ITEMS, ON_LOAN)
#define F_INUSE     ENG_FIELD(ITEMS, IN_USE)
#define F_ATTACH    ENG_FIELD(ITEMS, ATTACHED)
#define F_LOCID     ENG_FIELD(ITEMS, LOC_ID)
#define F_PARENT    ENG_FIELD(ITEMS, PARENT_ID)
#define F_TAGS      ENG_FIELD(ITEMS, TAGS)
#define F_STATUS    ENG_FIELD(ITEMS, STATUS)
#define F_NOTES     ENG_FIELD(ITEMS, NOTES)
#define F_CREATED   ENG_FIELD(ITEMS, CREATED)
#define F_UPDATED   ENG_FIELD(ITEMS, UPDATED)

#define F_ATBL      ENG_FIELD(AUDIT, TBL)
#define F_AUSER     ENG_FIELD(AUDIT, USERNAME)
#define F_ARECID    ENG_FIELD(AUDIT, REC_ID)
#define F_AACTION   ENG_FIELD(AUDIT, ACTION)
#define F_AFLD      ENG_FIELD(AUDIT, FLD)
#define F_AOLD      ENG_FIELD(AUDIT, OLDVAL)
#define F_ANEW      ENG_FIELD(AUDIT, NEWVAL)

#define CONDLEN     ENG_FW_ITEMS_COND   /* each code of conds           */
#define FINDLEN     ENG_FW_ITEMS_DESCR  /* widest field ITEM FIND reads */
#define TAGSLEN     ENG_FW_ITEMS_TAGS   /* a stored tag list            */
#define TAGREC      (TAGSLEN + 1)       /* bytes of one record's tags in
                                           the set of ITEM TAGS, at most */

/* eng_scan.f.flags beside the ENG_IF_ switches: floc filters. */
#define IF_LOC          0x80

#define KIDS_NONE       0
#define KIDS_LIVE       1
#define KIDS_UNSETTLED  2

/* validate reads CATEGORY and STATUS into one buffer of ENG_CATLEN + 1
   bytes, and code_put COND and WORKS into one of CONDLEN + 1; STATUS
   and WORKS hold one character, which validate, eng_item_keep and
   works_filter test.  eng_item_keep reads WORKS, TAGS and the fields of
   findf into one buffer of FINDLEN + 1, and eng_item_heal the audit
   values it compares into one of ENG_AUDVAL + 1; CATEGORY and STATUS
   there, and TBL, FLD and USERNAME in eng_item_heal, are tested with
   eng_rec_is, which takes fields up to ENG_MAXFIND.  The set of ITEM
   TAGS, in eng_memobuf, takes at least one record's tags. */
extern char items_codes_fit[1];
extern char items_codes_fit[(ENG_FW_ITEMS_WORKS == 1 &&
                             ENG_STATLEN == 1 &&
                             TAGREC <= ENG_TAGSET &&
                             ENG_TAGSET <= ENG_MAXMEMO) ? 1 : 2];
extern char items_find_fits[1];
extern char items_find_fits[(ENG_CATLEN <= FINDLEN &&
                             ENG_FW_ITEMS_MFR <= FINDLEN &&
                             ENG_FW_ITEMS_MODEL <= FINDLEN &&
                             ENG_FW_ITEMS_TAGS <= FINDLEN &&
                             ENG_FW_ITEMS_SERIALNO <= FINDLEN &&
                             ENG_FW_ITEMS_ITEMTYPE <= FINDLEN) ? 1 : 2];
/* The counters of LOANS.C follow one another from ON_LOAN, one per kind
   of row (L R U) and then ATTACHED, and their reasons fit hold_tx. */
extern char items_counters_ok[1];
extern char items_counters_ok[(ENG_FIELD(ITEMS, RESERVED) == F_ONLOAN + 1 &&
                               F_INUSE == F_ONLOAN + 2 &&
                               F_ATTACH == F_ONLOAN + 3 &&
                               ENG_TX_ONLOAN <= 0xFF &&
                               ENG_TX_HELDRES <= 0xFF &&
                               ENG_TX_HELDUSE <= 0xFF &&
                               ENG_TX_ATTACHED <= 0xFF) ? 1 : 2];
extern char items_tested_fit[1];
extern char items_tested_fit[(ENG_CATLEN <= ENG_MAXFIND &&
                              ENG_STATLEN <= ENG_MAXFIND &&
                              ENG_TBLNAMELEN <= ENG_MAXFIND &&
                              ENG_USERLEN <= ENG_MAXFIND &&
                              DBF_NAMELEN <= ENG_MAXFIND) ? 1 : 2];

static const char k_category[] = "CATEGORY";
static const char k_locid[] = "LOC_ID";
static const char k_status[] = "STATUS";
static const char k_qty[] = "QTY";
static const char k_works[] = "WORKS";
static const char k_tag[] = "TAG";

/* The reason a counter above 0 gives, from ON_LOAN to ATTACHED. */
static const u8 hold_tx[] = {
    ENG_TX_ONLOAN, ENG_TX_HELDRES, ENG_TX_HELDUSE, ENG_TX_ATTACHED
};

/* COND codes, CONDLEN characters each, padded with spaces; the blank
   one is "not graded". */
static const char conds[] = "NEWEXCGD FR PR    ";

/* The WORKS values, the last one a new item's: works, intermittently,
   does not work, not tested.  A WORKS= filter holds each at most once. */
static const char worksv[] = "YIN?";
#define K_NEWWORKS  (worksv + sizeof worksv - 2)
extern char items_works_fit[1];
extern char items_works_fit[(sizeof worksv - 1 == ENG_NWORKS) ? 1 : 2];

/* A new item's STATUS (active, not disposed), which is also the STATUS
   of an available item (ENG_IF_AVAIL). */
static const char k_newstat[] = "A";

/* The characters of a tag besides A-Z, a-z and 0-9. */
static const char tagpunct[] = "-_./+";

/* Fields ITEM FIND searches. */
static const u8 findf[] = {
    F_MFR, F_MODEL, F_DESCR, F_TAGS, F_SERIALNO, F_ITEMTYPE
};

/* Put 0 into every counter of the edited record. */
static int zero_counts(eng *e)
{
    u16 f;
    int rc;

    rc = ENG_OK;
    for (f = F_ONLOAN; f <= F_ATTACH && rc == ENG_OK; f++)
        rc = eng_edit_putnumf(e, f, 0);
    return rc;
}

/* Stamp UPDATED of the edited record with today. */
static int stamp(eng *e)
{
    char today[ENG_DATELEN + 1];

    eng_today(e, today);
    return eng_edit_putf(e, F_UPDATED, today);
}

/* Children of item id: *kids is KIDS_LIVE when a live one was found,
   KIDS_UNSETTLED when ENG_MAXREADS entries of deleted ones were read
   and more remain, else KIDS_NONE.  Moves the ITEMS buffer. */
static int kids_of(eng *e, u32 id, int *kids)
{
    char key[ENG_IDLEN + 1];

    *kids = KIDS_NONE;
    if (!eng_id_key(id, key))
        return ENG_OK;
    return eng_scan_any(e, ENG_T_ITEMS, ENG_X_ITEMS3, (const u8 *)key,
                        ENG_IDLEN, kids);
}

/* The length of the next word of the tag list at *s, 0 at its end;
   *s is left at the word, past the commas and spaces before it. */
static u16 tag_word(const char **s)
{
    const char *p;

    p = *s;
    while (*p == ',' || *p == ' ')
        p++;
    *s = p;
    while (*p != '\0' && *p != ',' && *p != ' ')
        p++;
    return (u16)(p - *s);
}

/* 1 when the n characters at w are a tag. */
static int tag_ok(const char *w, u16 n)
{
    int c;

    if (n > ENG_ITAGLEN)
        return 0;
    for (; n > 0; n--) {
        c = *w++;
        if (!TXT_ISALPHA(c) && !TXT_ISDIGIT(c) && strchr(tagpunct, c) == NULL)
            return 0;
    }
    return 1;
}

/* 1 when the tag list list holds the word of n characters at w, A-Z
   folded. */
static int tags_has(const char *list, const char *w, u16 n)
{
    u16 k;

    for (; (k = tag_word(&list)) != 0; list += k)
        if (k == n && txt_ieqn(list, w, n))
            return 1;
    return 0;
}

/* The length of the tag list list (len characters) with the word of n
   characters at w added after a comma, unless list holds it. */
static u16 tag_add(char *list, u16 len, const char *w, u16 n)
{
    if (tags_has(list, w, n))
        return len;
    if (len > 0)
        list[len++] = ',';
    memcpy(list + len, w, n);
    len = (u16)(len + n);
    list[len] = '\0';
    return len;
}

/* The tag list in, normalized as a commit stores TAGS, into out, which
   holds strlen(in) + 1 bytes; 0 when a word of in is no tag. */
static int tags_norm(const char *in, char *out)
{
    u16 len;
    u16 n;

    len = 0;
    out[0] = '\0';
    for (; (n = tag_word(&in)) != 0; in += n) {
        if (!tag_ok(in, n))
            return 0;
        len = tag_add(out, len, in, n);
    }
    return 1;
}

/* Store field f of the edited buffer upper-cased (ENG_F_UPPER), so that
   a lower-case code another program left is folded and audited by the
   commit, when, padded with spaces to w characters, it is one of codes
   (w characters each); else ENG_EARG naming name with the text tx. */
static int code_put(eng *e, u16 f, const char *codes, u16 w,
                    const char *name, u16 tx)
{
    char v[CONDLEN + 1];
    u16 i;

    eng_rec_str(e, ENG_T_ITEMS, f, v, (u16)sizeof v);
    eng_upper(v);
    for (i = (u16)strlen(v); i < w; i++)
        v[i] = ' ';
    v[w] = '\0';
    for (i = 0; codes[i] != '\0'; i += w)
        if (memcmp(v, codes + i, w) == 0)
            return eng_edit_putf(e, f, v);
    return eng_fail(e, ENG_EARG, name, tx);
}

/* Check the edited buffer against the field rules of ENGITEM.H and store
   its codes upper-cased and its tags normalized; the location was
   checked with its pair (eng_loc_pairs). */
static int validate(eng *e)
{
    char v[ENG_CATLEN + 1];
    char t[TAGSLEN + 1];
    char nt[TAGSLEN + 1];
    u32 r;
    int rc;

    eng_rec_str(e, ENG_T_ITEMS, F_CATEGORY, v, (u16)sizeof v);
    rc = eng_code_get(e, ENG_T_CATS, v, &r);
    if (rc == ENG_ENOTFOUND)
        return eng_fail(e, ENG_ECONSTR, k_category, ENG_TX_NOCAT);
    if (rc != ENG_OK)
        return rc;
    rc = code_put(e, F_COND, conds, CONDLEN, "COND", ENG_TX_BADCOND);
    if (rc == ENG_OK)
        rc = code_put(e, F_WORKS, worksv, ENG_FW_ITEMS_WORKS, k_works,
                      ENG_TX_WORKS);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_ITEMS, F_TAGS, t, (u16)sizeof t);
    if (!tags_norm(t, nt))
        return eng_fail(e, ENG_EARG, "TAGS", ENG_TX_BADTAG);
    rc = eng_edit_putf(e, F_TAGS, nt);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_ITEMS, F_STATUS, v, (u16)sizeof v);
    if (v[0] != 'A' && v[0] != 'D')
        return eng_fail(e, ENG_EARG, k_status, ENG_TX_STATUS);
    if (eng_rec_num(e, ENG_T_ITEMS, F_QTY) < eng_item_held(e))
        return eng_fail(e, ENG_ECONSTR, k_qty, ENG_TX_QTYLOAN);
    return ENG_OK;
}

/* Reserve the new item's ID, store it and commit. */
static int commit_new(eng *e, int rc, u32 *id)
{
    u32 nid;

    *id = 0;
    nid = 0;
    if (rc == ENG_OK)
        rc = eng_seq_next(e, ENG_T_ITEMS, 1, &nid);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_ITEMID, (i32)nid);
    rc = eng_edit_finish(e, rc, nid);
    if (rc == ENG_OK)
        *id = nid;
    return rc;
}

/* The new item's record from the defaults and the pairs p, its
   location by eng_loc_pairs, checked by eng_edit_required and validate.
   The edit is open on return when eng_edit_begin was reached and
   succeeded; eng_edit_cancel ends it either way. */
static int build_new(eng *e, const eng_pair *p, u16 n)
{
    eng_pair q[ENG_MAXPAIRS];
    char loc[TXT_U32LEN + 1];
    char today[ENG_DATELEN + 1];
    u32 id;
    int rc;

    eng_today(e, today);
    id = 0;
    rc = eng_loc_pairs(e, p, n, k_locid, q, loc, &id);
    if (rc == ENG_OK)
        rc = eng_edit_begin(e, ENG_T_ITEMS, 0);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putnumf(e, F_QTY, 1);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_WORKS, K_NEWWORKS);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_STATUS, k_newstat);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_PARENT, 0);
    if (rc == ENG_OK)
        rc = zero_counts(e);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_LOCID, 0);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_CREATED, today);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_UPDATED, today);
    if (rc == ENG_OK)
        rc = eng_edit_apply(e, q, n, ENG_F_RO);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    if (rc == ENG_OK)
        rc = validate(e);
    return rc;
}

int eng_item_add(eng *e, const eng_pair *p, u16 n, u32 *id)
{
    return commit_new(e, build_new(e, p, n), id);
}

int eng_item_check(eng *e, const eng_pair *p, u16 n)
{
    int rc;

    rc = build_new(e, p, n);
    eng_edit_cancel(e);
    return rc;
}

int eng_item_seek(eng *e, u32 id, u32 *recno)
{
    char key[ENG_IDLEN + 1];

    *recno = 0;
    if (!eng_id_key(id, key))
        return ENG_OK;
    return eng_seek_live(e, ENG_X_ITEMS1, key, recno);
}

int eng_item_need(eng *e, u32 id, const char *field, u32 *recno)
{
    int rc;

    rc = eng_item_seek(e, id, recno);
    if (rc == ENG_OK && *recno == 0)
        rc = eng_fail(e, ENG_ENOTFOUND, field, ENG_TX_NOITEM);
    return rc;
}

int eng_item_get(eng *e, u32 id, u32 *recno)
{
    return eng_item_need(e, id, NULL, recno);
}

int eng_item_set(eng *e, u32 id, const eng_pair *p, u16 n)
{
    eng_pair q[ENG_MAXPAIRS];
    char loc[TXT_U32LEN + 1];
    char oldst[ENG_STATLEN + 1];
    char newst[ENG_STATLEN + 1];
    u32 r;
    u32 oldloc;
    u32 newloc;
    i32 parent;
    u16 tx;
    u16 f;
    int kids;
    int rc;

    rc = eng_item_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    oldloc = (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID);
    eng_rec_str(e, ENG_T_ITEMS, F_STATUS, oldst, (u16)sizeof oldst);
    parent = eng_rec_num(e, ENG_T_ITEMS, F_PARENT);
    newloc = oldloc;
    rc = eng_loc_pairs(e, p, n, k_locid, q, loc, &newloc);
    kids = KIDS_NONE;
    if (rc == ENG_OK && newloc != oldloc && parent == 0)
        rc = kids_of(e, id, &kids);
    if (rc == ENG_OK && newloc != oldloc && kids == KIDS_UNSETTLED)
        rc = eng_fail(e, ENG_ECONSTR, k_locid, ENG_TX_KIDSMANY);
    else if (rc == ENG_OK && newloc != oldloc &&
             (parent != 0 || kids == KIDS_LIVE))
        rc = eng_fail(e, ENG_ECONSTR, k_locid, ENG_TX_ASSYMOVE);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_apply(e, q, n, ENG_F_RO);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    if (rc == ENG_OK)
        rc = validate(e);
    if (rc == ENG_OK && oldst[0] != 'D') {
        eng_rec_str(e, ENG_T_ITEMS, F_STATUS, newst, (u16)sizeof newst);
        tx = newst[0] == 'D' ? eng_item_hold(e, &f) : ENG_TX_NONE;
        if (tx == ENG_TX_NONE && newst[0] == 'D' && parent != 0)
            tx = ENG_TX_INST;
        if (tx != ENG_TX_NONE)
            rc = eng_fail(e, ENG_ECONSTR, k_status, tx);
    }
    if (rc == ENG_OK)
        rc = stamp(e);
    return eng_edit_finish(e, rc, id);
}

int eng_item_del(eng *e, u32 id)
{
    u32 r;
    u16 tx;
    u16 f;
    int kids;
    int rc;

    rc = eng_item_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    tx = eng_item_hold(e, &f);
    if (tx != ENG_TX_NONE)
        return eng_fail(e, ENG_ECONSTR, eng_field_name(e, ENG_T_ITEMS, f),
                        tx);
    rc = kids_of(e, id, &kids);
    if (rc != ENG_OK)
        return rc;
    if (kids == KIDS_LIVE)
        return eng_fail(e, ENG_ECONSTR, NULL, ENG_TX_HASKIDS);
    if (kids == KIDS_UNSETTLED)
        return eng_fail(e, ENG_ECONSTR, NULL, ENG_TX_KIDSMANY);
    rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc != ENG_OK)
        return rc;
    eng_edit_delete(e, 1);
    return eng_edit_finish(e, stamp(e), id);
}

/* The deleted record of item id into *recno, left in the ITEMS buffer;
   ENG_ECONSTR if the item is live, ENG_ENOTFOUND if it does not exist. */
static int find_deleted(eng *e, u32 id, u32 *recno)
{
    ndx *x;
    char key[ENG_IDLEN + 1];
    u8 k[ENG_IDLEN];
    u32 r;
    int exact;
    int rc;

    x = eng_ndx(e, ENG_X_ITEMS1);
    *recno = 0;
    if (!eng_id_key(id, key))
        return eng_fail(e, ENG_ENOTFOUND, NULL, ENG_TX_NOITEM);
    rc = ndx_seek(x, (const u8 *)key, ENG_IDLEN, &r, &exact);
    if (rc != DBF_OK)
        return eng_dbferr(e, rc, 1);
    /* IDs are never reused, so the run of equal keys is one entry long
       unless the table was edited by another program.  The last record
       read is *recno. */
    while (r != 0 && exact) {
        rc = ndx_key(x, k);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 1);
        if (memcmp(k, key, ENG_IDLEN) != 0)
            break;
        rc = eng_rec_read(e, ENG_T_ITEMS, r);
        if (rc != ENG_OK)
            return rc;
        if (!dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS)))
            return eng_fail(e, ENG_ECONSTR, NULL, ENG_TX_NOTDELD);
        *recno = r;
        rc = ndx_next(x, &r);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 1);
    }
    if (*recno == 0)
        return eng_fail(e, ENG_ENOTFOUND, NULL, ENG_TX_NOITEM);
    return ENG_OK;
}

int eng_item_undel(eng *e, u32 id)
{
    char cat[ENG_CATLEN + 1];
    u32 r;
    u32 found;
    u32 loc;
    u32 ploc;
    i32 parent;
    int rc;

    rc = find_deleted(e, id, &found);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_ITEMS, F_CATEGORY, cat, (u16)sizeof cat);
    loc = (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID);
    ploc = loc;
    parent = eng_rec_num(e, ENG_T_ITEMS, F_PARENT);
    rc = eng_code_get(e, ENG_T_CATS, cat, &r);
    if (rc == ENG_ENOTFOUND)
        return eng_fail(e, ENG_ECONSTR, k_category, ENG_TX_CATDEAD);
    if (rc != ENG_OK)
        return rc;
    if (parent != 0) {
        rc = eng_item_get(e, (u32)parent, &r);
        if (rc == ENG_ENOTFOUND)
            return eng_fail(e, ENG_ECONSTR, "PARENT_ID", ENG_TX_PARDEAD);
        if (rc != ENG_OK)
            return rc;
        ploc = (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID);
    } else if (loc != 0) {
        rc = eng_loc_seek(e, loc, &r);
        if (rc == ENG_OK && r == 0)
            rc = eng_fail(e, ENG_ECONSTR, k_locid, ENG_TX_LOCDEAD);
        if (rc != ENG_OK)
            return rc;
    }
    rc = eng_edit_begin(e, ENG_T_ITEMS, found);
    if (rc != ENG_OK)
        return rc;
    eng_edit_delete(e, 0);
    rc = ENG_OK;
    if (ploc != loc)
        rc = eng_edit_putnumf(e, F_LOCID, (i32)ploc);
    if (rc == ENG_OK)
        rc = stamp(e);
    return eng_edit_finish(e, rc, id);
}

/* Start the copy of the item in ITEMS record r as a new top-level item
   (ITEM COPY, ITEM SPLIT): its fields and notes, SERIALNO blank,
   counters 0, CREATED and UPDATED today.  The edit is open on return
   when eng_edit_begin_copy succeeded; commit_new or eng_edit_cancel
   ends it either way. */
static int begin_copy(eng *e, u32 r)
{
    char today[ENG_DATELEN + 1];
    int rc;

    eng_today(e, today);
    rc = eng_edit_begin_copy(e, ENG_T_ITEMS, r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putf(e, F_SERIALNO, "");
    if (rc == ENG_OK)
        rc = zero_counts(e);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_PARENT, 0);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_CREATED, today);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_UPDATED, today);
    return rc;
}

int eng_item_copy(eng *e, u32 id, u32 *newid)
{
    u32 r;
    int rc;

    rc = eng_item_get(e, id, &r);
    if (rc == ENG_OK)
        rc = begin_copy(e, r);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_COND, "");
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_WORKS, K_NEWWORKS);
    return commit_new(e, rc, newid);
}

int eng_item_split(eng *e, u32 id, const char *qty, u32 *newid)
{
    char was[ENG_FW_ITEMS_QTY + 1];
    char now[TXT_U32LEN + 1];
    eng_audrow row;
    u32 r;
    u32 nid;
    i32 left;                   /* id's QTY, then the units it keeps    */
    i32 keep;
    i32 n;
    int rc;

    *newid = 0;
    rc = eng_item_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    left = eng_rec_num(e, ENG_T_ITEMS, F_QTY);
    keep = eng_item_held(e);
    if (keep < 1)
        keep = 1;
    eng_rec_str(e, ENG_T_ITEMS, F_QTY, was, (u16)sizeof was);
    rc = begin_copy(e, r);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_QTY, qty != NULL ? qty : "");
    n = eng_rec_num(e, ENG_T_ITEMS, F_QTY);
    left -= n;
    if (rc == ENG_OK && n < 1)
        rc = eng_fail(e, ENG_EARG, k_qty, ENG_TX_NONE);
    if (rc == ENG_OK && left < keep)
        rc = eng_fail(e, ENG_ECONSTR, k_qty, ENG_TX_NOSTOCK);
    if (rc == ENG_OK)
        rc = eng_batch_begin(e, ENG_T_ITEMS);
    if (rc != ENG_OK) {
        eng_edit_cancel(e);
        return rc;
    }

    /* The new item's I row, as its commit would write it, then the U row
       id's commit would write, then the new item, committed quietly and
       flushed with them: once it is counted on disk, the rows that
       complete the split are there, the U row right after the I row. */
    rc = eng_seq_next(e, ENG_T_ITEMS, 1, &nid);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_ITEMID, (i32)nid);
    row.action = ENG_A_INSERT;
    row.fld = eng_field_name(e, ENG_T_ITEMS, F_ITEMID);
    row.oldval = NULL;
    row.newval = now;
    txt_u32(now, nid);
    if (rc == ENG_OK)
        rc = eng_audit(e, ENG_T_ITEMS, nid, &row);
    row.action = ENG_A_UPDATE;
    row.fld = k_qty;
    row.oldval = was;
    txt_u32(now, (u32)left);
    if (rc == ENG_OK)
        rc = eng_audit(e, ENG_T_ITEMS, id, &row);
    eng_edit_quiet(e);
    rc = eng_edit_finish(e, rc, nid);
    if (rc == ENG_OK)
        rc = eng_flush(e);
    if (rc == ENG_OK)
        rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc == ENG_OK) {
        eng_edit_quiet(e);
        rc = eng_edit_putnumf(e, F_QTY, left);
        if (rc == ENG_OK)
            rc = stamp(e);
        rc = eng_edit_finish(e, rc, id);
    }
    rc = eng_batch_end(e, rc);
    if (rc == ENG_OK)
        *newid = nid;
    return rc;
}

int eng_item_notes(eng *e, u32 id, u16 *len)
{
    u32 r;
    int rc;

    *len = 0;
    rc = eng_item_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    return eng_memo_read(e, ENG_T_ITEMS, F_NOTES, len);
}

int eng_item_addnote(eng *e, u32 id, const char *text, u8 how)
{
    u32 r;
    int rc;

    rc = eng_item_get(e, id, &r);
    if (rc == ENG_OK)
        rc = eng_edit_begin_note(e, ENG_T_ITEMS, F_NOTES, r, text, how);
    if (rc != ENG_OK || !ENG_NOTE_WRITES(how))
        return rc;
    return eng_edit_finish(e, stamp(e), id);
}

i32 eng_item_held(eng *e)
{
    i32 n;
    u16 f;

    n = 0;
    for (f = F_ONLOAN; f <= F_INUSE; f++)
        n += eng_rec_num(e, ENG_T_ITEMS, f);
    return n;
}

char eng_item_avail(eng *e)
{
    static const char held[] = { ENG_K_LOAN, ENG_K_RESERVE, ENG_K_USE };
    u16 f;

    if (!eng_rec_is(e, ENG_T_ITEMS, F_STATUS, k_newstat))
        return ENG_AV_GONE;
    if (eng_rec_num(e, ENG_T_ITEMS, F_PARENT) != 0)
        return ENG_AV_PART;
    if (eng_rec_num(e, ENG_T_ITEMS, F_QTY) > eng_item_held(e))
        return ENG_AV_YES;
    for (f = F_ONLOAN; f <= F_INUSE; f++)
        if (eng_rec_num(e, ENG_T_ITEMS, f) > 0)
            return held[f - F_ONLOAN];
    return ENG_AV_NONE;
}

u16 eng_item_hold(eng *e, u16 *f)
{
    for (*f = F_ONLOAN; *f <= F_ATTACH; (*f)++)
        if (eng_rec_num(e, ENG_T_ITEMS, *f) > 0)
            return hold_tx[*f - F_ONLOAN];
    return ENG_TX_NONE;
}

int eng_item_keep(eng *e, const eng_ifilt *f)
{
    char v[FINDLEN + 1];
    const char *t;
    u16 n;
    u16 i;
    int c;

    if (!eng_rec_is(e, ENG_T_ITEMS, F_CATEGORY, f->cat) ||
        !eng_rec_is(e, ENG_T_ITEMS, F_STATUS, f->stat))
        return 0;
    if (f->works[0] != '\0') {
        eng_rec_str(e, ENG_T_ITEMS, F_WORKS, v, (u16)sizeof v);
        c = TXT_UPPER(v[0]);
        if (c == '\0' || strchr(f->works, c) == NULL)
            return 0;
    }
    if ((f->flags & ENG_IF_TOP) && eng_rec_num(e, ENG_T_ITEMS, F_PARENT) != 0)
        return 0;
    if ((f->flags & (ENG_IF_AVAIL | ENG_IF_UNAVAIL)) &&
        (eng_item_avail(e) == ENG_AV_YES) != ((f->flags & ENG_IF_AVAIL) != 0))
        return 0;
    if (f->tag[0] != '\0') {
        eng_rec_str(e, ENG_T_ITEMS, F_TAGS, v, (u16)sizeof v);
        for (t = f->tag; (n = tag_word(&t)) != 0; t += n)
            if (!tags_has(v, t, n))
                return 0;
    }
    if (f->text[0] == '\0')
        return 1;
    for (i = 0; i < sizeof findf / sizeof findf[0]; i++) {
        eng_rec_str(e, ENG_T_ITEMS, findf[i], v, (u16)sizeof v);
        eng_upper(v);
        if (strstr(v, f->text) != NULL)
            return 1;
    }
    return 0;
}

/* 1 when the loaded item passes every filter of s that is set: those of
   eng_item_keep and the location of floc. */
static int item_match(eng *e, eng_scan *s)
{
    return eng_item_keep(e, &s->f) &&
           (!(s->f.flags & IF_LOC) ||
            (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID) == s->floc);
}

/* The WORKS= filter val into out (ENG_NWORKS + 1 bytes), upper-cased;
   NULL leaves out empty.  Each letter is one of worksv and comes once,
   so no more than ENG_NWORKS are stored. */
static int works_filter(eng *e, const char *val, char *out)
{
    u16 i;
    int c;

    out[0] = '\0';
    for (i = 0; val != NULL && val[i] != '\0'; i++) {
        c = val[i];
        c = TXT_UPPER(c);
        if (strchr(worksv, c) == NULL || strchr(out, c) != NULL)
            return eng_fail(e, ENG_EARG, k_works, ENG_TX_WORKS);
        out[i] = (char)c;
        out[i + 1] = '\0';
    }
    return ENG_OK;
}

int eng_item_filters(eng *e, eng_ifilt *f, const char *text,
                     const char *cat, const char *status, const char *tag,
                     const char *works, int flags)
{
    int rc;

    f->text[0] = '\0';
    f->tag[0] = '\0';
    f->flags = (u8)flags;
    if (text != NULL) {
        if (text[0] == '\0' || strlen(text) > ENG_MAXFIND)
            return eng_fail(e, ENG_EARG, "TEXT", ENG_TX_BADTEXT);
        strcpy(f->text, text);
        eng_upper(f->text);
    }
    rc = eng_filter_len(e, cat, ENG_CATLEN, "CAT");
    if (rc == ENG_OK)
        rc = eng_filter_len(e, status, ENG_STATLEN, k_status);
    txt_copy(f->cat, cat, (u16)sizeof f->cat);
    txt_copy(f->stat, status, (u16)sizeof f->stat);
    if (rc == ENG_OK && f->stat[0] != '\0' &&
        (flags & (ENG_IF_AVAIL | ENG_IF_UNAVAIL)))
        rc = eng_fail(e, ENG_EARG, k_status, ENG_TX_NONE);
    if (rc == ENG_OK)
        rc = eng_filter_len(e, tag, ENG_TAGFILT, k_tag);
    if (rc == ENG_OK && tag != NULL && tag[0] != '\0' &&
        (!tags_norm(tag, f->tag) || f->tag[0] == '\0'))
        rc = eng_fail(e, ENG_EARG, k_tag, ENG_TX_BADTAG);
    if (rc == ENG_OK)
        rc = works_filter(e, works, f->works);
    return rc;
}

int eng_item_scan(eng *e, eng_scan *s, int order, const eng_ifilt *f,
                  const char *loc, const char *after)
{
    char prefix[ENG_IDLEN + 1];
    u32 lid;
    u16 plen;
    int flags;
    int ix;
    int rc;

    if (f->text[0] != '\0') {
        ix = ENG_SCAN_RECORDS;
    } else {
        switch (order) {
        case ENG_ORD_ID:
            ix = ENG_X_ITEMS1;
            break;
        case ENG_ORD_LOC:
            ix = ENG_X_ITEMS2;
            break;
        case ENG_ORD_MFR:
            ix = ENG_X_ITEMS4;
            break;
        case ENG_ORD_SERIAL:
            ix = ENG_X_ITEMS5;
            break;
        default:
            return eng_fail(e, ENG_EARG, "ORDER", ENG_TX_BADORDER);
        }
    }
    flags = f->flags;
    lid = 0;
    if (loc != NULL && loc[0] != '\0') {
        rc = eng_loc_id(e, loc, &lid);
        if (rc == ENG_ENOTFOUND)
            rc = eng_fail(e, rc, "LOC", ENG_TX_NOSUCHLOC);
        if (rc != ENG_OK)
            return rc;
        flags |= IF_LOC;
    }
    plen = 0;
    if (ix == ENG_X_ITEMS2 && (flags & IF_LOC)) {
        txt_u32w(prefix, lid, ENG_IDLEN, ' ');
        plen = ENG_IDLEN;
        flags &= ~IF_LOC;
    }
    rc = eng_scan_open(e, s, ENG_T_ITEMS, ix,
                       plen ? (const u8 *)prefix : NULL, plen, after);
    if (rc != ENG_OK)
        return rc;
    memcpy(&s->f, f, sizeof s->f);
    s->f.flags = (u8)flags;
    s->floc = lid;
    /* With no filter the scan keeps every live item, so no row needs a
       call; a filter added to eng_item_keep is added here too. */
    if (flags != 0 || f->text[0] != '\0' || f->cat[0] != '\0' ||
        f->stat[0] != '\0' || f->tag[0] != '\0' || f->works[0] != '\0')
        s->match = item_match;
    return ENG_OK;
}

/* The order of the n characters at w against the tag t, A-Z folded:
   below 0, 0 or above 0. */
static int tag_cmp(const char *w, u16 n, const char *t)
{
    int a;
    int b;

    for (;; n--) {
        a = n > 0 ? *w++ : '\0';
        b = *t++;
        a = TXT_UPPER(a);
        b = TXT_UPPER(b);
        if (a != b || a == '\0')
            return a - b;
    }
}

/* The length of the set of ITEM TAGS (len bytes of tags, each ended by
   a NUL, A-Z folded in ascending order) with the tag of n characters at
   w put in its place, unless the set holds it.  A binary search over
   the bytes: each probe backs up to the start of its tag. */
static u16 set_add(char *set, u16 len, const char *w, u16 n)
{
    u16 lo;
    u16 hi;
    u16 m;
    int d;

    lo = 0;
    hi = len;
    while (lo < hi) {
        m = (u16)((lo + hi) / 2);
        while (m > lo && set[m - 1] != '\0')
            m--;
        d = tag_cmp(w, n, set + m);
        if (d == 0)
            return len;
        if (d < 0)
            hi = m;
        else
            lo = (u16)(m + strlen(set + m) + 1);
    }
    memmove(set + lo + n + 1, set + lo, (size_t)(len - lo));
    memcpy(set + lo, w, n);
    set[lo + n] = '\0';
    return (u16)(len + n + 1);
}

int eng_item_tags(eng *e, const char *after, u16 *len, char *cursor)
{
    eng_scan s;
    char v[2][TAGSLEN + 1];     /* this record's TAGS, the last one's  */
    char *set;
    const char *w;
    u32 r;
    u16 n;
    u16 i;
    int k;
    int more;
    int rc;

    set = eng_memobuf(e);
    i = 0;
    k = 0;
    v[1][0] = '\0';
    cursor[0] = '\0';
    rc = eng_scan_open(e, &s, ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, after);
    while (rc == ENG_OK && i + TAGREC <= ENG_TAGSET) {
        rc = eng_scan_next(e, &s, &r);
        if (rc != ENG_OK || r == 0)
            break;
        eng_rec_str(e, ENG_T_ITEMS, F_TAGS, v[k], (u16)sizeof v[k]);
        if (strcmp(v[k], v[!k]) == 0)
            continue;
        for (w = v[k]; (n = tag_word(&w)) != 0; w += n)
            if (tag_ok(w, n))
                i = set_add(set, i, w, n);
        k = !k;
    }
    more = 0;
    if (rc == ENG_OK)
        rc = eng_scan_more(e, &s, &more);
    if (more)
        eng_scan_cursor(&s, cursor);
    *len = i;
    return rc;
}

int eng_item_serial(eng *e, eng_scan *s, const char *sn,
                    const char *after)
{
    char key[ENG_SERLEN + 1];
    u16 i;

    if (sn == NULL || strlen(sn) > ENG_SERLEN)
        return eng_fail(e, ENG_EARG, "SERIAL", ENG_TX_SERLONG);
    strcpy(key, sn);
    eng_upper(key);
    for (i = (u16)strlen(key); i < ENG_SERLEN; i++)
        key[i] = ' ';
    key[ENG_SERLEN] = '\0';
    return eng_scan_open(e, s, ENG_T_ITEMS, ENG_X_ITEMS5, (const u8 *)key,
                         ENG_SERLEN, after);
}

/* ---------------------------------------------------------------------
 * Repair.
 * --------------------------------------------------------------------- */

int eng_item_heal(eng *e)
{
    char was[ENG_AUDVAL + 1];
    char v[ENG_AUDVAL + 1];
    char now[TXT_U32LEN + 1];
    u32 end;
    u32 k;
    u32 r;
    u32 id;
    u32 prev;                   /* the item the row before inserted     */
    u32 qrow;                   /* the last user's U row of a QTY       */
    u32 m;                      /* the item the row before it inserted  */
    i32 n;
    int rc;

    end = eng_reccount(e, ENG_T_AUDIT);
    prev = 0;
    qrow = 0;
    m = 0;
    for (k = eng_marker_from(e) + 1; k <= end; k++) {
        rc = eng_rec_read(e, ENG_T_AUDIT, k);
        if (rc != ENG_OK)
            return rc;
        id = prev;
        prev = 0;
        if (!eng_rec_is(e, ENG_T_AUDIT, F_ATBL,
                        eng_table_def(ENG_T_ITEMS)->name))
            continue;
        eng_rec_str(e, ENG_T_AUDIT, F_AACTION, v, (u16)sizeof v);
        if (v[0] == ENG_A_INSERT)
            prev = (u32)eng_rec_num(e, ENG_T_AUDIT, F_ARECID);
        if (!eng_rec_is(e, ENG_T_AUDIT, F_AFLD, k_qty) ||
            eng_rec_is(e, ENG_T_AUDIT, F_AUSER, ENG_SYSTEM))
            continue;
        qrow = k;
        m = id;
    }
    if (m == 0)
        return ENG_OK;

    /* The split's U row and its new item, live; the item it took from,
       live, holding the row's old QTY, which less the new item's QTY is
       the row's new QTY and no less than the units its open rows
       hold. */
    rc = eng_rec_read(e, ENG_T_AUDIT, qrow);
    if (rc == ENG_OK)
        rc = eng_item_seek(e, m, &r);
    if (rc != ENG_OK || r == 0)
        return rc;
    n = eng_rec_num(e, ENG_T_ITEMS, F_QTY);
    id = (u32)eng_rec_num(e, ENG_T_AUDIT, F_ARECID);
    rc = eng_item_seek(e, id, &r);
    if (rc != ENG_OK || r == 0)
        return rc;
    n = eng_rec_num(e, ENG_T_ITEMS, F_QTY) - n;
    eng_rec_str(e, ENG_T_AUDIT, F_AOLD, was, (u16)sizeof was);
    eng_rec_str(e, ENG_T_ITEMS, F_QTY, v, (u16)sizeof v);
    if (n < 0 || n < eng_item_held(e) || strcmp(v, was) != 0)
        return ENG_OK;
    eng_rec_str(e, ENG_T_AUDIT, F_ANEW, v, (u16)sizeof v);
    txt_u32(now, (u32)n);
    if (strcmp(v, now) != 0)
        return ENG_OK;
    rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_QTY, n);
    if (rc == ENG_OK)
        rc = stamp(e);
    if (rc != ENG_OK)
        return eng_edit_finish(e, rc, id);
    return eng_edit_heal(e, F_QTY, was, ENG_A_UPDATE, id);
}
