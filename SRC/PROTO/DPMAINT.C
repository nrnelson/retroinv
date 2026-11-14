/*
 * DPMAINT.C - the MAINT commands (GET LIST RECENT NOTES ADD SET DEL).
 *
 * The record ID of GET, SET, DEL and NOTES and the item ID of ADD and
 * LIST are the first positional argument, checked by proto_need_idf and
 * named ID and ITEM; the engine names the same fields for a record or an
 * item that is not live.  The KEY=value pairs of ADD and SET go to the
 * engine as they are: it refuses keys that are not settable fields of
 * MAINT.DBF and takes NOTES for the DETAIL memo.  The paged commands
 * take FIELDS= (a field list of MAINT, eng_fieldlist), PAGE= and AFTER=.
 * The level the writing forms of MAINT NOTES need is checked by the
 * dispatcher's gate.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGMAINT.H"

static const char k_id[] = "ID";
static const char k_item[] = "ITEM";
/* The keys of the paged commands; the last one is their AFTER= key. */
static const char page_keys[] = "FIELDS,PAGE,AFTER";
#define K_AFTER     (page_keys + sizeof page_keys - sizeof "AFTER")

/* The ID named field at position 0 and the check that nothing else is
   positional; keys is the comma list of the pairs the command accepts,
   or NULL to leave the pairs to the engine or the caller. */
static int maint_id(proto_call *c, const char *field, const char *keys,
                    u32 *id)
{
    int rc;

    rc = proto_need_idf(c, 0, field, id);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    return rc;
}

/* MAINT ADD item MTYPE= SUMMARY= [MDATE=][COST=][DONE_BY=][NOTES=]:
   "+OK ID=n". */
int dp_maint_add(proto_call *c)
{
    u32 item;
    u32 id;
    int rc;

    rc = maint_id(c, k_item, NULL, &item);
    if (rc == ENG_OK)
        rc = eng_maint_add(c->h->e, item, c->r->pair, c->r->npairs, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_ok_num(c, k_id, id);
}

/* MAINT GET id: K lines without the memo. */
int dp_maint_get(proto_call *c)
{
    u32 id;
    u32 recno;
    int rc;

    rc = maint_id(c, k_id, "", &id);
    if (rc == ENG_OK)
        rc = eng_maint_get(c->h->e, id, &recno);
    if (rc != ENG_OK)
        return rc;
    return proto_send_record(c, ENG_T_MAINT);
}

/* MAINT SET id k=v... */
int dp_maint_set(proto_call *c)
{
    u32 id;
    int rc;

    rc = maint_id(c, k_id, NULL, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_maint_set(c->h->e, id, c->r->pair,
                                       c->r->npairs));
}

/* MAINT DEL id. */
int dp_maint_del(proto_call *c)
{
    u32 id;
    int rc;

    rc = maint_id(c, k_id, "", &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_maint_del(c->h->e, id));
}

/* MAINT NOTES id: the DETAIL memo as L lines; with a pair, a write as
   ITEM NOTES. */
int dp_maint_notes(proto_call *c)
{
    const char *text;
    u32 id;
    u16 len;
    u8 how;
    int rc;

    rc = maint_id(c, k_id, NULL, &id);
    if (rc == ENG_OK)
        rc = proto_note(c, &text, &how);
    if (rc != ENG_OK)
        return rc;
    if (text != NULL)
        return proto_done(c, eng_maint_addnote(c->h->e, id, text, how));
    rc = eng_maint_notes(c->h->e, id, &len);
    if (rc != ENG_OK)
        return rc;
    return proto_send_memo(c, len);
}

/* MAINT LIST item [FIELDS=][PAGE=][AFTER=]: the item's records, oldest
   MDATE first. */
int dp_maint_list(proto_call *c)
{
    eng_scan s;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    u32 item;
    int rc;

    rc = maint_id(c, k_item, NULL, &item);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_MAINT, page_keys, &page, idx, &n);
    if (rc == ENG_OK)
        rc = eng_maint_scan(c->h->e, &s, item, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    return proto_send_page(c, &s, ENG_T_MAINT, idx, n, page);
}

/* MAINT RECENT [FIELDS=][PAGE=][AFTER=]: every record, the most
   recently added first. */
int dp_maint_recent(proto_call *c)
{
    eng_scan s;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_MAINT, page_keys, &page, idx, &n);
    if (rc == ENG_OK)
        rc = eng_maint_recent(c->h->e, &s, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    return proto_send_page(c, &s, ENG_T_MAINT, idx, n, page);
}
