/*
 * T_LOAN.C - unit test for loans and the ON_LOAN upkeep (LOANS.C), run
 * through a complete engine.
 *
 * usage: T_LOAN [dir]
 *
 * dir (default the current directory) is a directory, or a prefix ending
 * in '\', '/' or ':', that holds the scratch data directory (tfs_set_dir
 * of T_FILES.H); every engine file there is removed before the run and
 * at the end.  The clock is fixed at 8 October 2026, and at the next day
 * for the repair.
 * Checks:
 *   - LOAN OUT: defaults, the DUE and DATE keys, an empty DATE (today),
 *     a key given twice, unknown and unsettable keys, QTY below 1 and
 *     above what is on hand, missing HOLDER and DUE_DATE, DUE_DATE
 *     before OUT_DATE,
 *     items that are missing, installed or disposed; the I and O rows,
 *     and nothing written (no marker left) by a refused one;
 *   - the item rules while on loan: ITEM SET STATUS=D and QTY below
 *     ON_LOAN, and ITEM DEL, are refused;
 *   - LOAN RETURN: default date, a date before OUT_DATE or invalid, a
 *     second return, the U and R rows, ON_LOAN never below 0, and an item
 *     deleted meanwhile left alone; after every OUT and RETURN each
 *     item's ON_LOAN equals the QTY of its open loans;
 *   - LOAN SET: the settable keys, blank HOLDER or DUE_DATE, the keys
 *     that are not settable, DUE_DATE before OUT_DATE, and no marker
 *     left by a refused one;
 *   - LOAN LIST in LOANS2 and LOANS1 order, open only or all; LOAN
 *     OVERDUE as of a date, with loans due the day before, on the day
 *     and after, a blank due date and returned loans; the bound ends the
 *     scan at ASOF, so no cursor is given past it; paging by cursor;
 *     the LATE flag; a bad ASOF, alone too (eng_loan_asof_ok);
 *   - eng_loan_get over many records with deleted ones and gaps;
 *   - the repair at open: a directory damaged by hand (ON_LOAN left
 *     below its open loans, as a LOAN OUT whose item's commit failed
 *     leaves it, and above them, as such a LOAN RETURN does;
 *     an item without loans with ON_LOAN set; open loans above the
 *     item's QTY) and the marker naming LOANS such a change leaves;
 *     eng_open repairs ON_LOAN with O and R rows by SYSTEM and UPDATED
 *     stamped and removes the marker; eng_loan_heal on the repaired
 *     directory changes nothing;
 *   - LOAN RESERVE and LOAN USE: HOLDER or HOST_ID required, DUE_DATE
 *     only where the kind has one, the host's rules (no such item,
 *     disposed, the item itself, one of its parts), an installed or
 *     disposed item, QTY above what is on hand, nothing written by a
 *     refusal; the counters RESERVED, IN_USE and the host's ATTACHED
 *     and their O and R rows; the units of the three kinds counted
 *     once against QTY; the item rules while units are held (QTY below
 *     them, SPLIT, STATUS=D, DEL), each with the reason of
 *     the first counter, and a host with equipment attached neither
 *     disposed of nor deleted; AVAIL and its complement, a release
 *     making the item available only when nothing else holds it;
 *   - LOAN SET of a host: moved, taken away, refused, given to an
 *     ended row (no counter changes), the keys of each kind;
 *   - LOAN LIST by host, by kind and by holder (the whole name in any
 *     case, alone and with each other filter, an empty one keeping
 *     every row, one too long refused), of all rows or one item's,
 *     paged with cursors; LOAN OVERDUE with a reservation whose date
 *     passed;
 *   - the repair of all four counters, among them a host whose ATTACHED
 *     fell to 0 and one with no rows left, many hosts, a loan naming a
 *     host (not counted) and loans past the item's QTY (the later kinds
 *     capped to what is left).
 * The phases of reservations and in-use equipment are in T_LOAN2.C, as
 * one file would pass Watcom C 6.5's compiler memory; T_LOAN.H holds
 * what the two share.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when dir is too long.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGITEM.H"
#include "ENGLOAN.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"
#include "T_LOAN.H"

#define MAXPAGES        20      /* requests of one listed scan          */

/* The fields of an audit row as aud gives them. */
static const char k_aud[] = "USERNAME TBL REC_ID ACTION FLD OLDVAL NEWVAL";

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };
const eng_time g_next = { 2026, 10, 9, 8, 0, 0 };   /* the repair       */

eng *g_e;
u32 g_id;
static u32 g_a;                 /* items lent in the first phases       */
static u32 g_b;
static u32 g_c;                 /* the item of the list phase           */
const char *g_holder;

/* ---------------------------------------------------------------------
 * Fields, audit rows and changes.
 * --------------------------------------------------------------------- */

/* The text of field name of live item id, "?" when there is none. */
const char *ifield(u32 id, const char *name)
{
    u32 r;

    if (eng_item_get(g_e, id, &r) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_ITEMS, TE_BUF, name);
}

/* The text of field name of live loan id, "?" when there is none. */
const char *lfield(u32 id, const char *name)
{
    u32 r;

    if (eng_loan_get(g_e, id, &r) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_LOANS, TE_BUF, name);
}

/* Audit record recno as its k_aud fields. */
static const char *aud(u32 recno)
{
    return te_aud(recno, k_aud);
}

/* 1 when the marker ENG_EDITMARK is in the data directory. */
int marked(void)
{
    return tfs_exists(tfs_path(ENG_EDITMARK, ""));
}

/* A new item from the pairs of spec, which must be made: its ID. */
u32 add_item(const char *spec)
{
    u32 id;

    id = 0;
    TF_EQ(eng_item_add(g_e, te_p, te_kv(spec), &id), ENG_OK);
    return id;
}

/* A new row of kind of item from the pairs of spec, its LOAN_ID into
   g_id (0 when refused). */
int row(int kind, u32 item, const char *spec)
{
    return eng_loan_out(g_e, item, kind, te_p, te_kv(spec), &g_id);
}

/* A new row that must be made: its LOAN_ID. */
u32 hold(int kind, u32 item, const char *spec)
{
    if (!TF_EQ(row(kind, item, spec), ENG_OK))
        printf("  %s: %s %s\n", spec, eng_errfield(g_e), eng_errmsg(g_e));
    return g_id;
}

/* A loan of item from the pairs of spec, which must be made: its
   LOAN_ID. */
u32 lend(u32 item, const char *spec)
{
    return hold(ENG_K_LOAN, item, spec);
}

/* Number num stored in field name of record recno of table t straight
   into the file, as a hand edit would. */
void raw_num(int t, u32 recno, const char *name, i32 num)
{
    TF_EQ(te_raw_put(t, recno, -1, name, NULL, num), 0);
}

/* Every live item's counters are what its open rows give: ON_LOAN,
   RESERVED and IN_USE the positive QTY of its open rows of each kind,
   ATTACHED the number of open R and U rows naming it as host.  Each
   item against every live row (the tables here are small). */
void check_counts(const char *file, int line)
{
    static const char *const names[] = {
        "ON_LOAN", "RESERVED", "IN_USE", "ATTACHED"
    };
    static const char kinds[] = "LRU";
    const char *k;
    u32 ni;
    u32 nl;
    u32 r;
    u32 j;
    i32 id;
    i32 v[4];
    i32 sum[4];
    int i;

    ni = eng_reccount(g_e, ENG_T_ITEMS);
    nl = eng_reccount(g_e, ENG_T_LOANS);
    for (r = 1; r <= ni; r++) {
        id = te_rnum(ENG_T_ITEMS, r, "ITEM_ID");
        if (dbf_is_deleted(eng_dbf(g_e, ENG_T_ITEMS)))
            continue;
        for (i = 0; i < 4; i++) {
            v[i] = te_rnum(ENG_T_ITEMS, r, names[i]);
            sum[i] = 0;
        }
        for (j = 1; j <= nl; j++) {
            te_rnum(ENG_T_LOANS, j, "QTY");
            if (dbf_is_deleted(eng_dbf(g_e, ENG_T_LOANS)) ||
                te_rtext(ENG_T_LOANS, TE_BUF, "RET_DATE")[0] != '\0')
                continue;
            k = te_rtext(ENG_T_LOANS, TE_BUF, "KIND");
            for (i = 2; i > 0 && kinds[i] != k[0]; i--)
                ;
            if (te_rnum(ENG_T_LOANS, TE_BUF, "ITEM_ID") == id &&
                te_rnum(ENG_T_LOANS, TE_BUF, "QTY") > 0)
                sum[i] += te_rnum(ENG_T_LOANS, TE_BUF, "QTY");
            if (i != 0 && te_rnum(ENG_T_LOANS, TE_BUF, "HOST_ID") == id)
                sum[3]++;
        }
        for (i = 0; i < 4; i++)
            if (!tf_check(v[i] == sum[i], file, line, ""))
                printf("%s %d: item %ld %s %ld, open %ld\n", file, line,
                       (long)id, names[i], (long)v[i], (long)sum[i]);
    }
}

/* ---------------------------------------------------------------------
 * The phases.
 * --------------------------------------------------------------------- */

static void test_out(void)
{
    char parent[TXT_U32LEN + 1];
    u32 inst;
    u32 disp;
    u32 gone;
    u32 id;
    u32 na;

    g_a = add_item("CATEGORY=SYS|QTY=3");
    g_b = add_item("CATEGORY=SYS|QTY=1");
    inst = add_item("CATEGORY=SYS");
    disp = add_item("CATEGORY=SYS");
    gone = add_item("CATEGORY=SYS");
    txt_u32(parent, g_b);
    TF_EQ(eng_edit_begin(g_e, ENG_T_ITEMS, te_irec(inst)), ENG_OK);
    TF_EQ(eng_edit_put(g_e, "PARENT_ID", parent), ENG_OK);
    TF_EQ(eng_edit_commit(g_e, inst), ENG_OK);
    TF_EQ(eng_item_set(g_e, disp, te_p, te_kv("STATUS=D")), ENG_OK);
    TF_EQ(eng_item_del(g_e, gone), ENG_OK);
    na = te_naud();

    TE_FAIL(row(ENG_K_LOAN, 999, "HOLDER=X|DUE=20261020"), ENG_ENOTFOUND,
            "ITEM", "no such item");
    TE_FAIL(row(ENG_K_LOAN, gone, "HOLDER=X|DUE=20261020"), ENG_ENOTFOUND,
            "ITEM", "no such item");
    TE_FAIL(row(ENG_K_LOAN, inst, "HOLDER=X|DUE=20261020"), ENG_ECONSTR,
            "ITEM", "installed in an assembly");
    TE_FAIL(row(ENG_K_LOAN, disp, "HOLDER=X|DUE=20261020"), ENG_ECONSTR,
            "ITEM", "item was disposed of");
    TE_FAIL(row(ENG_K_LOAN, g_a, "DUE=20261020"), ENG_EARG, "HOLDER",
            "value required");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X"), ENG_EARG, "DUE_DATE",
            "value required");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261340"), ENG_EARG,
            "DUE_DATE", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|QTY=0"), ENG_EARG,
            "QTY", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|QTY=-1"), ENG_EARG,
            "QTY", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|QTY=4"),
            ENG_ECONSTR, "QTY", "not enough on hand");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DATE=20261010|DUE=20261009"),
            ENG_EARG, "DUE_DATE", "due date before out date");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261007"), ENG_EARG,
            "DUE_DATE", "due date before out date");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DATE=|DUE=20261007"), ENG_EARG,
            "DUE_DATE", "due date before out date");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|DUE_DATE=20261021"),
            ENG_EARG, "DUE_DATE", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a,
                "HOLDER=X|DATE=20261001|OUT_DATE=20261001|DUE=20261020"),
            ENG_EARG, "OUT_DATE", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|RET_DATE=20261009"),
            ENG_EARG, "RET_DATE", "field not settable");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|LOAN_ID=5"),
            ENG_EARG, "LOAN_ID", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|ITEM_ID=5"),
            ENG_EARG, "ITEM_ID", NULL);
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|KIND=R"),
            ENG_EARG, "KIND", "field not settable");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|HOST=2"),
            ENG_EARG, "HOST", "field not settable");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020|FOO=1"), ENG_EARG,
            "FOO", NULL);
    TF_EQ(g_id, 0);
    TF_EQ(te_naud(), na);
    TF_EQ(eng_reccount(g_e, ENG_T_LOANS), 0);
    TF_CHECK(!marked());

    /* Defaults: QTY 1, OUT_DATE today. */
    id = lend(g_a, "HOLDER=JANE|DUE=20261020|CONTACT=555-1234|NOTES=box");
    TF_EQ(id, 1);
    TF_STREQ(lfield(id, "QTY"), "1");
    TF_STREQ(lfield(id, "ITEM_ID"), "1");
    TF_STREQ(lfield(id, "OUT_DATE"), "20261008");
    TF_STREQ(lfield(id, "DUE_DATE"), "20261020");
    TF_STREQ(lfield(id, "RET_DATE"), "");
    TF_STREQ(lfield(id, "CONTACT"), "555-1234");
    TF_STREQ(ifield(g_a, "ON_LOAN"), "1");
    TF_STREQ(ifield(g_a, "UPDATED"), "20261008");
    TF_EQ(te_naud(), na + 2);
    TF_STREQ(aud(na + 1), "TESTER|LOANS|1|I|LOAN_ID||1");
    TF_STREQ(aud(na + 2), "TESTER|ITEMS|1|O|ON_LOAN|0|1");
    TF_CHECK(!marked());

    /* The DATE and DUE keys, and the full names. */
    id = lend(g_a, "HOLDER=BOB|DATE=20261001|DUE=20261005|QTY=2");
    TF_EQ(id, 2);
    TF_STREQ(lfield(id, "OUT_DATE"), "20261001");
    TF_STREQ(lfield(id, "DUE_DATE"), "20261005");
    TF_STREQ(ifield(g_a, "ON_LOAN"), "3");
    TF_STREQ(aud(te_naud()), "TESTER|ITEMS|1|O|ON_LOAN|1|3");
    TE_FAIL(row(ENG_K_LOAN, g_a, "HOLDER=X|DUE=20261020"), ENG_ECONSTR,
            "QTY", "not enough on hand");
    id = lend(g_b, "HOLDER=CY|DATE=|DUE_DATE=20261008");
    TF_EQ(id, 3);
    TF_STREQ(lfield(id, "OUT_DATE"), "20261008");
    TF_STREQ(ifield(g_b, "ON_LOAN"), "1");
    COUNTS_OK();
}

static void test_items(void)
{
    TE_FAIL(eng_item_set(g_e, g_a, te_p, te_kv("STATUS=D")), ENG_ECONSTR,
            "STATUS", "item is on loan");
    TF_STREQ(ifield(g_a, "STATUS"), "A");
    TE_FAIL(eng_item_set(g_e, g_a, te_p, te_kv("QTY=2")), ENG_ECONSTR, "QTY",
            NULL);
    TF_EQ(eng_item_set(g_e, g_a, te_p, te_kv("QTY=3")), ENG_OK);
    TE_FAIL(eng_item_del(g_e, g_a), ENG_ECONSTR, "ON_LOAN", NULL);
    TE_FAIL(eng_item_set(g_e, g_a, te_p, te_kv("ON_LOAN=0")), ENG_EARG,
            "ON_LOAN", NULL);
}

static void test_return(void)
{
    u32 na;
    u32 c;
    u32 id;

    TE_FAIL(eng_loan_return(g_e, 99, NULL), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_loan_return(g_e, 0, NULL), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_loan_return(g_e, 1, "20261007"), ENG_EARG, "RET_DATE",
            "end date before out date");
    TE_FAIL(eng_loan_return(g_e, 1, "20261032"), ENG_EARG, "RET_DATE", NULL);
    TE_FAIL(eng_loan_return(g_e, 1, "2026100"), ENG_EARG, "RET_DATE", NULL);
    TF_STREQ(lfield(1, "RET_DATE"), "");
    TF_CHECK(!marked());

    /* Partial: two of the three are back, by default today. */
    na = te_naud();
    TF_EQ(eng_loan_return(g_e, 2, NULL), ENG_OK);
    TF_STREQ(lfield(2, "RET_DATE"), "20261008");
    TF_STREQ(ifield(g_a, "ON_LOAN"), "1");
    TF_EQ(te_naud(), na + 2);
    TF_STREQ(aud(na + 1), "TESTER|LOANS|2|U|RET_DATE||20261008");
    TF_STREQ(aud(na + 2), "TESTER|ITEMS|1|R|ON_LOAN|3|1");
    TF_CHECK(!marked());
    COUNTS_OK();
    TE_FAIL(eng_loan_return(g_e, 2, NULL), ENG_ECONSTR, "ID",
            "already returned or released");
    TF_EQ(te_naud(), na + 2);

    /* Out again and back with a date. */
    id = lend(g_a, "HOLDER=DEE|DUE=20261030|QTY=2");
    TF_EQ(id, 4);
    COUNTS_OK();
    TF_EQ(eng_loan_return(g_e, 1, "20261009"), ENG_OK);
    TF_STREQ(lfield(1, "RET_DATE"), "20261009");
    COUNTS_OK();

    /* ON_LOAN set too low by hand: a return stops at 0, and writes no
       R row when the item is at 0 already. */
    raw_num(ENG_T_ITEMS, te_irec(g_a), "ON_LOAN", 1);
    na = te_naud();
    TF_EQ(eng_loan_return(g_e, 4, NULL), ENG_OK);
    TF_STREQ(ifield(g_a, "ON_LOAN"), "0");
    TF_EQ(te_naud(), na + 2);
    TF_STREQ(aud(te_naud()), "TESTER|ITEMS|1|R|ON_LOAN|1|0");
    raw_num(ENG_T_ITEMS, te_irec(g_b), "ON_LOAN", 0);
    na = te_naud();
    TF_EQ(eng_loan_return(g_e, 3, NULL), ENG_OK);
    TF_STREQ(ifield(g_b, "ON_LOAN"), "0");
    TF_EQ(te_naud(), na + 1);

    /* An item deleted while its loan is open is left alone. */
    c = add_item("CATEGORY=SYS");
    id = lend(c, "HOLDER=EVE|DUE=20261031");
    raw_num(ENG_T_ITEMS, te_irec(c), "ON_LOAN", 0);
    TF_EQ(eng_item_del(g_e, c), ENG_OK);
    raw_num(ENG_T_ITEMS, eng_reccount(g_e, ENG_T_ITEMS), "ON_LOAN", 1);
    na = te_naud();
    TF_EQ(eng_loan_return(g_e, id, NULL), ENG_OK);
    TF_EQ(te_naud(), na + 1);
    TF_STREQ(te_rtext(ENG_T_ITEMS, eng_reccount(g_e, ENG_T_ITEMS),
                      "ON_LOAN"), "1");
    TF_CHECK(!marked());
}

static void test_set(void)
{
    u32 id;
    u32 na;

    id = lend(g_a, "HOLDER=FAY|OUT_DATE=20261005|DUE=20261012");
    na = te_naud();
    TF_EQ(eng_loan_set(g_e, id, te_p,
                       te_kv("HOLDER=GUS|CONTACT=desk|NOTES=n1")), ENG_OK);
    TF_STREQ(lfield(id, "HOLDER"), "GUS");
    TF_EQ(te_naud(), na + 3);
    TF_STREQ(aud(na + 1), "TESTER|LOANS|6|U|HOLDER|FAY|GUS");
    TF_EQ(eng_loan_set(g_e, id, te_p, te_kv("DUE=20261013")), ENG_OK);
    TF_STREQ(lfield(id, "DUE_DATE"), "20261013");
    TF_EQ(eng_loan_set(g_e, id, te_p, te_kv("DUE_DATE=20261005")), ENG_OK);
    TF_STREQ(aud(te_naud()), "TESTER|LOANS|6|U|DUE_DATE|20261013|20261005");
    na = te_naud();
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("HOLDER=")), ENG_EARG,
            "HOLDER", NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("DUE=")), ENG_EARG, "DUE_DATE",
            NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("DUE_DATE=")), ENG_EARG,
            "DUE_DATE", NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("DUE=20261004")), ENG_EARG,
            "DUE_DATE", "due date before out date");
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("QTY=2")), ENG_EARG, "QTY",
            "field not settable");
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("DATE=20261001")), ENG_EARG,
            "DATE", NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("OUT_DATE=20261001")),
            ENG_EARG, "OUT_DATE", NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("RET_DATE=20261009")),
            ENG_EARG, "RET_DATE", NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p, te_kv("ITEM_ID=2")), ENG_EARG,
            "ITEM_ID", NULL);
    TE_FAIL(eng_loan_set(g_e, id, te_p,
                         te_kv("DUE=20261009|DUE_DATE=20261010")),
            ENG_EARG, "DUE_DATE", NULL);
    TE_FAIL(eng_loan_set(g_e, 99, te_p, te_kv("HOLDER=X")), ENG_ENOTFOUND,
            "ID", NULL);
    TF_EQ(te_naud(), na);
    TF_CHECK(!marked());
    TF_STREQ(lfield(id, "HOLDER"), "GUS");
    TF_STREQ(lfield(id, "DUE_DATE"), "20261005");
    TF_EQ(eng_loan_return(g_e, id, NULL), ENG_OK);
    COUNTS_OK();
}

/* The LOAN_IDs of a whole LOAN LIST (over 0, with the filters item,
   host, kind, g_holder and open) or LOAN OVERDUE (over 1, as of asof)
   scan, separated by spaces: requests of up to page rows each, every
   one after the cursor of the one before, until none remain; *pages
   counts the requests.  An overdue row that eng_loan_late does not call
   late as of asof is marked with a '!' after its ID. */
const char *ids(int over, u32 item, u32 host, int kind, int open,
                       const char *asof, u16 page, int *pages)
{
    static char out[LISTMAX];
    char cur[ENG_CURSORMAX];
    eng_scan s;
    u32 r;
    u16 k;
    int more;
    int rc;

    out[0] = '\0';
    cur[0] = '\0';
    *pages = 0;
    more = 1;
    while (more && *pages < MAXPAGES) {
        (*pages)++;
        rc = over ? eng_loan_overdue(g_e, &s, asof, cur)
                  : eng_loan_scan(g_e, &s, item, host, kind, g_holder,
                                  open, cur);
        if (!TF_EQ(rc, ENG_OK))
            break;
        for (k = 0; k < page; k++) {
            TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
            if (r == 0)
                break;
            if (out[0] != '\0')
                strcat(out, " ");
            strcat(out, te_rtext(ENG_T_LOANS, TE_BUF, "LOAN_ID"));
            if (over && !eng_loan_late(g_e, asof))
                strcat(out, "!");
        }
        TF_EQ(eng_scan_more(g_e, &s, &more), ENG_OK);
        eng_scan_cursor(&s, cur);
    }
    return out;
}

static void test_lists(void)
{
    eng_scan s;
    u32 c;
    u32 id;
    u32 r;
    int pages;

    /* Loans 1..6 are returned.  Open loans as of 20261008: */
    c = add_item("CATEGORY=SYS|QTY=9");
    g_c = c;
    id = lend(c, "HOLDER=H|DATE=20261001|DUE=20261007");     /* 7 late */
    TF_EQ(id, 7);
    lend(c, "HOLDER=I|DUE=20261008");                         /* 8 */
    lend(c, "HOLDER=J|DUE=20261009");                         /* 9 */
    lend(c, "HOLDER=K|DATE=20261001|DUE=20261002");           /* 10 late */
    id = lend(c, "HOLDER=L|DUE=20261111");                    /* 11 */
    TF_EQ(eng_loan_get(g_e, id, &r), ENG_OK);
    TF_EQ(eng_edit_begin(g_e, ENG_T_LOANS, r), ENG_OK);
    TF_EQ(eng_edit_put(g_e, "DUE_DATE", ""), ENG_OK);           /* blank */
    TF_EQ(eng_edit_commit(g_e, id), ENG_OK);
    lend(g_a, "HOLDER=M|DATE=20261001|DUE=20261003");         /* 12 late */
    id = lend(c, "HOLDER=N|DATE=20261001|DUE=20261004");     /* 13 */
    TF_EQ(eng_loan_return(g_e, id, NULL), ENG_OK);
    COUNTS_OK();

    /* The open loans by due date (the blank one first), then the
       returned ones by year of return and due date. */
    TF_STREQ(ids(0, 0, 0, 0, 1, NULL, 25, &pages), "11 10 12 7 8 9");
    TF_EQ(pages, 1);
    TF_STREQ(ids(0, 0, 0, 0, 1, NULL, 2, &pages), "11 10 12 7 8 9");
    TF_EQ(pages, 3);
    TF_STREQ(ids(0, 0, 0, 0, 0, NULL, 4, &pages),
             "11 10 12 7 8 9 13 2 6 3 1 4 5");
    TF_STREQ(ids(0, c, 0, 0, 0, NULL, 25, &pages), "7 8 9 10 11 13");
    TF_STREQ(ids(0, c, 0, 0, 1, NULL, 2, &pages), "7 8 9 10 11");
    TE_FAIL(eng_loan_scan(g_e, &s, 999, 0, 0, NULL, 0, NULL), ENG_ENOTFOUND,
            "ITEM", "no such item");

    /* Overdue as of today: due before 20261008, the blank one skipped. */
    TF_STREQ(ids(1, 0, 0, 0, 0, NULL, 25, &pages), "10 12 7");
    TF_EQ(pages, 1);
    TF_STREQ(ids(1, 0, 0, 0, 0, "20261008", 1, &pages), "10 12 7");
    TF_EQ(pages, 3);
    TF_STREQ(ids(1, 0, 0, 0, 0, "20261009", 25, &pages), "10 12 7 8");
    TF_STREQ(ids(1, 0, 0, 0, 0, "20261010", 25, &pages), "10 12 7 8 9");
    TF_STREQ(ids(1, 0, 0, 0, 0, "20261003", 25, &pages), "10");
    TF_STREQ(ids(1, 0, 0, 0, 0, "20261002", 25, &pages), "");
    TF_STREQ(ids(1, 0, 0, 0, 0, "20991231", 25, &pages), "10 12 7 8 9");
    TE_FAIL(eng_loan_overdue(g_e, &s, "20261301", NULL), ENG_EARG, "ASOF",
            NULL);
    TE_FAIL(eng_loan_overdue(g_e, &s, "2026", NULL), ENG_EARG, "ASOF", NULL);
    TE_FAIL(eng_loan_asof_ok(g_e, "20261301"), ENG_EARG, "ASOF", NULL);
    TE_FAIL(eng_loan_asof_ok(g_e, "2026"), ENG_EARG, "ASOF", NULL);
    TF_EQ(eng_loan_asof_ok(g_e, "20261231"), ENG_OK);
    TF_EQ(eng_loan_asof_ok(g_e, ""), ENG_OK);
    TF_EQ(eng_loan_asof_ok(g_e, NULL), ENG_OK);

    /* LATE as of a day, and the item of a loan. */
    TF_EQ(eng_loan_get(g_e, 8, &r), ENG_OK);
    TF_EQ(eng_loan_late(g_e, NULL), 0);
    TF_EQ(eng_loan_late(g_e, "20261009"), 1);
    TF_EQ(eng_loan_item(g_e, &r), ENG_OK);
    TF_EQ(r, te_irec(c));
    TF_EQ(eng_loan_get(g_e, 11, &r), ENG_OK);
    TF_EQ(eng_loan_late(g_e, "20991231"), 0);
    TF_EQ(eng_loan_get(g_e, 13, &r), ENG_OK);
    TF_EQ(eng_loan_late(g_e, "20991231"), 0);
}

/* The bound alone: an overdue page that ends on the last loan due before
   ASOF has no more, though open loans follow in LOANS2. */
static void test_bound(void)
{
    eng_scan s;
    u32 r;
    int more;

    more = 1;
    TF_EQ(eng_loan_overdue(g_e, &s, "20261008", NULL), ENG_OK);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_LOANS, TE_BUF, "LOAN_ID"), "7");
    TF_EQ(eng_scan_more(g_e, &s, &more), ENG_OK);
    TF_EQ(more, 0);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);
    TF_EQ(s.reads, 4);
}

/* eng_loan_get over many records: deleted ones, gaps in the IDs. */
static void test_get(void)
{
    u32 c;
    u32 id;
    u32 r;
    u32 first;
    u32 last;
    u32 gap;
    u16 i;

    c = add_item("CATEGORY=SYS|QTY=99");
    first = 0;
    gap = 0;
    for (i = 0; i < 30; i++) {
        if (i == 10)
            TF_EQ(eng_seq_next(g_e, ENG_T_LOANS, 5, &gap), ENG_OK);
        id = lend(c, "HOLDER=Z|DUE=20261231");
        if (first == 0)
            first = id;
    }
    last = id;
    TF_EQ(eng_loan_get(g_e, 1, &r), ENG_OK);
    TF_EQ(r, 1);
    TF_EQ(first, 14);
    TF_EQ(eng_loan_get(g_e, first, &r), ENG_OK);
    TF_EQ(r, 14);
    TF_EQ(eng_loan_get(g_e, last, &r), ENG_OK);
    TF_EQ(r, eng_reccount(g_e, ENG_T_LOANS));
    TF_EQ(eng_loan_get(g_e, gap + 5, &r), ENG_OK);
    TF_EQ(r, 24);
    TE_FAIL(eng_loan_get(g_e, gap, &r), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_loan_get(g_e, gap + 4, &r), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_loan_get(g_e, last + 1, &r), ENG_ENOTFOUND, "ID", NULL);
    TF_EQ(r, 0);
    for (id = first + 3; id <= first + 5; id++) {
        TF_EQ(eng_loan_get(g_e, id, &r), ENG_OK);
        TF_EQ(eng_edit_begin(g_e, ENG_T_LOANS, r), ENG_OK);
        eng_edit_delete(g_e, 1);
        TF_EQ(eng_edit_commit(g_e, id), ENG_OK);
    }
    TE_FAIL(eng_loan_get(g_e, first + 4, &r), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_loan_return(g_e, first + 4, NULL), ENG_ENOTFOUND, "ID", NULL);
    TF_EQ(eng_loan_get(g_e, first + 6, &r), ENG_OK);
    TF_EQ(eng_loan_get(g_e, first + 2, &r), ENG_OK);
    raw_num(ENG_T_ITEMS, te_irec(c), "ON_LOAN", 27);
    COUNTS_OK();
}

/* A directory damaged by hand, with the marker naming LOANS that an
   interrupted LOAN OUT or RETURN leaves, repaired by the next open. */
static void test_heal(void)
{
    u32 none;
    u32 over;
    u32 ol;
    u32 na;
    u32 n;
    u32 k;

    none = add_item("CATEGORY=SYS|QTY=5");
    over = add_item("CATEGORY=SYS|QTY=4");
    ol = lend(over, "HOLDER=P|DUE=20261231|QTY=3");
    raw_num(ENG_T_ITEMS, te_irec(none), "ON_LOAN", 5);
    raw_num(ENG_T_ITEMS, te_irec(over), "ON_LOAN", 1);
    raw_num(ENG_T_LOANS, eng_reccount(g_e, ENG_T_LOANS), "QTY", 9);
    raw_num(ENG_T_ITEMS, te_irec(g_a), "ON_LOAN", 0);
    raw_num(ENG_T_ITEMS, te_irec(g_b), "ON_LOAN", 1);
    na = te_naud();
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;

    TF_EQ(te_marker_put("LOANS"), 0);
    if (!TF_EQ(te_open(&g_e, &g_next), ENG_OK))
        return;
    TF_CHECK(!marked());
    TF_EQ(eng_marker_kept(g_e), 0);

    TF_STREQ(ifield(none, "ON_LOAN"), "0");
    TF_STREQ(ifield(over, "ON_LOAN"), "4");
    TF_STREQ(ifield(g_a, "ON_LOAN"), "1");
    TF_STREQ(ifield(g_b, "ON_LOAN"), "0");
    TF_STREQ(ifield(none, "UPDATED"), "20261009");
    TF_STREQ(ifield(over, "UPDATED"), "20261009");
    TF_STREQ(ifield(g_a, "UPDATED"), "20261009");
    TF_STREQ(ifield(g_b, "UPDATED"), "20261009");
    TF_STREQ(ifield(g_c, "UPDATED"), "20261008");
    n = te_naud() - na;
    TF_EQ(n, 4);
    for (k = 1; k <= n; k++)
        TF_STREQ(te_aud(na + k, "STATION"), "SYSTEM");
    TF_STREQ(aud(na + 1), "SYSTEM|ITEMS|1|O|ON_LOAN|0|1");
    TF_STREQ(aud(na + 2), "SYSTEM|ITEMS|2|R|ON_LOAN|1|0");
    TF_STREQ(aud(na + 3), "SYSTEM|ITEMS|10|O|ON_LOAN|1|4");
    TF_STREQ(aud(na + 4), "SYSTEM|ITEMS|9|R|ON_LOAN|5|0");

    /* Repaired: a second pass changes nothing.  The loan above the
       item's QTY ends, leaving the counters as the rows give them. */
    na = te_naud();
    TF_EQ(eng_loan_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), na);
    TF_EQ(eng_loan_return(g_e, ol, NULL), ENG_OK);
    COUNTS_OK();
}

int main(int argc, char **argv)
{
    tf_begin("T_LOAN");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    if (TF_EQ(te_open(&g_e, &g_now), ENG_OK)) {
        test_out();
        test_items();
        test_return();
        test_set();
        test_lists();
        test_bound();
        test_get();
        test_heal();
        if (g_e != NULL) {
            test_holds();
            test_hset();
            test_hinstall();
            test_hlists();
            test_hheal();
        }
        /* test_hmany in a data directory of its own: its rows would
           not fit the RAM disk of the DOS runs beside the others. */
        if (g_e != NULL) {
            TF_EQ(eng_close(g_e), ENG_OK);
            te_remove(tfs_dir(), NULL);
            if (TF_EQ(te_open(&g_e, &g_now), ENG_OK))
                test_hmany();
        }
        if (g_e != NULL)
            TF_EQ(eng_close(g_e), ENG_OK);
    }
    te_remove(tfs_dir(), NULL);
    return tf_done();
}
