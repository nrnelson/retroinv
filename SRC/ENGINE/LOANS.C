/*
 * LOANS.C - loans, reservations and in-use assignments (LOANS.DBF and
 * its indexes LOANS1, LOANS2), and the upkeep of the counters ITEMS
 * ON_LOAN, RESERVED, IN_USE and ATTACHED.
 *
 * A row of LOANS.DBF takes units of an item out of stock until its
 * RET_DATE is set: KIND L lends them, R reserves them, U marks them in
 * use.  The rules kept here are those of ENGLOAN.H.  The counter of a
 * kind on an item is the sum of the QTY of the item's open rows of that
 * kind, and ATTACHED of an item the number of open R and U rows naming
 * it as HOST_ID.  A change of a row commits the row, then the item's
 * counter, then the hosts' ATTACHED, each with UPDATED stamped, in one
 * batch naming LOANS, so a stop between them leaves the marker and the
 * next open's repair (eng_loan_heal) recomputes every counter.
 *
 * The protocol keys DUE (DUE_DATE), DATE (OUT_DATE) and HOST (HOST_ID)
 * stand for the fields; errors name the field.  An empty OUT_DATE, like
 * an empty date of LOAN RETURN or ASOF, means today, so every row has
 * an OUT_DATE.
 *
 * Rows are found by LOAN_ID through eng_id_find (LOANS.DBF is appended
 * to in ID order).  LOANS2 starts with STR(YEAR(RET_DATE),4), which is
 * three spaces and a 0 for an open row, then the due date as YYYYMMDD,
 * so the open rows are one key prefix in due order.  A host's rows are
 * found by walking that prefix: an index on HOST_ID would take a DOS
 * handle FILES= has no room for.
 *
 * The counters are the four ITEMS fields from ON_LOAN on, one per kind
 * in the order of kinds, then ATTACHED; ITEMS.C checks at compile time
 * that they follow one another.  Field names in error reports come from
 * the open table's descriptors, so this module keeps almost no strings
 * of its own in near data.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENGFLD_WIDTHS
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGITEM.H"
#include "ENGASSY.H"
#include "ENGLOAN.H"

#define MAXQTY  99999L          /* largest QTY and counter (N, 5 wide):
                                   the cap of the repair's sums        */
#define YLEN    4               /* STR(YEAR(RET_DATE),4), first in LOANS2 */
#define NKIND   3               /* the kinds, L R U                     */

#define L_ID        ENG_FIELD(LOANS, LOAN_ID)
#define L_ITEM      ENG_FIELD(LOANS, ITEM_ID)
#define L_KIND      ENG_FIELD(LOANS, KIND)
#define L_QTY       ENG_FIELD(LOANS, QTY)
#define L_HOLDER    ENG_FIELD(LOANS, HOLDER)
#define L_HOST      ENG_FIELD(LOANS, HOST_ID)
#define L_CONTACT   ENG_FIELD(LOANS, CONTACT)
#define L_OUT       ENG_FIELD(LOANS, OUT_DATE)
#define L_DUE       ENG_FIELD(LOANS, DUE_DATE)
#define L_RET       ENG_FIELD(LOANS, RET_DATE)
#define L_NOTES     ENG_FIELD(LOANS, NOTES)

#define I_ID        ENG_FIELD(ITEMS, ITEM_ID)
#define I_QTY       ENG_FIELD(ITEMS, QTY)
#define I_ONLOAN    ENG_FIELD(ITEMS, ON_LOAN)
#define I_RESERVED  ENG_FIELD(ITEMS, RESERVED)
#define I_INUSE     ENG_FIELD(ITEMS, IN_USE)
#define I_ATTACH    ENG_FIELD(ITEMS, ATTACHED)
#define I_PARENT    ENG_FIELD(ITEMS, PARENT_ID)
#define I_STATUS    ENG_FIELD(ITEMS, STATUS)
#define I_UPDATED   ENG_FIELD(ITEMS, UPDATED)

/* The LOANS fields a pair may set, one bit per field number: those of
   every kind when the row is made, less QTY and OUT_DATE on a change,
   and each kind's own (kind_keys). */
#define BIT(f)      ((u16)(1U << (f)))
#define NEW_KEYS    (BIT(L_QTY) | BIT(L_HOLDER) | BIT(L_CONTACT) | \
                     BIT(L_OUT) | BIT(L_NOTES))
#define SET_KEYS    (BIT(L_HOLDER) | BIT(L_CONTACT) | BIT(L_NOTES))

/* eng_scan.f.flags of the loan scans: keep open rows only, keep rows with
   a due date only.  The scans keep a KIND filter in eng_scan.f.stat, a
   HOLDER in eng_scan.f.text and a host in eng_scan.floc. */
#define MF_OPEN     1
#define MF_DUE      2

/* The HOLDER filter fits eng_scan.f.text, the width eng_rec_is reads. */
extern char loans_filter_fits[1];
extern char loans_filter_fits[(ENG_FW_LOANS_HOLDER <= ENG_MAXFIND) ? 1 : 2];

/* check_row reads HOLDER and then OUT_DATE into one buffer. */
extern char loans_holder_fits[1];
extern char loans_holder_fits[(ENG_FW_LOANS_HOLDER >= ENG_DATELEN) ? 1 : 2];

static const char kinds[] = "LRU";
static const u16 kind_keys[NKIND] = {
    BIT(L_DUE), BIT(L_DUE) | BIT(L_HOST), BIT(L_HOST)
};

/* The keys that stand for fields: word i of aliases is field
   alias_f[i]. */
static const char aliases[] = "DUE DATE HOST";
static const u8 alias_f[] = { L_DUE, L_OUT, L_HOST };

static const char k_item[] = "ITEM";
static const char k_host[] = "HOST";
static const char k_id[] = "ID";
static const char k_asof[] = "ASOF";

/* The open-row prefix of LOANS2, STR(0,4), into key (YLEN bytes). */
static void open_prefix(char *key)
{
    memset(key, ' ', YLEN - 1);
    key[YLEN - 1] = '0';
}

/* The number of kind c (an ENG_K_ letter), 0 .. NKIND - 1; any other
   character counts as L. */
static int kind_num(int c)
{
    int k;

    for (k = NKIND - 1; k > 0 && kinds[k] != (char)c; k--)
        ;
    return k;
}

/* The kind of the row in the LOANS buffer, 0 .. NKIND - 1; a KIND other
   than R or U (written by another program) counts as L. */
static int row_kind(eng *e)
{
    char v[2];

    eng_rec_str(e, ENG_T_LOANS, L_KIND, v, (u16)sizeof v);
    return kind_num(v[0]);
}

/* Move counter f of live item id by delta, never below 0, stamping
   UPDATED; nothing when id names no live item or the value stays. */
static int add_count(eng *e, u32 id, u16 f, i32 delta)
{
    char today[ENG_DATELEN + 1];
    u32 r;
    i32 v;
    int rc;

    rc = eng_item_seek(e, id, &r);
    if (rc != ENG_OK || r == 0)
        return rc;
    v = eng_rec_num(e, ENG_T_ITEMS, f);
    if (v + delta < 0)
        delta = -v;
    if (delta == 0)
        return ENG_OK;
    eng_today(e, today);
    rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putnumf(e, f, v + delta);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, I_UPDATED, today);
    return eng_edit_finish(e, rc, id);
}

/* Store the pairs into the edited row: each key a field allowed by the
   bits of allow, or an alias of one; any other key is ENG_EARG naming
   it, a field given twice ENG_EARG naming the field. */
static int apply(eng *e, const eng_pair *p, u16 n, u16 allow)
{
    u16 seen;
    u16 i;
    int f;
    int rc;

    seen = 0;
    for (i = 0; i < n; i++) {
        f = txt_word_num(aliases, p[i].key);
        f = f >= 0 ? alias_f[f] : eng_field_num(e, ENG_T_LOANS, p[i].key);
        if (f < 0 || (allow & BIT(f)) == 0)
            return eng_fail(e, ENG_EARG, p[i].key, ENG_TX_NOSET);
        if (seen & BIT(f))
            return eng_fail(e, ENG_EARG,
                            eng_field_name(e, ENG_T_LOANS, (u16)f),
                            ENG_TX_NONE);
        seen = (u16)(seen | BIT(f));
        rc = eng_edit_putf(e, (u16)f, p[i].val);
        if (rc != ENG_OK)
            return rc;
    }
    return ENG_OK;
}

/* ENG_EARG naming field f of LOANS, with text tx. */
static int bad(eng *e, u16 f, u16 tx)
{
    return eng_fail(e, ENG_EARG, eng_field_name(e, ENG_T_LOANS, f), tx);
}

/* The host host (not 0) of a row of item: a live item with STATUS A that
   is neither item nor one of its parts (item being top-level, it has no
   ancestors).  ENG_ENOTFOUND or ENG_ECONSTR naming HOST_ID.  Moves the
   ITEMS buffer. */
static int check_host(eng *e, u32 item, u32 host)
{
    char st[ENG_STATLEN + 1];
    const char *hf;
    u32 recs[ENG_MAXDEPTH];
    u32 r;
    u16 n;
    int rc;

    hf = eng_field_name(e, ENG_T_LOANS, L_HOST);
    if (host == item)
        return eng_fail(e, ENG_ECONSTR, hf, ENG_TX_HOSTSELF);
    rc = eng_item_need(e, host, hf, &r);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_ITEMS, I_STATUS, st, (u16)sizeof st);
    if (st[0] != 'A')
        return eng_fail(e, ENG_ECONSTR, hf, ENG_TX_DISPOSED);
    rc = eng_assy_path(e, host, recs, &n);
    if (rc == ENG_OK && n > 0)
        rc = eng_rec_read(e, ENG_T_ITEMS, recs[0]);
    if (rc == ENG_OK && n > 0 &&
        (u32)eng_rec_num(e, ENG_T_ITEMS, I_ID) == item)
        rc = eng_fail(e, ENG_ECONSTR, hf, ENG_TX_HOSTSELF);
    return rc;
}

/* The edited row of kind k of item keeps the rules of its kind: a loan
   has a HOLDER and a DUE_DATE, a reservation or an assignment a HOLDER
   or a HOST_ID; a DUE_DATE given is not before OUT_DATE (a blank
   OUT_DATE, which only another program can write, sorts before every
   date); a HOST_ID other than old, the row's host before the edit, is
   0 or passes check_host.  Moves the ITEMS buffer. */
static int check_row(eng *e, int k, u32 item, u32 old)
{
    u32 host;
    char v[ENG_FW_LOANS_HOLDER + 1];    /* HOLDER, then OUT_DATE        */
    char due[ENG_DATELEN + 1];

    eng_rec_str(e, ENG_T_LOANS, L_HOLDER, v, (u16)sizeof v);
    eng_rec_str(e, ENG_T_LOANS, L_DUE, due, (u16)sizeof due);
    if (k == 0 && v[0] == '\0')
        return bad(e, L_HOLDER, ENG_TX_REQUIRED);
    if (k == 0 && due[0] == '\0')
        return bad(e, L_DUE, ENG_TX_REQUIRED);
    if (k != 0 && v[0] == '\0' && eng_rec_num(e, ENG_T_LOANS, L_HOST) == 0)
        return bad(e, L_HOLDER, ENG_TX_NOHOLDER);
    eng_rec_str(e, ENG_T_LOANS, L_OUT, v, (u16)sizeof v);
    if (due[0] != '\0' && strcmp(due, v) < 0)
        return bad(e, L_DUE, ENG_TX_DUEOUT);
    host = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
    return host != old && host != 0 ? check_host(e, item, host) : ENG_OK;
}

int eng_loan_out(eng *e, u32 item, int kind, const eng_pair *p, u16 n,
                 u32 *id)
{
    char today[ENG_DATELEN + 1];
    char od[ENG_DATELEN + 1];
    char st[ENG_STATLEN + 1];
    char kv[2];
    u32 irec;
    u32 nid;
    u32 host;
    i32 room;
    i32 qty;
    int k;
    int rc;

    *id = 0;
    nid = 0;
    qty = 0;
    host = 0;
    k = kind_num(kind);
    rc = eng_item_need(e, item, k_item, &irec);
    if (rc != ENG_OK)
        return rc;
    if (eng_rec_num(e, ENG_T_ITEMS, I_PARENT) != 0)
        return eng_fail(e, ENG_ECONSTR, k_item, ENG_TX_INST);
    eng_rec_str(e, ENG_T_ITEMS, I_STATUS, st, (u16)sizeof st);
    if (st[0] != 'A')
        return eng_fail(e, ENG_ECONSTR, k_item, ENG_TX_DISPOSED);
    room = eng_rec_num(e, ENG_T_ITEMS, I_QTY) - eng_item_held(e);
    eng_today(e, today);
    kv[0] = (char)kind;
    kv[1] = '\0';

    rc = eng_edit_begin(e, ENG_T_LOANS, 0);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putnumf(e, L_ITEM, (i32)item);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, L_KIND, kv);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, L_QTY, 1);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, L_HOST, 0);
    if (rc == ENG_OK)
        rc = apply(e, p, n, (u16)(NEW_KEYS | kind_keys[k]));
    if (rc == ENG_OK) {
        eng_rec_str(e, ENG_T_LOANS, L_OUT, od, (u16)sizeof od);
        if (od[0] == '\0')
            rc = eng_edit_putf(e, L_OUT, today);
    }
    if (rc == ENG_OK) {
        qty = eng_rec_num(e, ENG_T_LOANS, L_QTY);
        host = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
        if (qty < 1)
            rc = bad(e, L_QTY, ENG_TX_NONE);
    }
    if (rc == ENG_OK)
        rc = check_row(e, k, item, 0);
    if (rc == ENG_OK && qty > room)
        rc = eng_fail(e, ENG_ECONSTR, eng_field_name(e, ENG_T_LOANS, L_QTY),
                      ENG_TX_NOSTOCK);
    if (rc == ENG_OK)
        rc = eng_batch_begin(e, ENG_T_LOANS);
    if (rc != ENG_OK) {
        eng_edit_cancel(e);
        return rc;
    }

    rc = eng_seq_next(e, ENG_T_LOANS, 1, &nid);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, L_ID, (i32)nid);
    rc = eng_edit_finish(e, rc, nid);
    if (rc == ENG_OK)
        rc = add_count(e, item, (u16)(I_ONLOAN + k), qty);
    if (rc == ENG_OK)
        rc = add_count(e, host, I_ATTACH, 1);
    rc = eng_batch_end(e, rc);
    if (rc == ENG_OK)
        *id = nid;
    return rc;
}

int eng_loan_hosted(eng *e, u32 top, u32 child, const char *field)
{
    ndx *x;
    char key[ENG_IDLEN + 1];
    u8 k[ENG_MAXKEYLEN];
    char ret[ENG_DATELEN + 1];
    u32 recs[ENG_MAXDEPTH];
    u32 cid;
    u32 host;
    u32 r;
    i32 left;
    i32 q;
    u16 tx;
    u16 reads;
    u16 n;
    int exact;
    int rc;

    rc = eng_rec_read(e, ENG_T_ITEMS, child);
    cid = (u32)eng_rec_num(e, ENG_T_ITEMS, I_ID);
    if (rc == ENG_OK)
        rc = eng_rec_read(e, ENG_T_ITEMS, top);
    left = eng_rec_num(e, ENG_T_ITEMS, I_RESERVED);
    tx = left > 0 ? ENG_TX_HELDRES : ENG_TX_HELDUSE;
    left += eng_rec_num(e, ENG_T_ITEMS, I_INUSE);
    if (rc != ENG_OK || left <= 0 ||
        !eng_id_key((u32)eng_rec_num(e, ENG_T_ITEMS, I_ID), key))
        return rc;
    x = eng_ndx(e, ENG_X_LOANS1);
    rc = eng_dbferr(e, ndx_seek(x, (const u8 *)key, ENG_IDLEN, &r, &exact),
                    1);
    for (reads = 0; rc == ENG_OK && r != 0 && left > 0; reads++) {
        rc = eng_dbferr(e, ndx_key(x, k), 1);
        if (rc != ENG_OK || memcmp(k, key, ENG_IDLEN) != 0)
            break;
        if (reads == ENG_MAXREADS)
            return eng_fail(e, ENG_ECONSTR, field, tx);
        rc = eng_rec_read(e, ENG_T_LOANS, r);
        eng_rec_str(e, ENG_T_LOANS, L_RET, ret, (u16)sizeof ret);
        n = 0;
        if (rc == ENG_OK && !dbf_is_deleted(eng_dbf(e, ENG_T_LOANS)) &&
            ret[0] == '\0' && row_kind(e) != 0) {
            q = eng_rec_num(e, ENG_T_LOANS, L_QTY);
            if (q > 0)
                left -= q;
            host = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
            if (host == cid)
                return eng_fail(e, ENG_ECONSTR, field, ENG_TX_HOSTSELF);
            if (host != 0)
                rc = eng_assy_path(e, host, recs, &n);
        }
        if (rc == ENG_ENOTFOUND) {
            eng_clear_error(e);
            rc = ENG_OK;
            n = 0;
        }
        while (rc == ENG_OK && n > 0)
            if (recs[--n] == child)
                return eng_fail(e, ENG_ECONSTR, field, ENG_TX_HOSTSELF);
        if (rc == ENG_OK)
            rc = eng_dbferr(e, ndx_next(x, &r), 1);
    }
    return rc;
}

int eng_loan_get(eng *e, u32 id, u32 *recno)
{
    int rc;

    *recno = 0;
    rc = ENG_OK;
    if (id != 0 && id <= ENG_MAXID)
        rc = eng_id_find(e, ENG_T_LOANS, id, recno);
    if (rc == ENG_OK && *recno == 0)
        rc = eng_fail(e, ENG_ENOTFOUND, k_id, ENG_TX_NONE);
    return rc;
}

int eng_loan_return(eng *e, u32 id, const char *date)
{
    char today[ENG_DATELEN + 1];
    char out[ENG_DATELEN + 1];
    char ret[ENG_DATELEN + 1];
    u32 lrec;
    u32 item;
    u32 host;
    i32 qty;
    int k;
    int rc;

    rc = eng_loan_get(e, id, &lrec);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_LOANS, L_RET, ret, (u16)sizeof ret);
    if (ret[0] != '\0')
        return eng_fail(e, ENG_ECONSTR, k_id, ENG_TX_RETURNED);
    eng_rec_str(e, ENG_T_LOANS, L_OUT, out, (u16)sizeof out);
    qty = eng_rec_num(e, ENG_T_LOANS, L_QTY);
    item = (u32)eng_rec_num(e, ENG_T_LOANS, L_ITEM);
    host = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
    k = row_kind(e);
    if (date == NULL || date[0] == '\0') {
        eng_today(e, today);
        date = today;
    }

    rc = eng_edit_begin(e, ENG_T_LOANS, lrec);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_putf(e, L_RET, date);
    if (rc == ENG_OK && strcmp(date, out) < 0)
        rc = bad(e, L_RET, ENG_TX_RETOUT);
    if (rc == ENG_OK)
        rc = eng_batch_begin(e, ENG_T_LOANS);
    if (rc != ENG_OK) {
        eng_edit_cancel(e);
        return rc;
    }

    rc = eng_edit_commit(e, id);
    if (rc == ENG_OK)
        rc = add_count(e, item, (u16)(I_ONLOAN + k), -qty);
    if (rc == ENG_OK && k != 0)
        rc = add_count(e, host, I_ATTACH, -1);
    return eng_batch_end(e, rc);
}

int eng_loan_set(eng *e, u32 id, const eng_pair *p, u16 n)
{
    char ret[ENG_DATELEN + 1];
    u32 r;
    u32 item;
    u32 old;
    u32 host;
    int k;
    int rc;

    rc = eng_loan_get(e, id, &r);
    if (rc != ENG_OK)
        return rc;
    k = row_kind(e);
    item = (u32)eng_rec_num(e, ENG_T_LOANS, L_ITEM);
    old = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
    eng_rec_str(e, ENG_T_LOANS, L_RET, ret, (u16)sizeof ret);
    rc = eng_edit_begin(e, ENG_T_LOANS, r);
    if (rc != ENG_OK)
        return rc;
    rc = apply(e, p, n, (u16)(SET_KEYS | kind_keys[k]));
    if (rc == ENG_OK)
        rc = check_row(e, k, item, old);
    host = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
    if (rc != ENG_OK || host == old || ret[0] != '\0')
        return eng_edit_finish(e, rc, id);

    /* An open row moved to another host: the old one loses it, the new
       one gains it. */
    rc = eng_batch_begin(e, ENG_T_LOANS);
    if (rc != ENG_OK) {
        eng_edit_cancel(e);
        return rc;
    }
    rc = eng_edit_commit(e, id);
    if (rc == ENG_OK)
        rc = add_count(e, old, I_ATTACH, -1);
    if (rc == ENG_OK)
        rc = add_count(e, host, I_ATTACH, 1);
    return eng_batch_end(e, rc);
}

/* The filter of the loan scans: s->f.flags, the kind in s->f.stat, the
   host in s->floc and the holder in s->f.text. */
static int loan_match(eng *e, eng_scan *s)
{
    char v[ENG_DATELEN + 1];

    if (s->f.flags & MF_OPEN) {
        eng_rec_str(e, ENG_T_LOANS, L_RET, v, (u16)sizeof v);
        if (v[0] != '\0')
            return 0;
    }
    if (s->f.flags & MF_DUE) {
        eng_rec_str(e, ENG_T_LOANS, L_DUE, v, (u16)sizeof v);
        if (v[0] == '\0')
            return 0;
    }
    if (s->f.stat[0] != '\0' && kinds[row_kind(e)] != s->f.stat[0])
        return 0;
    if (s->floc != 0 &&
        (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST) != s->floc)
        return 0;
    return eng_rec_is(e, ENG_T_LOANS, L_HOLDER, s->f.text);
}

int eng_loan_scan(eng *e, eng_scan *s, u32 item, u32 host, int kind,
                  const char *holder, int open, const char *after)
{
    char key[ENG_IDLEN + 1];
    u32 r;
    int rc;

    rc = eng_filter_len(e, holder, ENG_FW_LOANS_HOLDER,
                        eng_field_name(e, ENG_T_LOANS, L_HOLDER));
    if (rc != ENG_OK)
        return rc;
    if (host != 0) {
        rc = eng_item_need(e, host, k_host, &r);
        if (rc != ENG_OK)
            return rc;
        open = 1;
    }
    if (item == 0) {
        if (open)
            open_prefix(key);
        rc = eng_scan_open(e, s, ENG_T_LOANS, ENG_X_LOANS2,
                           open ? (const u8 *)key : NULL,
                           (u16)(open ? YLEN : 0), after);
        open = 0;
    } else {
        rc = eng_item_need(e, item, k_item, &r);
        if (rc != ENG_OK)
            return rc;
        eng_id_key(item, key);
        rc = eng_scan_open(e, s, ENG_T_LOANS, ENG_X_LOANS1, (const u8 *)key,
                           ENG_IDLEN, after);
    }
    if (rc != ENG_OK)
        return rc;
    s->f.flags = (u8)(open ? MF_OPEN : 0);
    s->f.stat[0] = (char)kind;
    s->floc = host;
    txt_copy(s->f.text, holder, (u16)sizeof s->f.text);
    s->match = loan_match;
    return ENG_OK;
}

int eng_loan_asof_ok(eng *e, const char *asof)
{
    if (asof == NULL || asof[0] == '\0' || txt_date_ok(asof))
        return ENG_OK;
    return eng_fail(e, ENG_EARG, k_asof, ENG_TX_NONE);
}

int eng_loan_overdue(eng *e, eng_scan *s, const char *asof,
                     const char *after)
{
    char bound[YLEN + ENG_DATELEN + 1];
    int rc;

    rc = eng_loan_asof_ok(e, asof);
    if (rc != ENG_OK)
        return rc;
    open_prefix(bound);
    if (asof == NULL || asof[0] == '\0')
        eng_today(e, bound + YLEN);
    else
        strcpy(bound + YLEN, asof);
    rc = eng_scan_open(e, s, ENG_T_LOANS, ENG_X_LOANS2, (const u8 *)bound,
                       YLEN, after);
    if (rc != ENG_OK)
        return rc;
    memcpy(s->bound, bound, YLEN + ENG_DATELEN);
    s->blen = YLEN + ENG_DATELEN;
    s->f.flags = MF_DUE;
    s->match = loan_match;
    return ENG_OK;
}

int eng_loan_late(eng *e, const char *asof)
{
    char today[ENG_DATELEN + 1];
    char ret[ENG_DATELEN + 1];
    char due[ENG_DATELEN + 1];

    if (asof == NULL || asof[0] == '\0') {
        eng_today(e, today);
        asof = today;
    }
    eng_rec_str(e, ENG_T_LOANS, L_RET, ret, (u16)sizeof ret);
    eng_rec_str(e, ENG_T_LOANS, L_DUE, due, (u16)sizeof due);
    return ret[0] == '\0' && due[0] != '\0' && strcmp(due, asof) < 0;
}

int eng_loan_item(eng *e, u32 *recno)
{
    i32 item;

    item = eng_rec_num(e, ENG_T_LOANS, L_ITEM);
    return eng_item_seek(e, item > 0 ? (u32)item : 0, recno);
}

/* ---------------------------------------------------------------------
 * Repair.
 * --------------------------------------------------------------------- */

/* The repair's change of counter f of the item in the ITEMS buffer
   (record recno, ID id) to v when it differs, with UPDATED today, the
   repair's date (ENG_DATELEN + 1 bytes), which its first change reads
   from the clock while it is "".  Its O or R row, the old and new text
   as a commit gives them, is written and AUDIT.DBF and its
   indexes flushed first; the commit then writes no row
   (eng_edit_quiet).  So a stop leaves the row without the record, which
   the next open's repair writes again with the same row, but never the
   record without its row. */
static int heal_put(eng *e, u32 recno, u32 id, u16 f, i32 v, char *today)
{
    char was[ENG_AUDVAL + 1];
    i32 old;
    int rc;

    old = eng_rec_num(e, ENG_T_ITEMS, f);
    if (old == v)
        return ENG_OK;
    if (today[0] == '\0')
        eng_today(e, today);
    rc = eng_edit_begin(e, ENG_T_ITEMS, recno);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_ITEMS, f, was, (u16)sizeof was);
    rc = eng_edit_putnumf(e, f, v);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, I_UPDATED, today);
    if (rc != ENG_OK)
        return eng_edit_finish(e, rc, id);
    return eng_edit_heal(e, f, was,
                         (char)(v < old ? ENG_A_RETURN : ENG_A_LOANOUT), id);
}

/* Give live item id the sums of its open rows of each kind, each capped
   at what its QTY leaves after the kinds before it (heal_put). */
static int heal_item(eng *e, u32 id, const i32 *sum, char *today)
{
    u32 r;
    i32 left;
    i32 v;
    int k;
    int rc;

    rc = eng_item_seek(e, id, &r);
    if (rc != ENG_OK || r == 0)
        return rc;
    left = eng_rec_num(e, ENG_T_ITEMS, I_QTY);
    for (k = 0; k < NKIND && rc == ENG_OK; k++) {
        v = sum[k] > left ? left : sum[k];
        if (v < 0)
            v = 0;
        left -= v;
        rc = heal_put(e, r, id, (u16)(I_ONLOAN + k), v, today);
    }
    return rc;
}

/* Give live item id the number of open live R and U rows naming it as
   HOST_ID for ATTACHED (heal_put), counted on one walk of the open
   prefix of LOANS2; with only0 set, only when its ATTACHED is 0. */
static int heal_host(eng *e, u32 id, int only0, char *today)
{
    ndx *x;
    char pre[YLEN];
    u8 key[ENG_MAXKEYLEN];
    u32 ir;
    u32 r;
    i32 n;
    int exact;
    int rc;

    rc = eng_item_seek(e, id, &ir);
    if (rc != ENG_OK || ir == 0 ||
        (only0 && eng_rec_num(e, ENG_T_ITEMS, I_ATTACH) != 0))
        return rc;
    x = eng_ndx(e, ENG_X_LOANS2);
    open_prefix(pre);
    n = 0;
    rc = eng_dbferr(e, ndx_seek(x, (const u8 *)pre, YLEN, &r, &exact), 1);
    while (rc == ENG_OK && r != 0) {
        rc = eng_dbferr(e, ndx_key(x, key), 1);
        if (rc != ENG_OK || memcmp(key, pre, YLEN) != 0)
            break;
        rc = eng_rec_read(e, ENG_T_LOANS, r);
        if (rc == ENG_OK && !dbf_is_deleted(eng_dbf(e, ENG_T_LOANS)) &&
            row_kind(e) != 0 &&
            (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST) == id && n < MAXQTY)
            n++;
        if (rc == ENG_OK)
            rc = eng_dbferr(e, ndx_next(x, &r), 1);
    }
    if (rc == ENG_OK)
        rc = heal_put(e, ir, id, I_ATTACH, n, today);
    return rc;
}

/* The first pass of eng_loan_heal: LOANS1 from its first entry, the QTY
   of the open live rows of each kind summed per item and the item
   corrected when its group of entries ends (heal_item); the host of an
   open R or U row whose ATTACHED is 0 is counted on the way
   (heal_host). */
static int heal_loans(eng *e, char *today)
{
    ndx *x;
    u8 key[ENG_MAXKEYLEN];
    u8 cur[ENG_IDLEN];
    char ret[ENG_DATELEN + 1];
    i32 sum[NKIND];
    i32 q;
    u32 r;
    u32 host;
    int have;
    int k;
    int rc;

    x = eng_ndx(e, ENG_X_LOANS1);
    have = 0;
    rc = eng_dbferr(e, ndx_first(x, &r), 1);
    while (rc == ENG_OK) {
        if (r != 0)
            rc = eng_dbferr(e, ndx_key(x, key), 1);
        if (rc != ENG_OK)
            break;
        if (have && (r == 0 || memcmp(key, cur, ENG_IDLEN) != 0)) {
            rc = heal_item(e, eng_key_id(cur, ENG_IDLEN), sum, today);
            have = 0;
            if (rc != ENG_OK)
                break;
        }
        if (r == 0)
            break;
        if (!have) {
            memcpy(cur, key, ENG_IDLEN);
            memset(sum, 0, sizeof sum);
            have = 1;
        }
        rc = eng_rec_read(e, ENG_T_LOANS, r);
        if (rc != ENG_OK)
            break;
        eng_rec_str(e, ENG_T_LOANS, L_RET, ret, (u16)sizeof ret);
        q = eng_rec_num(e, ENG_T_LOANS, L_QTY);
        host = (u32)eng_rec_num(e, ENG_T_LOANS, L_HOST);
        k = row_kind(e);
        if (!dbf_is_deleted(eng_dbf(e, ENG_T_LOANS)) && ret[0] == '\0') {
            if (q > 0)
                sum[k] = sum[k] + q > MAXQTY ? MAXQTY : sum[k] + q;
            if (k != 0 && host != 0)
                rc = heal_host(e, host, 1, today);
        }
        if (rc == ENG_OK)
            rc = eng_dbferr(e, ndx_next(x, &r), 1);
    }
    return rc;
}

/* The second pass: every live ITEMS record with ON_LOAN, RESERVED or
   IN_USE other than 0 (a negative one written by another program
   included) that has no LOANS1 entry, and so no group in the first
   pass, gets them 0, and every one whose ATTACHED is other than 0 has its
   hosts counted (heal_host). */
static int heal_items(eng *e, char *today)
{
    ndx *x;
    char key[ENG_IDLEN + 1];
    u32 n;
    u32 r;
    u32 lr;
    i32 id;
    int exact;
    int k;
    int rc;

    x = eng_ndx(e, ENG_X_LOANS1);
    n = eng_reccount(e, ENG_T_ITEMS);
    rc = ENG_OK;
    for (r = 1; r <= n && rc == ENG_OK; r++) {
        rc = eng_rec_read(e, ENG_T_ITEMS, r);
        id = eng_rec_num(e, ENG_T_ITEMS, I_ID);
        if (rc != ENG_OK || dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS)) ||
            id <= 0 || !eng_id_key((u32)id, key))
            continue;
        for (k = 0; k < NKIND && eng_rec_num(e, ENG_T_ITEMS,
                                             (u16)(I_ONLOAN + k)) == 0; k++)
            ;
        lr = 1;
        exact = 1;
        if (k < NKIND)
            rc = eng_dbferr(e, ndx_seek(x, (const u8 *)key, ENG_IDLEN, &lr,
                                        &exact), 1);
        for (k = 0; rc == ENG_OK && (lr == 0 || !exact) && k < NKIND; k++)
            rc = heal_put(e, r, (u32)id, (u16)(I_ONLOAN + k), 0, today);
        if (rc == ENG_OK && eng_rec_num(e, ENG_T_ITEMS, I_ATTACH) != 0)
            rc = heal_host(e, (u32)id, 0, today);
    }
    return rc;
}

int eng_loan_heal(eng *e)
{
    char today[ENG_DATELEN + 1];
    int rc;

    today[0] = '\0';
    rc = heal_loans(e, today);
    if (rc == ENG_OK)
        rc = heal_items(e, today);
    return rc;
}
