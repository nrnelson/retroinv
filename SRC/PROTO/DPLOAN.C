/*
 * DPLOAN.C - the LOAN commands (GET LIST OVERDUE OUT ADD RESERVE USE
 * RETURN SET) and REPORT LOANS: loans, reservations and in-use
 * assignments, the rows of LOANS.DBF; dp_loan_out serves LOAN OUT and
 * LOAN ADD.
 *
 * The row ID of GET, RETURN and SET and the item ID of OUT, RESERVE and
 * USE are the first positional argument, checked by proto_need_idf and
 * named ID and ITEM; the engine names the same fields for a row or an
 * item it does not find.  The pairs of OUT, RESERVE, USE and SET go to
 * the engine, which takes DUE for DUE_DATE, DATE for OUT_DATE and HOST
 * for HOST_ID and answers "field not settable" naming any key that is
 * not a field the command changes for the row's kind.  Here only the
 * keys of the comma list loan_keys pass (every LOANS field and the
 * aliases for SET; the others take its suffixes, below); any other key
 * is refused before the engine is called.
 *
 * LOAN LIST, LOAN OVERDUE and REPORT LOANS have fixed columns: the lead
 * column LATE (1 for an open row due before today, or before ASOF for
 * OVERDUE and REPORT LOANS), the LOANS fields of DP_LOAN_LIST
 * (DP_REPORT_LOANS, the same without RET_DATE, for REPORT LOANS) and
 * the ITEMS fields of DP_LOAN_ITEM of the row's item, sent empty when
 * the item is no longer live.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGFLD.H"
#include "ENGLOAN.H"

/* The keys of LOAN SET; LOAN OUT, RESERVE and USE take the list from QTY
   on and LOAN RETURN from RET_DATE on (NEW_KEYS and RET_KEYS below).
   The engine refuses the fields among them that a command does not
   change. */
static const char loan_keys[] =
    "LOAN_ID,ITEM_ID,KIND,QTY,HOLDER,HOST_ID,HOST,CONTACT,OUT_DATE,"
    "DUE_DATE,DUE,NOTES,RET_DATE,DATE";
#define NEW_KEYS    (loan_keys + sizeof "LOAN_ID,ITEM_ID,KIND," - 1)
#define RET_KEYS    (loan_keys + sizeof loan_keys - sizeof "RET_DATE,DATE")
#define K_DATE      (loan_keys + sizeof loan_keys - sizeof "DATE")

static const char k_id[] = "ID";
static const char k_item[] = "ITEM";
static const char k_ret[] = "RET_DATE";
static const char k_open[] = "OPEN";
static const char k_asof[] = "ASOF";
static const char k_host[] = "HOST";
static const char k_kind[] = "KIND";
static const char k_holder[] = "HOLDER";

/* The keys of LOAN LIST; the last one is also the AFTER= key of every
   loan list.  REPORT LOANS takes report_keys, LOAN OVERDUE the same list
   from ASOF on. */
static const char list_keys[] = "ITEM,HOST,KIND,HOLDER,OPEN,PAGE,AFTER";
#define K_AFTER     (list_keys + sizeof list_keys - sizeof "AFTER")
static const char report_keys[] = "OVERDUE,ASOF,PAGE,AFTER";
#define OVER_KEYS   (report_keys + sizeof "OVERDUE," - 1)
static const char k_overdue[] = "OVERDUE";

/* The columns of the loan lists: REPORT LOANS takes all but the last. */
static const u16 loan_cols[] = { DP_LOAN_LIST(DP_FNUM) };
#define LOAN_NCOLS  ((u16)(sizeof loan_cols / sizeof loan_cols[0]))
#define REPORT_NCOLS ((u16)(LOAN_NCOLS - 1))
static const u16 item_cols[] = { DP_LOAN_ITEM(DP_FNUM) };

/* The ID named field at position 0, nothing else positional, and only
   the pairs of the comma list keys. */
static int loan_id(proto_call *c, const char *field, const char *keys,
                   u32 *id)
{
    int rc;

    rc = proto_need_idf(c, 0, field, id);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    return rc;
}

/* A new row of kind (ENG_K_) of the item at position 0: "+OK ID=n". */
static int new_row(proto_call *c, int kind)
{
    u32 item;
    u32 id;
    int rc;

    rc = loan_id(c, k_item, NEW_KEYS, &item);
    if (rc == ENG_OK)
        rc = eng_loan_out(c->h->e, item, kind, c->r->pair, c->r->npairs,
                          &id);
    if (rc != ENG_OK)
        return rc;
    return proto_ok_num(c, k_id, id);
}

/* LOAN OUT item HOLDER= DUE= [QTY=][CONTACT=][DATE=][NOTES=], also LOAN
   ADD. */
int dp_loan_out(proto_call *c)
{
    return new_row(c, ENG_K_LOAN);
}

/* LOAN RESERVE item [HOLDER=][HOST=][DUE=][QTY=][CONTACT=][DATE=]
   [NOTES=]. */
int dp_loan_reserve(proto_call *c)
{
    return new_row(c, ENG_K_RESERVE);
}

/* LOAN USE item [HOLDER=][HOST=][QTY=][CONTACT=][DATE=][NOTES=]. */
int dp_loan_use(proto_call *c)
{
    return new_row(c, ENG_K_USE);
}

/* LOAN RETURN id [DATE=]: RET_DATE= is the same key; giving both is
   ENG_EARG naming RET_DATE, as the engine answers a field and its alias
   given together. */
int dp_loan_return(proto_call *c)
{
    const char *date;
    const char *alias;
    u32 id;
    int rc;

    rc = loan_id(c, k_id, RET_KEYS, &id);
    if (rc != ENG_OK)
        return rc;
    date = proto_get(c->r, k_ret);
    alias = proto_get(c->r, K_DATE);
    if (date != NULL && alias != NULL)
        return proto_fail(c, ENG_EARG, k_ret, NULL);
    return proto_done(c, eng_loan_return(c->h->e, id,
                                         date != NULL ? date : alias));
}

/* LOAN GET id: K lines of the row. */
int dp_loan_get(proto_call *c)
{
    u32 id;
    u32 recno;
    int rc;

    rc = loan_id(c, k_id, "", &id);
    if (rc == ENG_OK)
        rc = eng_loan_get(c->h->e, id, &recno);
    if (rc != ENG_OK)
        return rc;
    return proto_send_record(c, ENG_T_LOANS);
}

/* LOAN SET id [HOLDER=][HOST=][CONTACT=][DUE=][NOTES=]. */
int dp_loan_set(proto_call *c)
{
    u32 id;
    int rc;

    rc = loan_id(c, k_id, loan_keys, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_loan_set(c->h->e, id, c->r->pair, c->r->npairs));
}

/* The row source of the loan lists: the scan and the date LATE is
   judged against (NULL for today). */
typedef struct {
    eng_scan s;
    const char *asof;
} loan_src;

static int loan_next(proto_call *c, proto_rows *r, u32 *recno)
{
    loan_src *ls;
    eng *e;
    u32 irec;
    int rc;

    ls = (loan_src *)r->arg;
    e = c->h->e;
    rc = eng_scan_next(e, &ls->s, recno);
    if (rc != ENG_OK || *recno == 0)
        return rc;
    r->leadval[0] = (char)(eng_loan_late(e, ls->asof) ? '1' : '0');
    r->leadval[1] = '\0';
    rc = eng_loan_item(e, &irec);
    r->xok = irec != 0;
    return rc;
}

static int loan_more(proto_call *c, proto_rows *r, int *more)
{
    return eng_scan_more(c->h->e, &((loan_src *)r->arg)->s, more);
}

static void loan_cursor(proto_rows *r, char *out)
{
    eng_scan_cursor(&((const loan_src *)r->arg)->s, out);
}

/* Send a page of the scan ls with the LOANS fields cols[0..n-1]
   between LATE and item_cols. */
static int loan_page(proto_call *c, loan_src *ls, const u16 *cols, u16 n,
                     u16 page)
{
    proto_rows r;

    proto_rows_init(&r, ENG_T_LOANS, cols, n, loan_next, loan_more,
                    loan_cursor, ls);
    r.lead = DP_LATE;
    r.xt = ENG_T_ITEMS;
    r.xidx = item_cols;
    r.xn = (u16)(sizeof item_cols / sizeof item_cols[0]);
    return proto_send_rows(c, &r, page);
}

/* The arguments of the loan lists: no positional ones, the pairs of the
   comma list keys and the page size. */
static int loan_list_args(proto_call *c, const char *keys, u16 *page)
{
    int rc;

    rc = proto_args_end(c, 0, keys);
    if (rc == ENG_OK)
        rc = proto_page_size(c, page);
    return rc;
}

/* LOAN LIST [ITEM=][HOST=][KIND=L|R|U][HOLDER=][OPEN=0|1][PAGE=][AFTER=]:
   KIND= empty or absent keeps every kind, any other value is ENG_EARG
   naming it. */
int dp_loan_list(proto_call *c)
{
    loan_src ls;
    const char *kv;
    u32 item;
    u32 host;
    u16 page;
    int kind;
    int open;
    int rc;

    rc = loan_list_args(c, list_keys, &page);
    if (rc == ENG_OK)
        rc = proto_key_id(c, k_item, &item);
    if (rc == ENG_OK)
        rc = proto_key_id(c, k_host, &host);
    if (rc == ENG_OK)
        rc = proto_flag(c, k_open, &open);
    if (rc != ENG_OK)
        return rc;
    kind = 0;
    kv = proto_get(c->r, k_kind);
    if (kv != NULL && kv[0] != '\0') {
        kind = kv[0];
        kind = kv[1] == '\0' ? TXT_UPPER(kind) : 0;
        if (kind != ENG_K_LOAN && kind != ENG_K_RESERVE && kind != ENG_K_USE)
            return proto_fail(c, ENG_EARG, k_kind, NULL);
    }
    rc = eng_loan_scan(c->h->e, &ls.s, item, host, kind,
                       proto_get(c->r, k_holder), open,
                       proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    ls.asof = NULL;
    return loan_page(c, &ls, loan_cols, LOAN_NCOLS, page);
}

/* LOAN OVERDUE [ASOF=][PAGE=][AFTER=]. */
int dp_loan_overdue(proto_call *c)
{
    loan_src ls;
    u16 page;
    int rc;

    rc = loan_list_args(c, OVER_KEYS, &page);
    if (rc != ENG_OK)
        return rc;
    ls.asof = proto_get(c->r, k_asof);
    rc = eng_loan_overdue(c->h->e, &ls.s, ls.asof, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    return loan_page(c, &ls, loan_cols, LOAN_NCOLS, page);
}

/* REPORT LOANS [OVERDUE=0|1][ASOF=][PAGE=][AFTER=]: the open rows by
   DUE_DATE, with OVERDUE=1 only those due before ASOF, LATE judged
   against ASOF in both forms.  ASOF is checked in both (ENG_EARG
   naming it), before the cursor; both scans take the same LOANS2
   cursors. */
int dp_report_loans(proto_call *c)
{
    loan_src ls;
    const char *after;
    u16 page;
    int overdue;
    int rc;

    rc = loan_list_args(c, report_keys, &page);
    if (rc == ENG_OK)
        rc = proto_flag(c, k_overdue, &overdue);
    if (rc != ENG_OK)
        return rc;
    ls.asof = proto_get(c->r, k_asof);
    after = proto_get(c->r, K_AFTER);
    if (overdue) {
        rc = eng_loan_overdue(c->h->e, &ls.s, ls.asof, after);
    } else {
        rc = eng_loan_asof_ok(c->h->e, ls.asof);
        if (rc == ENG_OK)
            rc = eng_loan_scan(c->h->e, &ls.s, 0, 0, 0, NULL, 1, after);
    }
    if (rc != ENG_OK)
        return rc;
    return loan_page(c, &ls, loan_cols, REPORT_NCOLS, page);
}
