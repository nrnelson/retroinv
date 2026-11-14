/*
 * DPCSV.C - the EXPORT and IMPORT commands, which write and read CSV
 * files on the machine running the engine (ENGCSV.H).  The dispatcher's
 * gate admits them at ENG_LVL_ADMIN only.
 *
 *   EXPORT table FILE=path [HEADER=0|1] [REPLACE=0|1] [AFTER=cursor]
 *   IMPORT table FILE=path [HEADER=0|1]
 *
 * The table is the one positional argument; any other key, or a flag
 * other than 0 or 1, is refused naming it.  The engine checks the table
 * and the path (eng_csv_table, eng_csv_path) and names TABLE or FILE.
 *
 * EXPORT answers one step of the export: +DATA, "K ROWS=n" (the records
 * this request wrote), "C cursor" while records remain, and the
 * terminator; the client repeats the request with AFTER=cursor, the
 * same path and the same flags until no C line comes, and the file then
 * has its name.  Every request gives the session's slot, as the host
 * lists it, or 0 in local mode, as the export's tag (ENGCSV.H): the
 * exports of two sessions never share a scratch file, and a
 * continuation from another session finds none (ENG_ECURSOR).
 *
 * IMPORT needs the system to itself (the gate answers ENG_EBUSY while
 * other sessions are open), since its work is not bounded.  It answers
 * "+OK ADDED=n" when every line was added.  When lines were refused,
 * nothing was added and the answer is +DATA, "F LINE FIELD CODE TEXT",
 * one R line per refused line up to ENG_IMPERRS (the line number in the
 * file, the field or LINE for the line as a whole, the error code and
 * its text), "K ERRORS=n" counting every refused line, and the
 * terminator.  When adding failed after some records were written, the
 * answer is +DATA, "K ADDED=n" and "K ERROR=code"; a failure before any
 * record was written is an ordinary -ERR line.
 *
 * The file also holds the compile-time checks of the protocol's line
 * and response limits (at its end).
 */
#include <stddef.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENGFLD_WIDTHS
#include "ENGFLD.H"
#include "ENGCSV.H"
#include "PROTO.H"
#include "DPCMD.H"

/* The keys of EXPORT; IMPORT takes the list from FILE on.  The last one
   is HEADER, the flag both take. */
static const char csv_keys[] = "REPLACE,AFTER,FILE,HEADER";
#define IMPORT_KEYS (csv_keys + sizeof "REPLACE,AFTER," - 1)
#define K_HEADER    (csv_keys + sizeof csv_keys - sizeof "HEADER")

static const char k_table[] = "TABLE";
static const char k_file[] = "FILE";
static const char k_replace[] = "REPLACE";
static const char k_after[] = "AFTER";
static const char k_cols[] = "LINE FIELD CODE TEXT";
static const char k_line[] = "LINE";
static const char k_added[] = "ADDED";

/* The flag key (proto_flag) into *flags as bit: absent or 0 leaves it
   clear, 1 sets it. */
static int csv_flag(proto_call *c, const char *key, u8 bit, u8 *flags)
{
    int on;
    int rc;

    rc = proto_flag(c, key, &on);
    if (on)
        *flags = (u8)(*flags | bit);
    return rc;
}

/* The arguments both commands share: the table (with import 1, one that
   may be imported) into *t, nothing else positional, no key outside the
   comma list keys, FILE= into *path and HEADER= into *flags. */
static int csv_args(proto_call *c, const char *keys, int import, int *t,
                    const char **path, u8 *flags)
{
    const char *name;
    int rc;

    *flags = 0;
    *path = proto_get(c->r, k_file);
    rc = proto_need(c, 0, k_table, &name);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    if (rc == ENG_OK)
        rc = eng_csv_table(c->h->e, name, import, t);
    if (rc == ENG_OK)
        rc = eng_csv_path(c->h->e, *path);
    if (rc == ENG_OK)
        rc = csv_flag(c, K_HEADER, ENG_CSV_HEADER, flags);
    return rc;
}

/* The tag of the session's exports: the slot the host lists for it
   (self), or 0 in local mode. */
static u8 csv_tag(const proto_host *h)
{
    proto_peer p;

    return proto_self(h, &p) ? p.slot : 0;
}

/* EXPORT table FILE=path [HEADER=] [REPLACE=] [AFTER=]: one step. */
int dp_export(proto_call *c)
{
    char cursor[ENG_CURSORMAX];
    const char *path;
    u16 rows;
    u8 flags;
    int t;
    int rc;

    rc = csv_args(c, csv_keys, 0, &t, &path, &flags);
    if (rc == ENG_OK)
        rc = csv_flag(c, k_replace, ENG_CSV_REPLACE, &flags);
    if (rc == ENG_OK)
        rc = eng_export(c->h->e, t, path, flags, csv_tag(c->h),
                        proto_get(c->r, k_after), &rows, cursor);
    if (rc != ENG_OK)
        return rc;
    proto_data_begin(c->o);
    proto_k_num(c->o, "ROWS", rows);
    proto_send_cursor(c->o, cursor);
    proto_data_end(c->o);
    return ENG_OK;
}

/* The state of an import's answer: begun once +DATA and the F line of
   the refused lines are written. */
typedef struct {
    proto_out *o;
    int begun;
} csv_answer;

/* eng_csverr_fn of IMPORT: one R line per refused line, after +DATA and
   the F line on the first.  The line is never cut, so it needs no check
   of the writer's cut flag: a line number, a field name of at most
   DBF_NAMELEN, a code and a text of at most ENG_TXMAX stay far within
   PROTO_MAXRESP. */
static void csv_refused(void *arg, u32 line, const char *field, int code,
                        const char *msg)
{
    csv_answer *a;

    a = (csv_answer *)arg;
    if (!a->begun) {
        proto_data_begin(a->o);
        proto_line_begin(a->o, "F");
        proto_line_word(a->o, k_cols);
        proto_line_end(a->o);
        a->begun = 1;
    }
    proto_line_begin(a->o, "R");
    proto_line_unum(a->o, line);
    proto_line_value(a->o, field[0] != '\0' ? field : k_line);
    proto_line_unum(a->o, (u32)code);
    proto_line_value(a->o, msg[0] != '\0' ? msg : proto_errtext(code));
    proto_line_end(a->o);
}

/* IMPORT table FILE=path [HEADER=]: the whole file in one request. */
int dp_import(proto_call *c)
{
    csv_answer a;
    const char *path;
    u32 added;
    u16 errors;
    u8 flags;
    int t;
    int rc;

    rc = csv_args(c, IMPORT_KEYS, 1, &t, &path, &flags);
    if (rc != ENG_OK)
        return rc;
    a.o = c->o;
    a.begun = 0;
    rc = eng_import(c->h->e, t, path, flags, csv_refused, &a, &errors,
                    &added);
    if (!a.begun && rc == ENG_OK)
        return proto_ok_num(c, k_added, added);
    if (!a.begun && added == 0)
        return rc;
    if (a.begun) {
        proto_k_num(c->o, "ERRORS", errors);
    } else {
        proto_data_begin(c->o);
        proto_k_num(c->o, k_added, added);
    }
    if (rc != ENG_OK) {
        proto_err(c->o, rc, NULL, NULL);
        return rc;
    }
    proto_data_end(c->o);
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * The limits of PROTO.H, checked at compile time: the longest requests
 * against PROTO_MAXLINE, the R lines of fixed columns against
 * PROTO_MAXRESP, the unpaged lists against PROTO_MAXUNPAGED, the
 * refused lines of IMPORT against PROTO_IMPERRS and the session slots
 * against the export tags.  They are kept here, away from DISPATCH.C,
 * whose compile is the closest to Watcom C 6.5's memory limit; the
 * checks of ADMIN CLIENTS and of LOC GET's PATH line live in DPADMIN.C
 * and DPLOC.C, beside what they check.  Field widths and key lengths
 * are the ENG_FW_ and ENG_KL_ constants of ENGFLD.H.
 * --------------------------------------------------------------------- */

/* Bytes of a literal; of " KEY=" and n bytes; of a value of n bytes
   quoted, every byte counted as a two-byte escape; of " AFTER=" and the
   cursor of an index with keys of n bytes, laid out as for
   ENG_CURSORLEN; and of " AFTER=" and a record-order cursor, whose
   index name is a table name. */
#define ML_LIT(s)       (sizeof s - 1)
#define ML_PAIR(s, n)   (ML_LIT(" " s "=") + (n))
#define ML_QUOTED(n)    (2 + 2 * (n))
#define ML_AFTER(n)     ML_PAIR("AFTER", ENG_IXNAMELEN + 1 + 2 * (n) + 1 + 8)
#define ML_RCURSOR      (ENG_TBLNAMELEN + 1 + 1 + 8)
#define ML_RAFTER       ML_PAIR("AFTER", ML_RCURSOR)

/* PAGE= with two digits, FIELDS= at its longest, the filters of the
   item lists, and their ORDER= with the AFTER= cursor of its index at
   the longest the two take together: ORDER=MFR, whose index has the
   longest key (the other orders take less, DPITEM.C checks; ALL=1,
   refused beside ORDER=, has the short ITEMS2 cursor, which ITEM FIND
   takes with ALL=1 in place of its record-order one).  AVAIL= is
   refused beside STATUS=, so the longer of the two stands for both.  A
   location is at most ENG_LOCLEN characters (DPLOC.C checks); TAG= is
   counted bare, its tags joined by commas. */
#define ML_PAGE         ML_PAIR("PAGE", 2)
#define ML_FIELDS       ML_PAIR("FIELDS", ENG_MAXFSPEC)
#define ML_FILTER       (ML_PAIR("CAT", ENG_CATLEN) + \
                         ML_PAIR("LOC", ENG_LOCLEN) + \
                         ML_PAIR("STATUS", ENG_FW_ITEMS_STATUS) + \
                         ML_PAIR("TAG", ENG_TAGFILT) + \
                         ML_PAIR("WORKS", ENG_NWORKS))
#define ML_ORDERED      (ML_PAIR("ORDER", ML_LIT("MFR")) + \
                         ML_AFTER(ENG_MAXKEYLEN))

extern char fits_avail[1];
extern char fits_avail[(ML_PAIR("AVAIL", 1) <=
                        ML_PAIR("STATUS", ENG_FW_ITEMS_STATUS)) ? 1 : 2];

extern char fits_cursor[1];
extern char fits_cursor[(ML_AFTER(ENG_MAXKEYLEN) ==
                         ML_PAIR("AFTER", ENG_CURSORLEN)) ? 1 : 2];
extern char fits_page[1];
extern char fits_page[(PROTO_PAGEMAX <= 99) ? 1 : 2];
extern char fits_item_list[1];
extern char fits_item_list[(ML_LIT("ITEM LIST") + ML_FILTER + ML_ORDERED +
                            ML_PAGE + ML_FIELDS <= PROTO_MAXLINE) ? 1 : 2];
extern char fits_item_find[1];
extern char fits_item_find[(ML_LIT("ITEM FIND ") + ML_QUOTED(ENG_MAXFIND) +
                            ML_FILTER + ML_PAIR("ALL", 1) + ML_FIELDS +
                            ML_PAGE + ML_AFTER(ENG_KL_ITEMS2) <=
                            PROTO_MAXLINE &&
                            ML_RAFTER <= ML_AFTER(ENG_KL_ITEMS2)) ? 1 : 2];
extern char fits_item_serial[1];
extern char fits_item_serial[(ML_LIT("ITEM SERIAL ") +
                              ML_QUOTED(ENG_FW_ITEMS_SERIALNO) + ML_FIELDS +
                              ML_PAGE + ML_AFTER(ENG_KL_ITEMS5) <=
                              PROTO_MAXLINE) ? 1 : 2];
extern char fits_assy_bom[1];
extern char fits_assy_bom[(ML_LIT("ASSY BOM ") + ENG_IDLEN + ML_FIELDS +
                           ML_PAGE + ML_AFTER(ENG_KL_ITEMS3) <=
                           PROTO_MAXLINE) ? 1 : 2];
extern char fits_loan_list[1];
extern char fits_loan_list[(ML_LIT("LOAN LIST") +
                            ML_PAIR("ITEM", ENG_IDLEN) +
                            ML_PAIR("HOST", ENG_IDLEN) + ML_PAIR("KIND", 1) +
                            ML_PAIR("OPEN", 1) + ML_PAGE +
                            ML_AFTER(ENG_KL_LOANS2) <=
                            PROTO_MAXLINE) ? 1 : 2];
extern char fits_loan_overdue[1];
extern char fits_loan_overdue[(ML_LIT("LOAN OVERDUE") +
                               ML_PAIR("ASOF", ENG_DATELEN) + ML_PAGE +
                               ML_AFTER(ENG_KL_LOANS2) <=
                               PROTO_MAXLINE) ? 1 : 2];
extern char fits_report_items[1];
extern char fits_report_items[(ML_LIT("REPORT ITEMS") + ML_FILTER +
                               ML_ORDERED + ML_PAGE <=
                               PROTO_MAXLINE) ? 1 : 2];
extern char fits_report_loans[1];
extern char fits_report_loans[(ML_LIT("REPORT LOANS") +
                               ML_PAIR("OVERDUE", 1) +
                               ML_PAIR("ASOF", ENG_DATELEN) + ML_PAGE +
                               ML_AFTER(ENG_KL_LOANS2) <=
                               PROTO_MAXLINE) ? 1 : 2];
extern char fits_export[1];
extern char fits_export[(ML_LIT("EXPORT ") + ENG_TBLNAMELEN +
                         ML_PAIR("FILE", ML_QUOTED(ENG_CSVPATH)) +
                         ML_PAIR("HEADER", 1) + ML_PAIR("REPLACE", 1) +
                         ML_RAFTER <= PROTO_MAXLINE) ? 1 : 2];
extern char fits_audit_list[1];
extern char fits_audit_list[(ML_LIT("AUDIT LIST") +
                             ML_PAIR("TBL", ENG_TBLNAMELEN) +
                             ML_PAIR("REC", ENG_IDLEN) +
                             ML_PAIR("STATION", ML_QUOTED(ENG_STALEN)) +
                             ML_FIELDS + ML_PAGE +
                             ML_AFTER(ENG_KL_AUDIT1) <=
                             PROTO_MAXLINE) ? 1 : 2];
extern char fits_admin_dir[1];
extern char fits_admin_dir[(ML_LIT("ADMIN RESTORE") +
                            ML_PAIR("DIR", ML_QUOTED(ENG_MAXDIR)) <=
                            PROTO_MAXLINE) ? 1 : 2];

/* The count, the summed width and the F-line words of the fixed column
   lists of DPCMD.H, used as their X, and ML_XONE, ML_XWIDTH and ML_XWORD
   as their C; ML_RLINE is the bytes of an R line of n columns of w bytes
   in all, CR LF excluded, and a lead column counts as one of
   PROTO_LEADLEN bytes.  A FIELDS= list holds at most DP_MAXCOLS columns,
   its computed ones adding at most DP_XWIDTH to the width eng_fieldlist
   allows. */
#define ML_ONE(t, f)    + 1
#define ML_WIDTH(t, f)  + ENG_FW_##t##_##f
#define ML_XONE(name)   + 1
#define ML_XWIDTH(name) + DP_XW_##name
#define ML_XWORD(name)  " " #name
#define ML_WORD(t, f)   " " #f
#define ML_RLINE(n, w)  (1 + 3 * (n) + 2 * (w))

extern char fits_fields_row[1];
extern char fits_fields_row[(ML_RLINE(DP_MAXCOLS,
                                      ENG_MAXRWIDTH + DP_XWIDTH) <=
                             PROTO_MAXRESP) ? 1 : 2];
extern char fits_report_items_row[1];
extern char fits_report_items_row[(ML_RLINE(0 DP_REPORT_ITEMS(ML_ONE,
                                                              ML_XONE),
                                            0 DP_REPORT_ITEMS(ML_WIDTH,
                                                              ML_XWIDTH)) <=
                                   PROTO_MAXRESP) ? 1 : 2];
extern char fits_loc_list_row[1];
extern char fits_loc_list_row[(ML_RLINE(1 DP_LOC_LIST(ML_ONE),
                                        PROTO_LEADLEN
                                        DP_LOC_LIST(ML_WIDTH)) <=
                               PROTO_MAXRESP) ? 1 : 2];
/* LOAN LIST's rows, whose columns begin with those of REPORT LOANS. */
extern char fits_loan_list_row[1];
extern char fits_loan_list_row[(ML_RLINE(1 DP_LOAN_LIST(ML_ONE)
                                         DP_LOAN_ITEM(ML_ONE),
                                         PROTO_LEADLEN
                                         DP_LOAN_LIST(ML_WIDTH)
                                         DP_LOAN_ITEM(ML_WIDTH)) <=
                                PROTO_MAXRESP) ? 1 : 2];

/* "+DATA" and the terminator, with their CR LF. */
#define ML_FRAME        (ML_LIT("+DATA\r\n") + ML_LIT(".\r\n"))

extern char fits_assy_path[1];
extern char fits_assy_path[(ML_FRAME +
                            ML_LIT("F " DP_LEVEL
                                   DP_ASSY_PATH(ML_WORD, ML_XWORD)
                                   "\r\n") +
                            (ENG_MAXDEPTH - 1) *
                            (ML_RLINE(1 DP_ASSY_PATH(ML_ONE, ML_XONE),
                                      PROTO_LEADLEN
                                      DP_ASSY_PATH(ML_WIDTH,
                                                   ML_XWIDTH)) + 2) <=
                            PROTO_MAXUNPAGED) ? 1 : 2];
extern char fits_cat_list[1];
extern char fits_cat_list[(ML_FRAME +
                           ML_LIT("F" DP_CAT_LIST(ML_WORD) "\r\n") +
                           ENG_MAXCODES *
                           (ML_RLINE(0 DP_CAT_LIST(ML_ONE),
                                     0 DP_CAT_LIST(ML_WIDTH)) + 2) <=
                           PROTO_MAXUNPAGED) ? 1 : 2];
/* ITEM TAGS: the R line of a tag of n characters ("R ", the tag, CR LF)
   takes n + 4 bytes for the n + 1 the tag takes of the set of
   eng_item_tags, at most 5/2 as many; then the C line of a record-order
   cursor. */
extern char fits_item_tags[1];
extern char fits_item_tags[(ML_FRAME + ML_LIT("F TAG\r\n") +
                            5 * ENG_TAGSET / 2 + ML_LIT("C \r\n") +
                            ML_RCURSOR <= PROTO_MAXUNPAGED) ? 1 : 2];
extern char fits_ltype_list[1];
extern char fits_ltype_list[(ML_FRAME +
                             ML_LIT("F" DP_LTYPE_LIST(ML_WORD) "\r\n") +
                             ENG_MAXCODES *
                             (ML_RLINE(0 DP_LTYPE_LIST(ML_ONE),
                                       0 DP_LTYPE_LIST(ML_WIDTH)) + 2) <=
                             PROTO_MAXUNPAGED) ? 1 : 2];

/* A session's slot, at most PROTO_MAXPEERS, is an export's tag. */
extern char fits_tag[1];
extern char fits_tag[(PROTO_MAXPEERS < ENG_EXPTAGS) ? 1 : 2];

/* The engine reports as many refused lines as an IMPORT answer lists. */
extern char same_imperrs[1];
extern char same_imperrs[(ENG_IMPERRS == PROTO_IMPERRS) ? 1 : 2];
