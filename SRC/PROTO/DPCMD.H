/*
 * DPCMD.H - the request in progress, the helpers DISPATCH.C shares with
 * the command handlers, and the handlers themselves, one per entry of the
 * command table of DISPATCH.C, grouped by the file that defines them.
 *
 * Kept out of PROTO.H so that only DISPATCH.C and the DP*.C files read
 * these declarations: every other includer of PROTO.H (the transports,
 * the client's link, the tests) is spared the compiler memory, which
 * Watcom C 6.5 runs short of on the largest source files.  Include it
 * after PROTO.H.
 */
#ifndef DPCMD_H
#define DPCMD_H

/* ---------------------------------------------------------------------
 * The request in progress and the helpers (DISPATCH.C).
 * --------------------------------------------------------------------- */
typedef struct proto_call proto_call;
typedef int (*proto_cmd_fn)(proto_call *c);

/* One request in progress, handed to its handler (cmd), which is chosen
   from the command table of DISPATCH.C and admitted by its gate there.
   A handler returns ENG_OK after writing the whole response, or an
   ENG_E code having written nothing; proto_dispatch then writes the
   -ERR line with the field and text from proto_fail, or else from
   eng_errfield and eng_errmsg, or else proto_errtext.  A failure after
   proto_data_begin is written by the handler itself as a last line
   "K ERROR=code" before the terminator.  A handler sets close to end the
   session after the response. */
struct proto_call {
    proto_host *h;
    proto_session *s;
    proto_req *r;
    proto_out *o;
    proto_cmd_fn cmd;
    u16 argi;                   /* first positional after the command   */
    u8  close;
    char errfld[DBF_NAMELEN + 1];   /* copied by proto_fail             */
    const char *errmsg;         /* a static string, proto_errtext's
                                   buffer, or NULL                      */
};

/* Record a handler's own failure and return code; field (NULL for none)
   is copied, at most DBF_NAMELEN characters, msg must be static.  A NULL
   msg records the standard text of code, which holds only until the
   next proto_errtext call. */
int proto_fail(proto_call *c, int code, const char *field, const char *msg);

/* Positional argument i after the command, or NULL. */
const char *proto_arg(const proto_call *c, u16 i);

/* Positional argument i into *val; ENG_EARG naming field if missing. */
int proto_need(proto_call *c, u16 i, const char *field, const char **val);

/* ENG_ESYNTAX ("too many arguments") when a positional argument follows
   the n the command takes. */
int proto_no_more(proto_call *c, u16 n);

/* proto_no_more, then proto_only_keys with keys unless keys is NULL
   (which leaves the pairs to the engine). */
int proto_args_end(proto_call *c, u16 n, const char *keys);

/* The value of the pair key into *val; ENG_EARG ("missing argument")
   naming key when it is absent. */
int proto_need_key(proto_call *c, const char *key, const char **val);

/* Positional argument i as an item ID: decimal digits only (no sign,
   leading zeros allowed) with a value of 1..ENG_MAXID; ENG_EARG naming ID
   otherwise.  proto_need_idf names field instead (CHILD, PARENT, ITEM),
   for any ID of ENG_IDLEN digits (MAINT_ID, LOAN_ID). */
int proto_need_id(proto_call *c, u16 i, u32 *id);
int proto_need_idf(proto_call *c, u16 i, const char *field, u32 *id);

/* The pair key as an ID of the same form into *id: 0 when the pair is
   absent or empty, ENG_EARG ("bad ID") naming key when it is not one. */
int proto_key_id(proto_call *c, const char *key, u32 *id);

/* The flag pair key into *on: 0 when absent or empty, 0 or 1 for the
   values "0" and "1", any other value ENG_EARG naming key. */
int proto_flag(proto_call *c, const char *key, int *on);

/* ENG_EARG naming the first pair whose key is not in the comma list
   keys (e.g. "CAT,LOC,PAGE,AFTER"). */
int proto_only_keys(proto_call *c, const char *keys);

/* The session whose request runs, as the host lists it, into *p: 1, or
   0 in local mode. */
int proto_self(const proto_host *h, proto_peer *p);

/* The pair of ITEM, LOC or MAINT NOTES, at most one of ADD=, NEW=, PART=
   and END=: its value into *text and into *how ENG_NOTE_ADD,
   ENG_NOTE_NEW, ENG_NOTE_PART or ENG_NOTE_PART | ENG_NOTE_END
   (ENGNOTE.H), the last three with the session's slot as their tag,
   except on a session offered login tickets (tkok: the mailbox's, each
   of one request), which is known by station and user alone (tag 0);
   *text NULL, a read, without a pair.  ENG_EARG naming a
   pair of another key ("unexpected key") or the second of two. */
int proto_note(proto_call *c, const char **text, u8 *how);

/* PAGE= of the request: PROTO_PAGEDEF when absent; ENG_EARG naming PAGE
   unless 1..PROTO_PAGEMAX. */
int proto_page_size(proto_call *c, u16 *page);

/* The arguments of a paged list of table t: proto_only_keys with keys,
   PAGE= into *page (proto_page_size), and FIELDS= into idx[0..*n-1]
   (DP_MAXCOLS entries): the fields of eng_fieldlist, with the computed
   columns of t named in the list where they stand, each at most once
   (else ENG_EARG naming it); the default list, with every computed
   column of t, when absent, as it is for a command whose keys leave
   FIELDS out. */
int proto_page_args(proto_call *c, int t, const char *keys, u16 *page,
                    u16 *idx, u16 *n);

/* "+OK" when rc is ENG_OK; returns rc. */
int proto_done(const proto_call *c, int rc);

/* "+OK KEY=n"; returns ENG_OK. */
int proto_ok_num(const proto_call *c, const char *key, u32 n);

/* ---------------------------------------------------------------------
 * Computed columns.
 *
 * A list of field numbers may also hold computed columns of its table,
 * values that are no field of the record: DP_XCOL | k stands for
 * dp_xcols[k] (DPLOC.C), whose text writes the value for the record in
 * the table's buffer into val (ENG_MAXVAL bytes).  It may keep its last
 * value in the rows' xkey, xk and xval, which last one response, while
 * the data cannot change.  The default list of the table has each right
 * after its field after; FIELDS= may name each once.  They add at most
 * DP_NXCOL columns, DP_XWIDTH characters in all, to the fields
 * eng_fieldlist limits, which DPCSV.C counts.  LOC_NAME of ITEMS, after
 * LOC_ID, is the LOC_NAME of the item's location, read with one LOCS1
 * lookup when the column is in the row and the LOC_ID is not that of
 * the row before; blank for LOC_ID 0, and for a LOC_ID that names no
 * live location (data damaged by another program).  AVAIL of ITEMS,
 * after QTY, is the letter of eng_item_avail (ENGITEM.H): Y for an
 * available item, else why it is not.
 * --------------------------------------------------------------------- */
#define DP_XCOL         0x8000u /* an idx entry naming a computed column */
#define DP_NXCOL        2       /* entries of dp_xcols                  */
#define DP_X_LOC_NAME   (DP_XCOL | 0)
#define DP_X_AVAIL      (DP_XCOL | 1)
#define DP_XW_LOC_NAME  ENG_LOCNAMELEN
#define DP_XW_AVAIL     1
#define DP_XWIDTH       (DP_XW_LOC_NAME + DP_XW_AVAIL)  /* summed       */
#define DP_MAXCOLS      (ENG_MAXFLIST + DP_NXCOL)   /* a list's columns */

typedef struct proto_rows proto_rows;

typedef struct {
    u8   t;                     /* ENG_T_ of the records                */
    u8   after;                 /* the field it follows in the default
                                   list                                 */
    const char *name;
    int (*text)(proto_call *c, proto_rows *r, char *val);
} dp_xcol;

extern const dp_xcol dp_xcols[DP_NXCOL];

/* ---------------------------------------------------------------------
 * Lists, records and memos (DISPATCH.C).
 * --------------------------------------------------------------------- */

/* A source of rows for proto_send_rows, for lists that are not one
   eng_scan of one table: walks (ASSY BOM, ASSY LIST, LOC LIST, LOC
   CONTENTS) and lists with a column computed per row or taken from a
   second table (LOAN LIST).  Each row is: the lead column when lead is
   not NULL (its value leadval, set by next), the fields and computed
   columns idx[0..n-1] of table t's buffer, then the fields
   xidx[0..xn-1] of table xt's buffer, sent empty when next set xok to
   0.  The F line names the same columns in the same order.  next loads
   the next record into t's buffer (and xt's) and gives its record
   number, 0 when the source is done or its bounded work is spent; more
   and cursor answer as eng_scan_more and eng_scan_cursor do for the last
   entry the source examined.  arg is the source's own state (an
   eng_scan, an eng_walk, an eng_lwalk). */
typedef int (*proto_next_fn)(proto_call *c, proto_rows *r, u32 *recno);
typedef int (*proto_more_fn)(proto_call *c, proto_rows *r, int *more);
typedef void (*proto_curs_fn)(proto_rows *r, char *out);

#define PROTO_LEADLEN   TXT_U32LEN      /* a lead value: txt_u32 digits */

struct proto_rows {
    int  t;                     /* ENG_T_ of the records                */
    const u16 *idx;             /* their fields                         */
    u16  n;
    const char *lead;           /* lead column name, or NULL            */
    char leadval[PROTO_LEADLEN + 1];
    int  xt;                    /* ENG_T_ of the trailing columns       */
    const u16 *xidx;            /* their fields; xn 0 for none          */
    u16  xn;
    u8   xok;                   /* xt's buffer holds this row's record  */
    proto_next_fn next;
    proto_more_fn more;
    proto_curs_fn cursor;
    void *arg;
    u32  xkey;                  /* a computed column's last key         */
    u8   xk;                    /* its column, DP_NXCOL for none        */
    char xval[DP_XWIDTH + 1];   /* its value for xkey                   */
};

/* Set up r for the fields idx[0..n-1] of table t from the source next,
   more, cursor and arg, without a lead column or trailing columns; the
   caller sets lead and xt, xidx, xn afterwards when it has them. */
void proto_rows_init(proto_rows *r, int t, const u16 *idx, u16 n,
                     proto_next_fn next, proto_more_fn more,
                     proto_curs_fn cursor, void *arg);

/* The F line of r's columns. */
void proto_send_fline(const proto_call *c, const proto_rows *r);

/* Write a paged list from r: +DATA, the F line, at most page R lines
   (fewer when the sink has less than PROTO_ROOMMIN bytes of room or
   next gives 0), a C line when more reports entries left, and the
   terminator.  A failed read or a cut line ends the block with K
   ERROR=code and the terminator, and the code is returned. */
int proto_send_rows(proto_call *c, proto_rows *r, u16 page);

/* "C cursor", the cursor line of a page, unless cursor is "". */
void proto_send_cursor(proto_out *o, const char *cursor);

/* proto_send_rows over the entries of the open scan s, with the fields
   idx[0..n-1] of table t. */
int proto_send_page(proto_call *c, eng_scan *s, int t, const u16 *idx,
                    u16 n, u16 page);

/* +DATA, one "K FIELD=value" line per field of table t's buffer that is
   neither hidden nor memo, in file order, and the terminator.
   proto_send_fields writes only the K lines, into a +DATA block the
   caller has begun and ends; a failure ends the block itself, as
   proto_send_rows does. */
int proto_send_record(proto_call *c, int t);
int proto_send_fields(proto_call *c, int t);

/* +DATA, the len bytes of eng_memobuf as L lines, and the terminator.
   Lines break at CR LF, a lone LF, a lone CR and 8D 0A (the dBASE soft
   return), and once PROTO_MEMOLINE bytes have been written; '\' is
   written as \\ and other bytes below 20 except TAB, and 7F, as \xHH,
   and those written bytes are what PROTO_MEMOLINE counts.  An empty
   memo line is sent as "L" alone.  When the next line would take the
   response past PROTO_MAXUNPAGED (possible only for a memo written by
   another program), the lines stop and "K TRUNCATED=1" is sent before
   the terminator. */
int proto_send_memo(proto_call *c, u16 len);

/* ---------------------------------------------------------------------
 * Fixed columns.
 *
 * The lists with fixed columns, as rows X(table, field) in column order,
 * and C(name) for a computed column: the handlers build their tables of
 * field numbers from them with DP_FNUM and DP_XNUM, and DPCSV.C sums
 * their widths to check at compile time that the rows fit the limits of
 * PROTO.H.  DP_LEVEL, DP_LATE and DP_DEPTH
 * name the lead columns of ASSY PATH, of the loan lists and of LOC LIST.
 * --------------------------------------------------------------------- */
#define DP_FNUM(t, f)   ENG_FIELD(t, f),
#define DP_XNUM(name)   DP_X_##name,
#define DP_LEVEL        "LEVEL"
#define DP_LATE         "LATE"
#define DP_DEPTH        "DEPTH"

#define DP_REPORT_ITEMS(X, C) \
    X(ITEMS, ITEM_ID) X(ITEMS, CATEGORY) X(ITEMS, MFR) X(ITEMS, MODEL) \
    X(ITEMS, SERIALNO) X(ITEMS, COND) X(ITEMS, WORKS) X(ITEMS, QTY) \
    X(ITEMS, LOC_ID) C(LOC_NAME) X(ITEMS, STATUS) X(ITEMS, ACQ_DATE) \
    X(ITEMS, ACQ_PRICE) X(ITEMS, CUR_VALUE)
#define DP_REPORT_LOANS(X) \
    X(LOANS, LOAN_ID) X(LOANS, ITEM_ID) X(LOANS, KIND) X(LOANS, QTY) \
    X(LOANS, HOLDER) X(LOANS, HOST_ID) X(LOANS, CONTACT) \
    X(LOANS, OUT_DATE) X(LOANS, DUE_DATE)
#define DP_LOAN_LIST(X) DP_REPORT_LOANS(X) X(LOANS, RET_DATE)
#define DP_LOAN_ITEM(X) X(ITEMS, MFR) X(ITEMS, MODEL)
#define DP_ASSY_PATH(X, C) \
    X(ITEMS, ITEM_ID) X(ITEMS, CATEGORY) X(ITEMS, MFR) X(ITEMS, MODEL) \
    X(ITEMS, LOC_ID) C(LOC_NAME)
#define DP_LOC_LIST(X) \
    X(LOCS, LOC_ID) X(LOCS, PARENT_ID) X(LOCS, LOC_NAME) X(LOCS, LOC_CODE) \
    X(LOCS, LTYPE) X(LOCS, CAPACITY)
#define DP_CAT_LIST(X)  X(CATS, CAT_CODE) X(CATS, CAT_NAME) X(CATS, SORTORD)
#define DP_LTYPE_LIST(X) \
    X(LTYPES, LT_CODE) X(LTYPES, LT_NAME) X(LTYPES, SORTORD)

/* ---------------------------------------------------------------------
 * The handlers.
 * --------------------------------------------------------------------- */

/* DPSESS.C.  dp_args: ENG_OK if the request has no pairs and at most n
   positional arguments after the command, the pairs checked first, as
   the session commands check them, those of DPTKT.C included. */
int dp_args(proto_call *c, u16 n);
int dp_hello(proto_call *c);
int dp_login(proto_call *c);
int dp_passwd(proto_call *c);
int dp_whoami(proto_call *c);
int dp_bye(proto_call *c);
int dp_ping(proto_call *c);
int dp_ver(proto_call *c);
int dp_status(proto_call *c);

/* DPUSER.C. */
int dp_user_list(proto_call *c);
int dp_user_get(proto_call *c);
int dp_user_add(proto_call *c);
int dp_user_set(proto_call *c);
int dp_user_reset(proto_call *c);
int dp_user_del(proto_call *c);

/* DPITEM.C: the ITEM commands and REPORT ITEMS. */
int dp_item_get(proto_call *c);
int dp_item_add(proto_call *c);
int dp_item_set(proto_call *c);
int dp_item_del(proto_call *c);
int dp_item_undel(proto_call *c);
int dp_item_list(proto_call *c);
int dp_item_find(proto_call *c);
int dp_item_serial(proto_call *c);
int dp_item_notes(proto_call *c);
int dp_item_copy(proto_call *c);
int dp_item_split(proto_call *c);
int dp_item_tags(proto_call *c);
int dp_report_items(proto_call *c);

/* DPLOC.C. */
int dp_loc_get(proto_call *c);
int dp_loc_add(proto_call *c);
int dp_loc_set(proto_call *c);
int dp_loc_del(proto_call *c);
int dp_loc_list(proto_call *c);
int dp_loc_contents(proto_call *c);
int dp_loc_notes(proto_call *c);

/* The items at loc, with all 1 also those below it, in tree order, that
   pass the filters f (eng_loc_contents), from AFTER= of the request, as
   a paged list of the columns idx[0..n-1]: LOC CONTENTS, and ITEM LIST,
   ITEM FIND and REPORT ITEMS with ALL=1. */
int dp_item_walk(proto_call *c, const char *loc, int all,
                 const eng_ifilt *f, const u16 *idx, u16 n, u16 page);
int dp_cat_list(proto_call *c);
int dp_cat_add(proto_call *c);
int dp_cat_set(proto_call *c);
int dp_cat_del(proto_call *c);
int dp_ltype_list(proto_call *c);
int dp_ltype_add(proto_call *c);
int dp_ltype_set(proto_call *c);
int dp_ltype_del(proto_call *c);

/* DPASSY.C. */
int dp_assy_install(proto_call *c);
int dp_assy_remove(proto_call *c);
int dp_assy_move(proto_call *c);
int dp_assy_bom(proto_call *c);
int dp_assy_path(proto_call *c);
int dp_assy_children(proto_call *c);
int dp_assy_list(proto_call *c);

/* DPMAINT.C. */
int dp_maint_add(proto_call *c);
int dp_maint_get(proto_call *c);
int dp_maint_set(proto_call *c);
int dp_maint_del(proto_call *c);
int dp_maint_notes(proto_call *c);
int dp_maint_list(proto_call *c);
int dp_maint_recent(proto_call *c);

/* DPLOAN.C: the LOAN commands and REPORT LOANS.  dp_loan_out serves
   LOAN OUT and LOAN ADD. */
int dp_loan_out(proto_call *c);
int dp_loan_reserve(proto_call *c);
int dp_loan_use(proto_call *c);
int dp_loan_return(proto_call *c);
int dp_loan_get(proto_call *c);
int dp_loan_set(proto_call *c);
int dp_loan_list(proto_call *c);
int dp_loan_overdue(proto_call *c);
int dp_report_loans(proto_call *c);

/* DPCSV.C. */
int dp_export(proto_call *c);
int dp_import(proto_call *c);

/* DPADMIN.C: AUDIT LIST and the ADMIN commands. */
int dp_audit_list(proto_call *c);
int dp_admin_clients(proto_call *c);
int dp_admin_flush(proto_call *c);
int dp_admin_shutdown(proto_call *c);
int dp_admin_pack(proto_call *c);
int dp_admin_reindex(proto_call *c);
int dp_admin_backup(proto_call *c);
int dp_admin_restore(proto_call *c);

#endif
