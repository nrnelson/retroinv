/*
 * RPT.C - the layout of reports and labels (RPT.H).
 *
 * Values are found through a table of the field names RPT.C knows
 * (rpt_fname), each kept once.  An F line maps every known name to its
 * place in the R lines that follow (fx); a value an R line lacks is
 * empty, and an R line before any F line, which names no column, is
 * ignored.  A record is then laid out from one array of value
 * pointers, by field, whether it came from an R line or from the K
 * lines of a single record, which a label run gathers in a near buffer
 * of RPT_ROWMAX bytes (the fields of its kind only).
 *
 * Report columns come from one table per kind (rpt_cols): field, width,
 * alignment and the message number of the heading, a column of width 0
 * ending a kind's columns early; each column is followed by one space; a
 * number too long for its right-aligned column shows as '*' marks, other
 * values are cut.  Lines are built in a buffer of RPT_WIDTH characters and
 * sent with their trailing spaces cut.
 *
 * Paging.  A page is started only when a body line is about to be
 * written, so no page is ever empty and a report never ends with a
 * blank page; the page ends (pr_ff) after its last body line, and when
 * the report is closed.  A page that starts inside a group repeats the
 * group's heading, marked as continued, and a page inside the category
 * table repeats the table's heading and its own column headings; a
 * blank line that would open a page is left out.
 *
 * Totals are kept per group and for the whole report: records,
 * quantity and, for the valuation, the two amounts in cents.  RPT_VALUE
 * also totals by category in a far block of RPT_NCAT entries, whose
 * codes are kept in near memory for the lookup of every row: first the
 * categories given by rpt_cat, then each code of a row that none of
 * them has, in the order met; rows whose code finds no free entry are
 * added up on a last line of their own, so the table always adds up to
 * the report's total.
 */
#define MSG_PATHS
#define MSG_LETTER
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "PRINT.H"
#include "SCRNFORM.H"
#include "RPT.H"

#define MR_H_ITEMS      950     /* the category table's count heading   */
#define MR_T_LOANS      953     /* report titles, with MR_T_LIST and
                                   MR_T_VALUE of RPT.H                  */
#define MR_T_OVERDUE    954
#define MR_PAGE         955     /* date and page number                 */
#define MR_NONE         956     /* the body of an empty report          */
#define MR_NOLOC        958     /* the heading of no location's group   */
#define MR_CONT         959     /* a heading repeated on a new page     */
#define MR_SUBL         960     /* item listing: count and quantity     */
#define MR_TOTL         961
#define MR_SUBV         962     /* valuation: labels of the total lines */
#define MR_TOTV         963
#define MR_NITEMS       964
#define MR_BYCAT        965
#define MR_NLOANS       966     /* loans: count and late count          */
#define MR_L_NO         967     /* label prefixes                       */
#define MR_L_SN         968
#define MR_L_LOC        969
#define MR_L_CODE       970
#define MR_H_ID         971     /* column headings                      */
#define MR_H_CAT        972
#define MR_H_MFR        973
#define MR_H_MODEL      974
#define MR_H_SERIAL     975
#define MR_H_WORKS      976
#define MR_H_QTY        977
#define MR_H_LOC        978
#define MR_H_PAID       979
#define MR_H_VALUE      980
#define MR_H_LATE       981
#define MR_H_LOAN       982
#define MR_H_ITEM       983
#define MR_H_HOLDER     984
#define MR_H_CONTACT    985
#define MR_H_KIND       986
#define MR_H_DUE        987
#define MR_H_MM         988
#define MR_H_NAME       989     /* the category table's name heading    */
#define MR_OTHER        990     /* categories beyond the table          */
#define MR_STOPPED      991     /* the last line of a stopped report    */

/* The fields RPT.C reads, by number. */
#define RF_ID           0
#define RF_CAT          1
#define RF_MFR          2
#define RF_MODEL        3
#define RF_SERIAL       4
#define RF_WORKS        5
#define RF_QTY          6
#define RF_LOC          7
#define RF_PRICE        8
#define RF_VALUE        9
#define RF_LATE         10
#define RF_LOAN         11
#define RF_HOLDER       12
#define RF_CONTACT      13
#define RF_KIND         14
#define RF_DUE          15
#define RF_LCODE        16
#define RF_PATH         17
#define RF_LTNAME       18
#define RF_HOST         19
#define RF_N            20
#define RF_NONE         0xFFu   /* fx, kx: the field is not present     */

#define RB(f)           ((u32)1 << (f))

static const char k_id[] = "ITEM_ID";
static const char k_cat[] = "CATEGORY";
static const char k_mfr[] = "MFR";
static const char k_model[] = "MODEL";
static const char k_serial[] = "SERIALNO";
static const char k_works[] = "WORKS";
static const char k_qty[] = "QTY";
static const char k_loc[] = "LOC_NAME";
static const char k_price[] = "ACQ_PRICE";
static const char k_value[] = "CUR_VALUE";
static const char k_late[] = "LATE";
static const char k_loan[] = "LOAN_ID";
static const char k_holder[] = "HOLDER";
static const char k_contact[] = "CONTACT";
static const char k_kind[] = "KIND";
static const char k_due[] = "DUE_DATE";
static const char k_lcode[] = "LOC_CODE";
static const char k_path[] = "PATH";
static const char k_ltname[] = "LT_NAME";
static const char k_host[] = "HOST_ID";
/* The kinds' letters (ITL_KINDS of ITEMLS.H, as itl_whys, which the
   report's tests do not link, begins): a kind's number is its distance
   into them. */
static const char k_kinds[] = ITL_KINDS;
/* The works states' letters (ITL_WORKS of ITEMLS.H), likewise. */
static const char k_states[] = ITL_WORKS;

static const char *const rpt_fname[RF_N] = {
    k_id, k_cat, k_mfr, k_model, k_serial, k_works, k_qty, k_loc, k_price,
    k_value, k_late, k_loan, k_holder, k_contact, k_kind, k_due, k_lcode,
    k_path, k_ltname, k_host
};

/* The fields each kind reads, by kind - 1. */
static const u32 rpt_need[RPT_BLABEL] = {
    RB(RF_ID) | RB(RF_CAT) | RB(RF_MFR) | RB(RF_MODEL) | RB(RF_SERIAL) |
        RB(RF_WORKS) | RB(RF_QTY) | RB(RF_LOC),
    RB(RF_ID) | RB(RF_CAT) | RB(RF_MFR) | RB(RF_MODEL) | RB(RF_QTY) |
        RB(RF_PRICE) | RB(RF_VALUE),
    RB(RF_LATE) | RB(RF_LOAN) | RB(RF_ID) | RB(RF_KIND) | RB(RF_QTY) |
        RB(RF_HOLDER) | RB(RF_HOST) | RB(RF_CONTACT) | RB(RF_DUE) |
        RB(RF_MFR) | RB(RF_MODEL),
    RB(RF_ID) | RB(RF_CAT) | RB(RF_MFR) | RB(RF_MODEL) | RB(RF_SERIAL) |
        RB(RF_LOC),
    RB(RF_LOC) | RB(RF_LCODE) | RB(RF_PATH) | RB(RF_LTNAME)
};

/* Report columns.  Flags: right-aligned, a date YYYYMMDD shown as
   YYYY-MM-DD, the late mark ("1" shown as '!'), maker and model in one
   column, MSG_CUTMARK ending a name cut short, a row's kind by its name
   (MH_K_NAME of ITEMLS.H), a holder or, for a row naming none, its
   host's ID after MH_L_HOST, or MH_L_HOSTUSE for a use, a works state
   by its word (MT_WK_Q of ITEMLS.H).  head is the heading's message
   number less MSG_R_REPORT. */
#define CF_RIGHT        0x01
#define CF_DATE         0x02
#define CF_LATE         0x04
#define CF_ITEM         0x08
#define CF_MARK         0x10
#define CF_KIND         0x20
#define CF_HOLDER       0x40
#define CF_WORKS        0x80

typedef struct {
    u8   f;
    u8   w;
    u8   fl;
    u8   head;
} rcol;

#define H(m)            ((u8)((m) - MSG_R_REPORT))
#define RPT_NCOLS       9       /* columns of every report kind         */

/* Column widths: L item listing, V valuation, O loans. */
#define W_ID            8       /* ITEM_ID, LOAN_ID                     */
#define W_CAT           LINK_CATLEN
#define W_DATE          10      /* YYYY-MM-DD                           */
#define LW_MFR          14
#define LW_MODEL        14
#define LW_SERIAL       12
#define LW_WORKS        5
#define LW_QTY          5
#define LW_LOC          10      /* the location's name                  */
#define VW_MFR          16
#define VW_MODEL        16
#define VW_QTY          6
#define VW_AMT          11
#define OW_LATE         1
#define OW_KIND         8
#define OW_QTY          3
#define OW_HOLDER       12
#define OW_CONTACT      10
#define OW_ITEM         11

static const rcol rpt_cols[3][RPT_NCOLS] = {
    {   { RF_ID, W_ID, CF_RIGHT, H(MR_H_ID) },
        { RF_CAT, W_CAT, 0, H(MR_H_CAT) },
        { RF_MFR, LW_MFR, 0, H(MR_H_MFR) },
        { RF_MODEL, LW_MODEL, 0, H(MR_H_MODEL) },
        { RF_SERIAL, LW_SERIAL, 0, H(MR_H_SERIAL) },
        { RF_WORKS, LW_WORKS, CF_WORKS, H(MR_H_WORKS) },
        { RF_QTY, LW_QTY, CF_RIGHT, H(MR_H_QTY) },
        { RF_LOC, LW_LOC, CF_MARK, H(MR_H_LOC) } },
    {   { RF_ID, W_ID, CF_RIGHT, H(MR_H_ID) },
        { RF_CAT, W_CAT, 0, H(MR_H_CAT) },
        { RF_MFR, VW_MFR, 0, H(MR_H_MFR) },
        { RF_MODEL, VW_MODEL, 0, H(MR_H_MODEL) },
        { RF_QTY, VW_QTY, CF_RIGHT, H(MR_H_QTY) },
        { RF_PRICE, VW_AMT, CF_RIGHT, H(MR_H_PAID) },
        { RF_VALUE, VW_AMT, CF_RIGHT, H(MR_H_VALUE) } },
    {   { RF_LATE, OW_LATE, CF_LATE, H(MR_H_LATE) },
        { RF_LOAN, W_ID, CF_RIGHT, H(MR_H_LOAN) },
        { RF_ID, W_ID, CF_RIGHT, H(MR_H_ITEM) },
        { RF_KIND, OW_KIND, CF_KIND, H(MR_H_KIND) },
        { RF_QTY, OW_QTY, CF_RIGHT, H(MR_H_QTY) },
        { RF_HOLDER, OW_HOLDER, CF_HOLDER | CF_MARK, H(MR_H_HOLDER) },
        { RF_CONTACT, OW_CONTACT, 0, H(MR_H_CONTACT) },
        { RF_DUE, W_DATE, CF_DATE, H(MR_H_DUE) },
        { RF_MFR, OW_ITEM, CF_ITEM | CF_MARK, H(MR_H_MM) } }
};

/* Each kind's columns (8, 7 and 9 of them) must fit the line, one
   space after each but the last. */
extern char rpt_cols_fit[1];
extern char rpt_cols_fit[(W_ID + W_CAT + LW_MFR + LW_MODEL + LW_SERIAL +
                          LW_WORKS + LW_QTY + LW_LOC + 7 <= RPT_WIDTH
                          && W_ID + W_CAT + VW_MFR + VW_MODEL + VW_QTY +
                             2 * VW_AMT + 6 <= RPT_WIDTH
                          && OW_LATE + 2 * W_ID + OW_KIND + OW_QTY +
                             OW_HOLDER + OW_CONTACT + W_DATE + OW_ITEM + 8
                                 <= RPT_WIDTH) ? 1 : 2];

/* Valuation total lines and the category table, in the valuation's
   columns: the label spans ID, category and maker, the count takes the
   model's column, then quantity and the two amounts in their columns. */
#define RV_LABELW       (W_ID + 1 + W_CAT + 1 + VW_MFR)
#define RV_NCOL         (RV_LABELW + 1)
#define RV_NW           VW_MODEL
#define RV_QCOL         (RV_NCOL + VW_MODEL + 1)
#define RV_QW           VW_QTY
#define RV_PCOL         (RV_QCOL + VW_QTY + 1)
#define RV_VCOL         (RV_PCOL + VW_AMT + 1)
#define RV_AW           VW_AMT

/* Maker and model joined, as the loans' last column and the second line
   of an item label take them: both fields at their stored width. */
#define RT_MMLEN        (2 * LINK_MFRLEN + 1)

/* Text sizes with the NUL: a total (fmt_i32), a sign and the digits of
   a u32, two of them after a point; a date as shown, YYYY-MM-DD
   (fmt_date). */
#define RT_AMTSZ        (TXT_U32LEN + 3)
#define RT_DATESZ       (MSG_DATEW + 1)

/* A location's path, and a label's path line, fit msg_path's least
   width. */
extern char rpt_path_fits[1];
extern char rpt_path_fits[(RPT_MINLABELW > MSG_PATHCUTW) ? 1 : 2];

/* fx and kx hold value indexes and kbuf offsets in a u8, RF_NONE
   excluded. */
extern char rpt_kx_fits[1];
extern char rpt_kx_fits[(RPT_ROWMAX < RF_NONE && RQ_MAXVALS < RF_NONE)
                        ? 1 : 2];

/* Totals; ovf marks a sum that left the 32-bit range. */
#define OV_QTY          0x01
#define OV_PAID         0x02
#define OV_VAL          0x04

typedef struct {
    u32  n;
    i32  qty;
    i32  paid;
    i32  val;
    u8   ovf;
} rtot;

/* One category of the valuation's far table, the totals first so that
   cat_add copies them alone; its code is kept apart, in near memory
   (codes), where every row's code is looked up. */
typedef struct {
    rtot t;
    char name[LINK_CODENAMELEN + 1];
} rcat;

typedef char rcode[LINK_CATLEN + 1];

#define RPT_NCAT        (2 * LINK_MAXCODES)

/* Kinds of body line (body). */
#define BL_ROW          0
#define BL_HEAD         1       /* a heading: kept off a page's foot    */
#define BL_BLANK        2       /* left out at the top of a page        */

struct rpt {
    pr       *p;
    cmem_blk *cats;             /* RPT_VALUE: RPT_NCAT rcat entries     */
    rcode    *codes;            /* RPT_VALUE: the codes of cats         */
    char     *kbuf;             /* labels: values of K lines            */
    u32       count;            /* records laid out                     */
    u32       late;             /* RPT_LOANS: late loans                */
    rtot      grp;              /* the group being laid out             */
    rtot      all;
    rtot      other;            /* RPT_VALUE: rows of categories that
                                   found no room in cats                */
    u16       page;             /* pages started                        */
    u16       ncat;             /* entries of cats                      */
    u16       ngiven;           /* of which given by rpt_cat            */
    u16       clast;            /* the entry cat_find found last        */
    u16       kused;            /* bytes of kbuf in use                 */
    u8        kind;
    u8        opts;
    u8        pagelen;
    u8        labelw;
    u8        labelh;
    u8        body;             /* body lines of a page                 */
    u8        line;             /* body lines on the current page       */
    u8        open;             /* a page is started                    */
    u8        ingroup;          /* a group heading has been written     */
    u8        incat;            /* the category table has begun         */
    u8        hasf;             /* an F line has named the columns      */
    u8        fx[RF_N];         /* value index in R lines, or RF_NONE   */
    u8        kx[RF_N];         /* offset in kbuf, or RF_NONE           */
    char      ghead[LINK_LOCPATHLEN + 1];   /* the group's location
                                   path, "" for no location            */
    char      date[RT_DATESZ];  /* YYYY-MM-DD                           */
    char      sub[RPT_SUBLEN + 1];
};

/* An empty value, for fields a line does not hold. */
static const char rpt_empty[] = "";

/* ---------------------------------------------------------------------
 * Text helpers.
 * --------------------------------------------------------------------- */

/* Fill line with RPT_WIDTH spaces, NUL after them. */
static void blank_line(char *line)
{
    memset(line, ' ', RPT_WIDTH);
    line[RPT_WIDTH] = '\0';
}

/* Put s into line at col, at most w characters, left- or right-aligned
   (right: a longer value keeps its first w characters as well). */
static void put_at(char *line, u8 col, u8 w, const char *s, u8 fl)
{
    size_t n;
    u8 at;

    n = strlen(s);
    at = col;
    if (col >= RPT_WIDTH)
        return;
    if (col + w > RPT_WIDTH)
        w = (u8)(RPT_WIDTH - col);
    if (n > w)
        n = w;
    if (fl & CF_RIGHT)
        at = (u8)(col + w - n);
    memcpy(line + at, s, n);
    if ((fl & CF_MARK) && n > 0 && s[n] != '\0')
        line[at + n - 1] = MSG_CUTMARK;
}

/* Fill w columns at col with '*': a total that does not fit. */
static void stars_at(char *line, u8 col, u8 w)
{
    if (col + w > RPT_WIDTH)
        w = (u8)(RPT_WIDTH - col);
    memset(line + col, '*', w);
}

/* v, or v cents with two decimals, into buf (RT_AMTSZ bytes). */
static void fmt_i32(char *buf, i32 v, int cents)
{
    char *d;
    u32 u;
    u16 n;

    d = buf;
    if (v < 0) {
        *d++ = '-';
        u = (u32)0 - (u32)v;
    } else {
        u = (u32)v;
    }
    if (!cents) {
        txt_u32(d, u);
        return;
    }
    n = txt_u32(d, u / 100);
    d[n] = '.';
    d[n + 1] = (char)('0' + (int)(u % 100 / 10));
    d[n + 2] = (char)('0' + (int)(u % 10));
    d[n + 3] = '\0';
}

/* A total into its column, right-aligned, or '*' marks. */
static void total_at(char *line, u8 col, u8 w, i32 v, int cents, int ovf)
{
    char buf[RT_AMTSZ];

    fmt_i32(buf, v, cents);
    if (ovf || strlen(buf) > w)
        stars_at(line, col, w);
    else
        put_at(line, col, w, buf, 1);
}

/* The quantity and the two amounts of t into the valuation's columns. */
static void totals_at(char *line, const rtot *t)
{
    total_at(line, RV_QCOL, RV_QW, t->qty, 0, t->ovf & OV_QTY);
    total_at(line, RV_PCOL, RV_AW, t->paid, 1, t->ovf & OV_PAID);
    total_at(line, RV_VCOL, RV_AW, t->val, 1, t->ovf & OV_VAL);
}

/* A date YYYYMMDD as YYYY-MM-DD into out (RT_DATESZ bytes); anything
   else is copied as it is, cut to MSG_DATEW characters. */
static void fmt_date(char *out, const char *s)
{
    if (!msg_date(out, s)) {
        strncpy(out, s, MSG_DATEW);
        out[MSG_DATEW] = '\0';
    }
}

/* Two words, maker and model or a label and an ID, one space between
   them when both are given, into out of size bytes. */
static void join_item(char *out, u16 size, const char *mfr,
                      const char *model)
{
    size_t n;

    n = strlen(mfr);
    if (n >= size)
        n = (size_t)(size - 1);
    memcpy(out, mfr, n);
    if (n > 0 && model[0] != '\0' && n + 1 < size)
        out[n++] = ' ';
    out[n] = '\0';
    strncat(out, model, (size_t)(size - 1) - n);
}

/* A whole number of the protocol: optional '-', digits.  Returns 0, or 1
   when it does not fit 32 bits; *v receives it (0 when blank). */
static int parse_num(const char *s, i32 *v, int cents)
{
    u32 u;
    u8 neg;
    u8 dec;
    int frac;

    u = 0;
    neg = 0;
    dec = 0;
    frac = -1;
    while (*s == ' ')
        s++;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    for (; *s != '\0'; s++) {
        if (*s == '.' && cents && frac < 0) {
            frac = 0;
            continue;
        }
        if (*s < '0' || *s > '9')
            break;
        if (frac >= 0) {
            if (frac == 2)
                continue;
            frac++;
        }
        if (u > (u32)0x7FFFFFFFL / 10)
            return 1;
        u = u * 10 + (u32)(*s - '0');
        if (u > (u32)0x7FFFFFFFL)
            return 1;
    }
    if (cents) {
        for (dec = (u8)(frac < 0 ? 0 : frac); dec < 2; dec++) {
            if (u > (u32)0x7FFFFFFFL / 10)
                return 1;
            u *= 10;
        }
    }
    *v = neg ? -(i32)u : (i32)u;
    return 0;
}

/* a += b; 1 when the sum leaves the 32-bit range (a then unchanged). */
static int add_i32(i32 *a, i32 b)
{
    if ((b > 0 && *a > (i32)0x7FFFFFFFL - b) ||
        (b < 0 && *a < -(i32)0x7FFFFFFFL - 1 - b))
        return 1;
    *a += b;
    return 0;
}

/* Add one record to t: quantity and amounts, ovf the parse overflows. */
static void tot_add(rtot *t, i32 qty, i32 paid, i32 val, u8 ovf)
{
    t->n++;
    t->ovf = (u8)(t->ovf | ovf);
    if (add_i32(&t->qty, qty))
        t->ovf |= OV_QTY;
    if (add_i32(&t->paid, paid))
        t->ovf |= OV_PAID;
    if (add_i32(&t->val, val))
        t->ovf |= OV_VAL;
}

/* ---------------------------------------------------------------------
 * Output.
 * --------------------------------------------------------------------- */

/* Send line with style, its trailing spaces cut, and end it. */
static void emit(rpt *r, const char *line, u8 style)
{
    size_t n;

    n = strlen(line);
    while (n > 0 && line[n - 1] == ' ')
        n--;
    if (n > 0) {
        pr_style(r->p, style);
        pr_text(r->p, line, (u16)n);
        pr_style(r->p, 0);
    }
    pr_eol(r->p);
}

/* The heading of the group of location path head ("" for no location),
   marked as continued when cont is 1; a path cut to fit keeps its
   nearest names. */
static void group_title(char *line, const char *head, int cont)
{
    char t[RPT_WIDTH + 1];
    u16 w;

    blank_line(line);
    if (head[0] == '\0')
        txt_copy(t, msg(MR_NOLOC), (u16)sizeof t);
    else
        strcpy(t, head);
    w = (u16)(RPT_WIDTH - (cont ? strlen(msg_sub(MR_CONT, "", NULL)) : 0));
    if (w > MSG_PATHCUTW && strlen(t) > w)
        msg_path(t, head, w);
    put_at(line, 0, RPT_WIDTH, cont ? msg_sub(MR_CONT, t, NULL) : t, 0);
}

/* The category table's column headings into line, which is blank. */
static void cat_heads(char *line)
{
    put_at(line, 0, W_CAT, msg(MR_H_CAT), 0);
    put_at(line, W_CAT + 1, RV_LABELW - W_CAT - 1, msg(MR_H_NAME), 0);
    put_at(line, RV_NCOL, RV_NW, msg(MR_H_ITEMS), 1);
    put_at(line, RV_QCOL, RV_QW, msg(MR_H_QTY), 1);
    put_at(line, RV_PCOL, RV_AW, msg(MR_H_PAID), 1);
    put_at(line, RV_VCOL, RV_AW, msg(MR_H_VALUE), 1);
}

/* The page header: title line, subtitle, a blank line, the column
   headings, a rule. */
static void page_header(rpt *r)
{
    char line[RPT_WIDTH + 1];
    char num[TXT_U32LEN + 1];
    const rcol *c;
    const char *t;
    u16 title;
    u8 i, col;
    size_t n;

    c = rpt_cols[r->kind - 1];
    r->page++;
    title = r->kind == RPT_LIST ? MR_T_LIST
          : r->kind == RPT_VALUE ? MR_T_VALUE
          : (r->opts & RPT_O_OVERDUE) ? MR_T_OVERDUE : MR_T_LOANS;
    blank_line(line);
    t = msg(MT_PROG);
    n = strlen(t);
    put_at(line, 0, RPT_WIDTH, t, 0);
    if (n + 2 < RPT_WIDTH)
        put_at(line, (u8)(n + 2), RPT_WIDTH, msg(title), 0);
    txt_u32(num, r->page);
    t = msg_sub(MR_PAGE, r->date, num);
    n = strlen(t);
    if (n > RPT_WIDTH)
        n = RPT_WIDTH;
    put_at(line, (u8)(RPT_WIDTH - n), (u8)n, t, 0);
    emit(r, line, PR_BOLD);
    emit(r, r->sub, 0);
    pr_eol(r->p);
    blank_line(line);
    if (r->incat) {
        cat_heads(line);
    } else {
        for (i = 0, col = 0; i < RPT_NCOLS && c[i].w != 0; i++) {
            put_at(line, col, c[i].w,
                   msg((u16)(MSG_R_REPORT + c[i].head)),
                   (u8)(c[i].fl & CF_RIGHT));
            col = (u8)(col + c[i].w + 1);
        }
    }
    emit(r, line, PR_BOLD);
    memset(line, '-', RPT_WIDTH);
    emit(r, line, 0);
}

/* End the page when one is started. */
static void page_end(rpt *r)
{
    if (r->open) {
        pr_ff(r->p);
        r->open = 0;
        r->line = 0;
    }
}

/* One body line of kind what (BL_). */
static void body(rpt *r, const char *line, u8 style, u8 what)
{
    char head[RPT_WIDTH + 1];

    if (what == BL_BLANK && !r->open)
        return;
    if (what == BL_HEAD && r->open && r->body - r->line <= RPT_KEEP)
        page_end(r);
    if (!r->open) {
        page_header(r);
        r->open = 1;
        r->line = 0;
        if (what != BL_HEAD && (r->ingroup || r->incat)) {
            if (r->incat) {
                blank_line(head);
                put_at(head, 0, RPT_WIDTH,
                       msg_sub(MR_CONT, msg(MR_BYCAT), NULL), 0);
            } else {
                group_title(head, r->ghead, 1);
            }
            emit(r, head, PR_BOLD);
            r->line++;
        }
    }
    emit(r, what == BL_BLANK ? rpt_empty : line, style);
    r->line++;
    if (r->line >= r->body)
        page_end(r);
}

static void body_blank(rpt *r)
{
    body(r, rpt_empty, 0, BL_BLANK);
}

/* "Items: n   Quantity: q" (RPT_LIST) or the valuation's total line
   labelled with message lab, for totals t; there a count whose
   MR_NITEMS text is wider than its column is written alone,
   right-aligned as under MR_H_ITEMS. */
static void total_line(rpt *r, const rtot *t, u16 lab, u16 listmsg)
{
    char line[RPT_WIDTH + 1];
    char n[TXT_U32LEN + 1];
    char q[RT_AMTSZ];
    const char *s;

    blank_line(line);
    txt_u32(n, t->n);
    if (r->kind == RPT_LIST) {
        if (t->ovf & OV_QTY)
            strcpy(q, "*");
        else
            fmt_i32(q, t->qty, 0);
        put_at(line, 0, RPT_WIDTH, msg_sub(listmsg, n, q), 0);
    } else {
        put_at(line, 0, RV_LABELW, msg(lab), 0);
        s = msg_sub(MR_NITEMS, n, NULL);
        if (strlen(s) <= RV_NW)
            put_at(line, RV_NCOL, RV_NW, s, 0);
        else
            put_at(line, RV_NCOL, RV_NW, n, 1);
        totals_at(line, t);
    }
    body(r, line, 0, BL_ROW);
}

static void group_end(rpt *r)
{
    total_line(r, &r->grp, MR_SUBV, MR_SUBL);
    body_blank(r);
    r->ingroup = 0;
}

void rpt_group(rpt *r, const char *path)
{
    char line[RPT_WIDTH + 1];

    if (!(r->opts & RPT_O_GROUP))
        return;
    if (r->ingroup)
        group_end(r);
    txt_copy(r->ghead, path, (u16)sizeof r->ghead);
    memset(&r->grp, 0, sizeof r->grp);
    group_title(line, r->ghead, 0);
    body(r, line, PR_BOLD, BL_HEAD);
    r->ingroup = 1;
}

/* ---------------------------------------------------------------------
 * Category totals (RPT_VALUE).
 * --------------------------------------------------------------------- */

/* The entry of code, or RPT_NCAT when there is none.  The entry found
   last is tried first: rows in location order often repeat it. */
static u16 cat_find(rpt *r, const char *code)
{
    u16 i;

    if (r->clast < r->ncat && strcmp(r->codes[r->clast], code) == 0)
        return r->clast;
    for (i = 0; i < r->ncat; i++)
        if (strcmp(r->codes[i], code) == 0) {
            r->clast = i;
            return i;
        }
    return RPT_NCAT;
}

/* Append an entry with code and name; 0, or PR_ENOMEM when full. */
static int cat_append(rpt *r, const char *code, const char *name)
{
    rcat e;

    if (r->ncat >= RPT_NCAT)
        return PR_ENOMEM;
    memset(&e, 0, sizeof e);
    strncpy(e.name, name, LINK_CODENAMELEN);
    cmem_put(r->cats, (u16)(r->ncat * sizeof(rcat)), &e, sizeof e);
    strncpy(r->codes[r->ncat], code, LINK_CATLEN);
    r->codes[r->ncat][LINK_CATLEN] = '\0';
    r->ncat++;
    return 0;
}

static void cat_add(rpt *r, const char *code, i32 qty, i32 paid, i32 val,
                    u8 ovf)
{
    char c[LINK_CATLEN + 1];
    rtot t;
    u16 off;
    u16 i;

    strncpy(c, code, LINK_CATLEN);
    c[LINK_CATLEN] = '\0';
    i = cat_find(r, c);
    if (i == RPT_NCAT) {
        if (cat_append(r, c, rpt_empty) != 0) {
            tot_add(&r->other, qty, paid, val, ovf);
            return;
        }
        i = (u16)(r->ncat - 1);
    }
    off = (u16)(i * sizeof(rcat));
    cmem_get(r->cats, off, &t, sizeof t);
    tot_add(&t, qty, paid, val, ovf);
    cmem_put(r->cats, off, &t, sizeof t);
}

/* One line of the category table: code and name (or the label of
   message lab when code is NULL), then the totals t, the count bare
   under its MR_H_ITEMS heading. */
static void cat_line(rpt *r, const char *code, const char *name, u16 lab,
                     const rtot *t)
{
    char line[RPT_WIDTH + 1];
    char n[TXT_U32LEN + 1];

    blank_line(line);
    txt_u32(n, t->n);
    if (code != NULL) {
        put_at(line, 0, W_CAT, code, 0);
        put_at(line, W_CAT + 1, RV_LABELW - W_CAT - 1, name, 0);
    } else {
        put_at(line, 0, RV_LABELW, msg(lab), 0);
    }
    put_at(line, RV_NCOL, RV_NW, n, 1);
    totals_at(line, t);
    body(r, line, 0, BL_ROW);
}

/* "Totals by category" and the table's column headings: those of the
   page when the title opens a page, else a line of their own; then one
   line per entry with records, and one for the rows whose category
   found no room, so that the lines add up to the report's total.
   Pages it continues on carry its own headings.  RPT_KEEP leaves room
   below the title for the headings and a first line. */
static void cat_lines(rpt *r)
{
    char line[RPT_WIDTH + 1];
    rcat e;
    u16 i;

    body_blank(r);
    r->incat = 1;
    blank_line(line);
    put_at(line, 0, RPT_WIDTH, msg(MR_BYCAT), 0);
    body(r, line, PR_BOLD, BL_HEAD);
    if (r->line > 1) {
        blank_line(line);
        cat_heads(line);
        body(r, line, PR_BOLD, BL_ROW);
    }
    for (i = 0; i < r->ncat; i++) {
        cmem_get(r->cats, (u16)(i * sizeof(rcat)), &e, sizeof e);
        if (e.t.n != 0)
            cat_line(r, r->codes[i], e.name, 0, &e.t);
    }
    if (r->other.n != 0)
        cat_line(r, NULL, NULL, MR_OTHER, &r->other);
}

/* ---------------------------------------------------------------------
 * Records.
 * --------------------------------------------------------------------- */

/* 1 when the LATE value s marks a late loan. */
static int is_late(const char *s)
{
    return s[0] == '1' && s[1] == '\0';
}

/* The text of column c of a row of values v (by field), as its flags
   word it, made in buf (RT_MMLEN + 1 bytes) when it is not one of the
   values or a message. */
static const char *cell_text(const rcol *c, const char *const *v,
                             char *buf)
{
    const char *s;
    const char *w;

    s = v[c->f];
    if (c->fl & CF_DATE) {
        fmt_date(buf, s);
        s = buf;
    } else if (c->fl & CF_LATE) {
        s = is_late(s) ? "!" : rpt_empty;
    } else if (c->fl & CF_ITEM) {
        join_item(buf, RT_MMLEN + 1, s, v[RF_MODEL]);
        s = buf;
    } else if (c->fl & (CF_KIND | CF_WORKS)) {
        w = (c->fl & CF_KIND) ? msg_letter(MH_K_NAME, k_kinds, s) :
            msg_letter(MT_WK_Q, k_states, s);
        if (w != NULL)
            s = w;
    } else if ((c->fl & CF_HOLDER) && s[0] == '\0') {
        join_item(buf, RT_MMLEN + 1,
                  msg((u16)(v[RF_KIND][0] == k_kinds[SCR_K_USE] ?
                            MH_L_HOSTUSE : MH_L_HOST)), v[RF_HOST]);
        s = buf;
    }
    return s;
}

/* One report row from the values v (by field). */
static void report_row(rpt *r, const char *const *v)
{
    char line[RPT_WIDTH + 1];
    char buf[RT_MMLEN + 1];
    const rcol *c;
    const char *s;
    u8 i, col;

    c = rpt_cols[r->kind - 1];
    blank_line(line);
    for (i = 0, col = 0; i < RPT_NCOLS && c[i].w != 0; i++) {
        s = cell_text(&c[i], v, buf);
        if ((c[i].fl & CF_RIGHT) && strlen(s) > c[i].w)
            stars_at(line, col, c[i].w);
        else
            put_at(line, col, c[i].w, s, c[i].fl);
        col = (u8)(col + c[i].w + 1);
    }
    body(r, line, 0, BL_ROW);
}

/* One row of the listing or the valuation, with its totals. */
static void items_row(rpt *r, const char *const *v)
{
    i32 qty;
    i32 paid;
    i32 val;
    u8 ovf;

    qty = 0;
    paid = 0;
    val = 0;
    ovf = 0;
    if (parse_num(v[RF_QTY], &qty, 0))
        ovf |= OV_QTY;
    if (r->kind == RPT_VALUE) {
        if (parse_num(v[RF_PRICE], &paid, 1))
            ovf |= OV_PAID;
        if (parse_num(v[RF_VALUE], &val, 1))
            ovf |= OV_VAL;
        cat_add(r, v[RF_CAT], qty, paid, val, ovf);
    }
    tot_add(&r->grp, qty, paid, val, ovf);
    tot_add(&r->all, qty, paid, val, ovf);
    report_row(r, v);
}

/* One label line: text cut to w characters (MSG_CUTMARK ending a cut),
   with style. */
static void label_line(rpt *r, const char *s, u8 style, u8 w)
{
    char line[RPT_MAXLABELW + 1];
    size_t n;

    n = strlen(s);
    if (n > w)
        n = w;
    memcpy(line, s, n);
    line[n] = '\0';
    if (n > 0 && s[n] != '\0')
        line[n - 1] = MSG_CUTMARK;
    emit(r, line, style);
}

/* One label, its lines cut to the label stock and padded to its height. */
static void label(rpt *r, const char *const *v)
{
    char line[RPT_MAXLABELW + 1];
    char buf[RT_MMLEN + 1];
    u8 w;
    u8 max;
    u8 k;
    const char *t;
    size_t n;

    w = r->labelw;
    max = (u8)(r->labelh - 1);
    k = 0;
    if (r->kind == RPT_ILABEL) {
        memset(line, ' ', w);
        line[w] = '\0';
        t = msg_sub(MR_L_NO, v[RF_ID], NULL);
        n = strlen(t);
        memcpy(line, t, n > w ? w : n);
        n = strlen(v[RF_CAT]);
        if (n > w)
            n = w;
        memcpy(line + w - n, v[RF_CAT], n);
        emit(r, line, PR_BOLD);
        k++;
        if (k < max) {
            join_item(buf, sizeof buf, v[RF_MFR], v[RF_MODEL]);
            label_line(r, buf, 0, w);
            k++;
        }
        if (k < max) {
            label_line(r, v[RF_SERIAL][0] != '\0'
                       ? msg_sub(MR_L_SN, v[RF_SERIAL], NULL) : rpt_empty,
                       0, w);
            k++;
        }
        if (k < max) {
            label_line(r, v[RF_LOC][0] != '\0'
                       ? msg_sub(MR_L_LOC, v[RF_LOC], NULL) : rpt_empty,
                       0, w);
            k++;
        }
    } else {
        /* Double width: the name takes two columns a character. */
        label_line(r, v[RF_LOC], PR_BOLD | PR_WIDE, (u8)(w / 2));
        k++;
        if (k < max && v[RF_LCODE][0] != '\0') {
            label_line(r, msg_sub(MR_L_CODE, v[RF_LCODE], NULL), 0, w);
            k++;
        }
        if (k < max) {
            msg_path(line, v[RF_PATH], w);
            label_line(r, line, 0, w);
            k++;
        }
        if (k < max) {
            label_line(r, v[RF_LTNAME], 0, w);
            k++;
        }
    }
    for (; k < r->labelh; k++)
        pr_eol(r->p);
}

/* Lay out one record from the values v (by field). */
static void record(rpt *r, const char *const *v)
{
    r->count++;
    switch (r->kind) {
    case RPT_LIST:
    case RPT_VALUE:
        items_row(r, v);
        break;
    case RPT_LOANS:
        if (is_late(v[RF_LATE]))
            r->late++;
        report_row(r, v);
        break;
    default:
        label(r, v);
        break;
    }
}

/* The field number of name among those kind reads, or RF_NONE. */
static u8 field_of(const rpt *r, const char *name)
{
    u8 i;

    for (i = 0; i < RF_N; i++)
        if ((rpt_need[r->kind - 1] & RB(i)) && strcmp(rpt_fname[i], name) == 0)
            return i;
    return RF_NONE;
}

/* ---------------------------------------------------------------------
 * Interface.
 * --------------------------------------------------------------------- */

static u8 clamp(u8 v, u8 lo, u8 hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Free r and everything it holds; returns pr_failed of the job. */
static int rpt_free(rpt *r)
{
    int rc;

    rc = pr_failed(r->p);
    if (r->cats != NULL)
        cmem_free(r->cats);
    free(r->codes);
    free(r->kbuf);
    free(r);
    return rc;
}

int rpt_open(rpt **out, u8 kind, u8 opts, pr *p, const rpt_geom *g,
             const char *date, const char *sub)
{
    rpt *r;

    *out = NULL;
    if (kind < RPT_LIST || kind > RPT_BLABEL)
        return RPT_EKIND;
    r = (rpt *)malloc(sizeof *r);
    if (r == NULL)
        return PR_ENOMEM;
    memset(r, 0, sizeof *r);
    r->p = p;
    r->kind = kind;
    r->opts = opts;
    r->pagelen = clamp(g->pagelen, RPT_MINPAGE, RPT_MAXPAGE);
    r->labelw = clamp(g->labelw, RPT_MINLABELW, RPT_MAXLABELW);
    r->labelh = clamp(g->labelh, RPT_MINLABELH, RPT_MAXLABELH);
    r->body = (u8)(r->pagelen - RPT_TOP - RPT_BOTTOM);
    memset(r->fx, RF_NONE, sizeof r->fx);
    memset(r->kx, RF_NONE, sizeof r->kx);
    fmt_date(r->date, date != NULL ? date : rpt_empty);
    if (sub != NULL)
        strncpy(r->sub, sub, RPT_SUBLEN);
    if (kind == RPT_VALUE) {
        r->cats = cmem_alloc((u16)(RPT_NCAT * sizeof(rcat)));
        r->codes = (rcode *)malloc(RPT_NCAT * sizeof(rcode));
        if (r->cats == NULL || r->codes == NULL) {
            rpt_free(r);
            return PR_ENOMEM;
        }
    }
    if (kind >= RPT_ILABEL) {
        r->kbuf = (char *)malloc(RPT_ROWMAX);
        if (r->kbuf == NULL) {
            rpt_free(r);
            return PR_ENOMEM;
        }
    } else {
        pr_form(p, r->pagelen);
    }
    *out = r;
    return PR_OK;
}

int rpt_cat(rpt *r, const char *code, const char *name)
{
    if (r->kind != RPT_VALUE)
        return 0;
    if (r->ngiven >= LINK_MAXCODES)
        return PR_ENOMEM;
    r->ngiven++;
    return cat_append(r, code, name);
}

int rpt_line(rpt *r, const rq_line *ln)
{
    const char *v[RF_N];
    u16 i;
    u8 f;
    size_t n;

    if (ln->kind == 'F') {
        r->hasf = 1;
        memset(r->fx, RF_NONE, sizeof r->fx);
        for (i = 0; i < ln->n; i++) {
            f = field_of(r, ln->v[i]);
            if (f != RF_NONE)
                r->fx[f] = (u8)i;
        }
    } else if (ln->kind == 'R' && r->hasf) {
        for (f = 0; f < RF_N; f++)
            v[f] = r->fx[f] != RF_NONE && r->fx[f] < ln->n
                 ? ln->v[r->fx[f]] : rpt_empty;
        record(r, v);
    } else if (ln->kind == 'K' && r->kbuf != NULL && ln->n >= 2) {
        f = field_of(r, ln->v[0]);
        n = strlen(ln->v[1]);
        if (f != RF_NONE && r->kx[f] == RF_NONE &&
            r->kused + n + 1 <= RPT_ROWMAX) {
            r->kx[f] = (u8)r->kused;
            memcpy(r->kbuf + r->kused, ln->v[1], n + 1);
            r->kused = (u16)(r->kused + n + 1);
        }
    }
    return pr_failed(r->p);
}

int rpt_record(rpt *r)
{
    const char *v[RF_N];
    u8 f;

    if (r->kbuf != NULL) {
        for (f = 0; f < RF_N; f++)
            v[f] = r->kx[f] != RF_NONE ? r->kbuf + r->kx[f] : rpt_empty;
        record(r, v);
        memset(r->kx, RF_NONE, sizeof r->kx);
        r->kused = 0;
    }
    return pr_failed(r->p);
}

u32 rpt_count(rpt *r)
{
    return r->count;
}

int rpt_close(rpt *r)
{
    char line[RPT_WIDTH + 1];
    char n[TXT_U32LEN + 1];
    char m[TXT_U32LEN + 1];

    if (r == NULL)
        return PR_OK;
    if (r->kind < RPT_ILABEL) {
        if (r->count == 0) {
            blank_line(line);
            put_at(line, 0, RPT_WIDTH, msg(MR_NONE), 0);
            body(r, line, 0, BL_ROW);
        } else if (r->kind == RPT_LOANS) {
            body_blank(r);
            blank_line(line);
            txt_u32(n, r->count);
            txt_u32(m, r->late);
            put_at(line, 0, RPT_WIDTH, msg_sub(MR_NLOANS, n, m), 0);
            body(r, line, 0, BL_ROW);
        } else {
            if (r->ingroup)
                group_end(r);
            else
                body_blank(r);
            total_line(r, &r->all, MR_TOTV, MR_TOTL);
            if (r->kind == RPT_VALUE)
                cat_lines(r);
        }
        page_end(r);
    }
    return rpt_free(r);
}

int rpt_abort(rpt *r)
{
    char line[RPT_WIDTH + 1];

    if (r == NULL)
        return PR_OK;
    if (r->kind < RPT_ILABEL) {
        blank_line(line);
        put_at(line, 0, RPT_WIDTH, msg(MR_STOPPED), 0);
        body(r, line, PR_BOLD, BL_ROW);
        page_end(r);
    }
    return rpt_free(r);
}
