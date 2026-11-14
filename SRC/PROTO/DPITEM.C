/*
 * DPITEM.C - the ITEM commands: GET ADD SET DEL UNDEL LIST FIND SERIAL
 * NOTES COPY SPLIT TAGS, and REPORT ITEMS.
 *
 * Item IDs are the first positional argument, checked by proto_need_id.
 * The KEY=value pairs of ADD and SET go to the engine as they are; it
 * refuses keys that are not settable fields of ITEMS.DBF.  The paged
 * commands take FIELDS= (a field list of ITEMS, eng_fieldlist), PAGE=
 * and AFTER=.  The level the writing forms of ITEM NOTES need is checked
 * by the dispatcher's gate.  ITEM LIST, ITEM FIND and REPORT ITEMS take
 * the filters CAT=, LOC= (a location token, ENGLOCS.H), STATUS=, TAG=,
 * WORKS= and AVAIL= (item_filters), and ALL=1 for the items at LOC= and
 * below it (item_rows); REPORT ITEMS is ITEM LIST with the fixed columns
 * of report_cols in place of FIELDS=.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGITEM.H"

/* The keys of ITEM LIST; REPORT ITEMS takes them without FIELDS.  The
   last one is the AVAIL= switch. */
static const char list_keys[] =
    "FIELDS,CAT,LOC,STATUS,TAG,WORKS,ORDER,PAGE,AFTER,ALL,AVAIL";
#define REPORT_KEYS (list_keys + sizeof "FIELDS," - 1)
#define K_AVAIL     (list_keys + sizeof list_keys - sizeof "AVAIL")

/* The keys of ITEM FIND; ITEM SERIAL takes the last three, the last one
   being the AFTER= key of every list, which alone ITEM TAGS takes. */
static const char find_keys[] =
    "CAT,LOC,STATUS,TAG,WORKS,AVAIL,ALL,FIELDS,PAGE,AFTER";
#define PAGE_KEYS   (find_keys + sizeof find_keys - sizeof "FIELDS,PAGE,AFTER")
#define K_AFTER     (find_keys + sizeof find_keys - sizeof "AFTER")

static const char k_id[] = "ID";
static const char k_qty[] = "QTY";
static const char k_loc[] = "LOC";
static const char k_order[] = "ORDER";
static const char k_all[] = "ALL";
static const char k_cat[] = "CAT";
static const char k_status[] = "STATUS";
static const char k_tag[] = "TAG";
static const char k_works[] = "WORKS";

/* The values of ORDER=, word i naming the order i; the last one is the
   SERIAL argument of ITEM SERIAL. */
static const char orders[] = "ID LOC MFR SERIAL";
#define K_SERIAL    (orders + sizeof orders - sizeof "SERIAL")
extern char orders_ok[1];
extern char orders_ok[(ENG_ORD_ID == 0 && ENG_ORD_LOC == 1 &&
                       ENG_ORD_MFR == 2 && ENG_ORD_SERIAL == 3) ? 1 : 2];

/* ORDER=SERIAL, the longest word, with an AFTER= cursor of ITEMS5 (keys
   of ENG_SERLEN) takes no more of a request than ORDER=MFR with one of
   ITEMS4, the longest key, which DPCSV.C counts; ORDER=ID and ORDER=LOC
   have shorter words and keys still. */
extern char orders_fit[1];
extern char orders_fit[(sizeof "SERIAL" + 2 * ENG_SERLEN <=
                        sizeof "MFR" + 2 * ENG_MAXKEYLEN) ? 1 : 2];

/* The columns of REPORT ITEMS. */
static const u16 report_cols[] = { DP_REPORT_ITEMS(DP_FNUM, DP_XNUM) };

/* The item ID and the check that nothing else is positional; keys is
   the comma list of the pairs the command accepts, or NULL to leave the
   pairs to the engine or the caller. */
static int item_id(proto_call *c, const char *keys, u32 *id)
{
    int rc;

    rc = proto_need_id(c, 0, id);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    return rc;
}

/* ITEM GET id. */
int dp_item_get(proto_call *c)
{
    u32 id;
    u32 recno;
    int rc;

    rc = item_id(c, "", &id);
    if (rc == ENG_OK)
        rc = eng_item_get(c->h->e, id, &recno);
    if (rc != ENG_OK)
        return rc;
    return proto_send_record(c, ENG_T_ITEMS);
}

/* ITEM ADD k=v...: "+OK ID=n". */
int dp_item_add(proto_call *c)
{
    u32 id;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = eng_item_add(c->h->e, c->r->pair, c->r->npairs, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_ok_num(c, k_id, id);
}

/* ITEM SET id k=v... */
int dp_item_set(proto_call *c)
{
    u32 id;
    int rc;

    rc = item_id(c, NULL, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_item_set(c->h->e, id, c->r->pair, c->r->npairs));
}

/* ITEM DEL id. */
int dp_item_del(proto_call *c)
{
    u32 id;
    int rc;

    rc = item_id(c, "", &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_item_del(c->h->e, id));
}

/* ITEM UNDEL id. */
int dp_item_undel(proto_call *c)
{
    u32 id;
    int rc;

    rc = item_id(c, "", &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_item_undel(c->h->e, id));
}

/* The filters of the item lists into *f (eng_item_filters): text (NULL
   for none), CAT=, STATUS=, TAG=, WORKS= and the switch of AVAIL=,
   ENG_IF_AVAIL for 1, ENG_IF_UNAVAIL for 0, none when it is absent or
   empty; any other AVAIL= is ENG_EARG naming it. */
static int item_filters(proto_call *c, const char *text, eng_ifilt *f)
{
    const char *v;
    int flags;
    int on;
    int rc;

    rc = proto_flag(c, K_AVAIL, &on);
    if (rc != ENG_OK)
        return rc;
    v = proto_get(c->r, K_AVAIL);
    flags = 0;
    if (on)
        flags = ENG_IF_AVAIL;
    else if (v != NULL && v[0] != '\0')
        flags = ENG_IF_UNAVAIL;
    return eng_item_filters(c->h->e, f, text, proto_get(c->r, k_cat),
                            proto_get(c->r, k_status),
                            proto_get(c->r, k_tag),
                            proto_get(c->r, k_works), flags);
}

/* The rows of ITEM LIST and REPORT ITEMS (text NULL) or ITEM FIND, the
   columns idx[0..n-1], with the filters of item_filters and AFTER=:
   eng_item_scan in the order of ORDER= (ID LOC MFR SERIAL, in any case;
   without it LOC when LOC= is given, not empty, else ID; ITEM FIND,
   whose keys leave ORDER= out, scans in record order), or with ALL=1
   the items at LOC= and at every location below it in tree order
   (dp_item_walk).  There the tree gives the order, so ORDER= is
   refused, and LOC= is needed, a location that names none being
   ENG_ENOTFOUND naming LOC, as without ALL=1. */
static int item_rows(proto_call *c, const char *text, const u16 *idx,
                     u16 n, u16 page)
{
    eng_ifilt f;
    eng_scan s;
    const char *loc;
    const char *ord;
    int order;
    int all;
    int rc;

    loc = proto_get(c->r, k_loc);
    ord = proto_get(c->r, k_order);
    order = (loc != NULL && loc[0] != '\0') ? ENG_ORD_LOC : ENG_ORD_ID;
    rc = proto_flag(c, k_all, &all);
    if (rc == ENG_OK && ord != NULL && !all)
        order = txt_word_num(orders, ord);
    if (rc == ENG_OK && ord != NULL && (all || order < 0))
        rc = proto_fail(c, ENG_EARG, k_order, NULL);
    if (rc == ENG_OK && all)
        rc = proto_need_key(c, k_loc, &loc);
    if (rc == ENG_OK)
        rc = item_filters(c, text, &f);
    if (rc == ENG_OK && !all)
        rc = eng_item_scan(c->h->e, &s, order, &f, loc,
                           proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    if (!all)
        return proto_send_page(c, &s, ENG_T_ITEMS, idx, n, page);
    rc = dp_item_walk(c, loc, 1, &f, idx, n, page);
    if (rc == ENG_ENOTFOUND)
        return eng_fail(c->h->e, rc, k_loc, ENG_TX_NOSUCHLOC);
    return rc;
}

/* ITEM LIST [CAT=][LOC=][STATUS=][TAG=][WORKS=][AVAIL=]
   [ORDER=ID|LOC|MFR|SERIAL][ALL=1][FIELDS=][PAGE=][AFTER=]. */
int dp_item_list(proto_call *c)
{
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, list_keys, &page, idx, &n);
    if (rc != ENG_OK)
        return rc;
    return item_rows(c, NULL, idx, n, page);
}

/* ITEM FIND "text" [CAT=][LOC=][STATUS=][TAG=][WORKS=][AVAIL=][ALL=1]
   [FIELDS=][PAGE=][AFTER=]. */
int dp_item_find(proto_call *c)
{
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    const char *text;
    int rc;

    rc = proto_need(c, 0, "TEXT", &text);
    if (rc == ENG_OK)
        rc = proto_no_more(c, 1);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, find_keys, &page, idx, &n);
    if (rc != ENG_OK)
        return rc;
    return item_rows(c, text, idx, n, page);
}

/* ITEM SERIAL "sn" [FIELDS=][PAGE=][AFTER=]. */
int dp_item_serial(proto_call *c)
{
    eng_scan s;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    const char *sn;
    int rc;

    rc = proto_need(c, 0, K_SERIAL, &sn);
    if (rc == ENG_OK)
        rc = proto_no_more(c, 1);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, PAGE_KEYS, &page, idx, &n);
    if (rc == ENG_OK)
        rc = eng_item_serial(c->h->e, &s, sn, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    return proto_send_page(c, &s, ENG_T_ITEMS, idx, n, page);
}

/* ITEM NOTES id: the memo as L lines.  ITEM NOTES id ADD="text": append
   a line; NEW=, PART= and END= stage the memo and write it whole
   (proto_note). */
int dp_item_notes(proto_call *c)
{
    const char *text;
    u32 id;
    u16 len;
    u8 how;
    int rc;

    rc = item_id(c, NULL, &id);
    if (rc == ENG_OK)
        rc = proto_note(c, &text, &how);
    if (rc != ENG_OK)
        return rc;
    if (text != NULL)
        return proto_done(c, eng_item_addnote(c->h->e, id, text, how));
    rc = eng_item_notes(c->h->e, id, &len);
    if (rc != ENG_OK)
        return rc;
    return proto_send_memo(c, len);
}

/* ITEM COPY id: "+OK ID=n" of the new item. */
int dp_item_copy(proto_call *c)
{
    u32 id;
    u32 newid;
    int rc;

    rc = item_id(c, "", &id);
    if (rc == ENG_OK)
        rc = eng_item_copy(c->h->e, id, &newid);
    if (rc != ENG_OK)
        return rc;
    return proto_ok_num(c, k_id, newid);
}

/* ITEM SPLIT id QTY=n: "+OK ID=n" of the new item, which takes n units
   of id. */
int dp_item_split(proto_call *c)
{
    u32 id;
    u32 newid;
    int rc;

    rc = item_id(c, k_qty, &id);
    if (rc == ENG_OK)
        rc = eng_item_split(c->h->e, id, proto_get(c->r, k_qty), &newid);
    if (rc != ENG_OK)
        return rc;
    return proto_ok_num(c, k_id, newid);
}

/* ITEM TAGS [AFTER=]: +DATA, "F TAG", an R line for each tag of
   eng_item_tags (which never need quotes), its C line while records
   remain, and the terminator. */
int dp_item_tags(proto_call *c)
{
    char cursor[ENG_CURSORMAX];
    const char *t;
    u16 len;
    u16 i;
    int rc;

    rc = proto_args_end(c, 0, K_AFTER);
    if (rc == ENG_OK)
        rc = eng_item_tags(c->h->e, proto_get(c->r, K_AFTER), &len, cursor);
    if (rc != ENG_OK)
        return rc;
    t = eng_memobuf(c->h->e);
    proto_data_begin(c->o);
    proto_line_begin(c->o, "F");
    proto_line_word(c->o, k_tag);
    proto_line_end(c->o);
    for (i = 0; i < len; i = (u16)(i + strlen(t + i) + 1)) {
        proto_line_begin(c->o, "R");
        proto_line_word(c->o, t + i);
        proto_line_end(c->o);
    }
    proto_send_cursor(c->o, cursor);
    proto_data_end(c->o);
    return ENG_OK;
}

/* REPORT ITEMS [CAT=][LOC=][STATUS=][TAG=][WORKS=][AVAIL=]
   [ORDER=ID|LOC|MFR|SERIAL][ALL=1][PAGE=][AFTER=]: the rows of ITEM
   LIST with the columns of report_cols. */
int dp_report_items(proto_call *c)
{
    u16 page;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = proto_only_keys(c, REPORT_KEYS);
    if (rc == ENG_OK)
        rc = proto_page_size(c, &page);
    if (rc != ENG_OK)
        return rc;
    return item_rows(c, NULL, report_cols,
                     (u16)(sizeof report_cols / sizeof report_cols[0]),
                     page);
}
