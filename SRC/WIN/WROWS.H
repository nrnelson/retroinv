/*
 * WROWS.H - the rows of INVWIN's lists and their columns, and the item
 * form's pages (WROWS.C).
 *
 * INVWIN's lists page the commands of the shared sources (itl_src of
 * ITEMLS.H, scr_assy_src of SCRAFORM.H, scr_loc_src of SCRLFORM.H,
 * scr_maint_src and scr_maint_isrc of SCRMFORM.H, scr_loan_src of
 * SCRNFORM.H, scr_user_src and scr_audit_src of SCRZFORM.H), copied as
 * templates: their commands, orders and FIELDS= lists, INVWIN's rows and
 * column headers; the categories' list pages CAT LIST, which no source
 * is shared for.  A row here is made with lv_col of PAGER.H, each field
 * at its full width, within LV_ROWTEXT.  Each table below lists its
 * columns in the row's order: the header's word (the table's head, a
 * string of WROWS.RC numbered in WROWSTR.H), the cells, WCOL_RIGHT (R)
 * or WCOL_INDENT (I), the default width (wcol of WCOLS.H), and what the
 * column shows, the field of that name as the server sends it unless it
 * says otherwise.
 * A header word is never wider than its column's cells, and a default
 * width never narrower, in ANSI_VAR_FONT, than the header word or the
 * widest value of a fixed form (a date, a number, a kind's or a works
 * state's word); free text, a name, may be clipped.  A column of names
 * kept in capitals (a user, a group, a field) has the default width 0,
 * its cells and gap, which hold a name of average capitals whole; a
 * category's code, four capitals, has room for wide ones (MOBO).
 * A date is shown in wrow_intl's short date, a quantity, cost or
 * capacity with its separators (INTL.H); the cells of a quantity and a
 * cost hold them (INTL_NUMW), a capacity's (SCR_LT_CAPW) has room.
 *
 * wrow_item_cols, a table per order of itl_src, the serial number
 * order's last; WROW_S_ITEM:
 *   ID         LINK_IDLEN       R 11  ITEM_ID
 *   Category   WROW_CATW          16  CATEGORY, a blank and its name
 *                                     from cats; the code alone when
 *                                     cats names it not
 *   Maker      LINK_MFRLEN        20  MFR
 *   Model      LINK_MFRLEN        20  MODEL
 *   Works      IC_WORKS            8  WORKS's word (ITEMLS.H)
 *   Qty        WROW_QTYW        R  8  QTY
 *   Location   LINK_LOCNAMELEN    20  LOC_NAME
 *   Status     IC_WHY             11  AVAIL's name, blank for Y
 * WROW_S_SER:
 *   ID         LINK_IDLEN       R 11  ITEM_ID
 *   Maker      LINK_MFRLEN        20  MFR
 *   Model      LINK_MFRLEN        20  MODEL
 *   Serial number LINK_SERIALLEN  20  SERIALNO
 *   Location   LINK_LOCNAMELEN    20  LOC_NAME
 * wrow_ipick_cols is the first WROW_PICKCOLS columns of the first table.
 *
 * wrow_part_cols, WROW_S_PART:
 *   ID         LINK_IDLEN       R 11  ITEM_ID
 *   Category   WROW_CATW          16  as an item's
 *   Maker      WROW_PARTW       I 24  MFR after AC_INDENT blanks a LEVEL
 *                                     below the first (SCRAFORM.H), at
 *                                     most WROW_BOMLEVELS levels
 *   Model      LINK_MFRLEN        20  MODEL
 *   Qty        WROW_QTYW        R  8  QTY
 *   Location   LINK_LOCNAMELEN    20  LOC_NAME
 *   Status     WROW_STATW          8  STATUS
 *
 * wrow_tree_cols, WROW_S_TREE:
 *   Name       WROW_TREEW       I 30  LOC_NAME after SCR_LT_INDENT
 *                                     blanks a DEPTH below the first,
 *                                     at most LINK_MAXLOCDEPTH levels
 *   Type       LINK_CODENAMELEN   16  LTYPE's name from types, the code
 *                                     when types names it not
 *   Code       LINK_LOCLEN        11  LOC_CODE
 *   Capacity   SCR_LT_CAPW      R 11  CAPACITY
 * wrow_tpick_cols is its first WROW_TPICKCOLS columns.
 *
 * wrow_svc_cols[0], MAINT RECENT's rows, WROW_S_SVC; wrow_svc_cols[1],
 * an item's (MAINT LIST), the same but Item, WROW_S_SVCI:
 *   Rec        LINK_IDLEN       R 11  MAINT_ID
 *   Item       LINK_IDLEN       R 11  ITEM_ID
 *   Date       MSG_DATEW          13  MDATE as a date
 *   Type       WROW_MTYPEW         7  MTYPE
 *   Cost       WROW_COSTW       R 11  COST
 *   Summary    MAINT_SUMW         60  SUMMARY
 *
 * wrow_loan_cols[0], the rows of every item (each view of Loans and
 * holds, an item's host's), WROW_S_LOAN; wrow_loan_cols[1], an item's
 * own, the same but Item, Maker and Model, WROW_S_LOANI:
 *   !          1                   3  "!" when scr_loan_mark marks it
 *   Loan       LC_ID            R 11  LOAN_ID
 *   Kind       LC_KIND            11  KIND's name (MH_K_NAME on)
 *   Item       LC_ID            R 11  ITEM_ID
 *   Qty        WROW_QTYW        R  8  QTY
 *   Holder     LOAN_NAMEW         20  HOLDER, or without one the host
 *                                     as scr_loan_fmt names it
 *   Due        MSG_DATEW          13  DUE_DATE as a date
 *   Ended      MSG_DATEW          13  RET_DATE as a date
 *   Maker      LINK_MFRLEN        20  MFR
 *   Model      LINK_MFRLEN        20  MODEL
 * The kind starts at LC_KINDAT and the item at LC_ITEMAT, as in INV's
 * rows, so scr_loan_rowkind and scr_loan_rowitem read them back.
 *
 * wrow_user_cols, WROW_S_USER:
 *   User       LINK_USERLEN        0  USERNAME
 *   Full name  LINK_FULLNAMELEN   24  FULLNAME
 *   Group      LINK_GROUPLEN       0  GROUPNAME
 *   Level      UC_LEVEL         R  7  LEVEL
 *   Status     UC_STATUS           8  STATUS
 *   Chg        UC_MUSTCHG          6  MUSTCHG
 *   Last login MSG_DATEW          13  LASTLOGIN as a date
 *
 * wrow_audit_cols, WROW_S_AUDIT:
 *   Date       MSG_DATEW          13  ADATE as a date
 *   Time       WROW_TIMEW         10  ATIME
 *   User       LINK_USERLEN        0  USERNAME
 *   Station    LINK_STALEN        14  STATION
 *   Table      WROW_TBLW           9  TBL
 *   Record     LINK_IDLEN       R 11  REC_ID
 *   A          1                   3  ACTION
 *   Field      LINK_NAMELEN        0  FLD
 *   Old value  WROW_AUDVALW       30  OLDVAL
 *   New value  WROW_AUDVALW       30  NEWVAL
 *
 * wrow_code_cols, the categories, WROW_S_CODE, CAT LIST's values in
 * their order (CODETAB.H):
 *   Code       LINK_CATLEN         9  the code
 *   Name       LINK_CODENAMELEN   20  its name
 *   Order      WROW_ORDW        R  8  its sort order
 *
 * The default widths put an item list's columns up to its location in
 * the list window of a 640-pixel screen, the rest beyond them.
 *
 * The item list's headings name its orders (WCOL_ORDER of WCOLS.H): ID
 * the ID order, Maker MFR, Location LOC (the tree) and Serial number
 * SERIAL; no other column names one.  Each table but the categories',
 * which keeps none, keeps its widths in the row of the store (wcol_tab's
 * keep) that its WROW_K_ names, a pick list the row of the table it is
 * the first columns of, in the order of the WINCOLS. names of CFG.H.
 */
#ifndef WROWS_H
#define WROWS_H

#include "COMPAT.H"
#include "CWIN.H"
#include "PAGER.H"
#include "CODETAB.H"
#include "INTL.H"
#include "ITEMLS.H"
#include "SCRAFORM.H"
#include "SCRLFORM.H"
#include "SCRMFORM.H"
#include "SCRNFORM.H"
#include "WCOLS.H"
#include "WFORM.H"
#include "WROWSTR.H"

/* The levels a part's maker is indented, as INV's parts list indents
   them: a deeper part is indented as this level. */
#define WROW_BOMLEVELS  8

/* Cells of columns that are no field's width alone: the category with
   its name; a part's maker and a location's name, each with its deepest
   indent; the words Status, Type and Order, wider than the values under
   them; and fields of the server's AUDIT table that INV shows narrower:
   ATIME (HH:MM:SS), TBL and OLDVAL and NEWVAL. */
#define WROW_CATW       (LINK_CATLEN + 1 + LINK_CODENAMELEN)
#define WROW_PARTW      (AC_INDENT * (WROW_BOMLEVELS - 1) + LINK_MFRLEN)
#define WROW_TREEW      (SCR_LT_INDENT * (LINK_MAXLOCDEPTH - 1) + \
                         LINK_LOCNAMELEN)
#define WROW_STATW      6
#define WROW_MTYPEW     4
#define WROW_TIMEW      8
#define WROW_TBLW       8
#define WROW_AUDVALW    40
#define WROW_ORDW       5

/* Cells of a quantity and a cost with their thousands separators. */
#define WROW_QTYW       INTL_NUMW(LINK_QTYLEN, 0)
#define WROW_COSTW      INTL_NUMW(MAINT_COSTW, MAINT_COSTDEC)

/* The rows of the widths store (WCOLS.H), WROW_NKEEP of them: an
   item's columns (wrow_item_cols' first, wrow_ipick_cols), the serial
   number order's, a part's, a location's (wrow_tree_cols,
   wrow_tpick_cols), a service record's with and without its item, a
   loan's of every item and of one, a user's, an audit row's. */
#define WROW_K_ITEM     1
#define WROW_K_SER      2
#define WROW_K_PART     3
#define WROW_K_TREE     4
#define WROW_K_SVC      5
#define WROW_K_ISVC     6
#define WROW_K_LOAN     7
#define WROW_K_ILOAN    8
#define WROW_K_USER     9
#define WROW_K_AUDIT    10
#define WROW_NKEEP      10

/* Columns of the item and location pick lists. */
#define WROW_PICKCOLS   4
#define WROW_TPICKCOLS  2

extern const wcol_tab wrow_item_cols[ITL_ORDSER + 1];
extern const wcol_tab wrow_ipick_cols;
extern const wcol_tab wrow_part_cols;
extern const wcol_tab wrow_tree_cols;
extern const wcol_tab wrow_tpick_cols;
extern const wcol_tab wrow_svc_cols[2];
extern const wcol_tab wrow_loan_cols[2];
extern const wcol_tab wrow_user_cols;
extern const wcol_tab wrow_audit_cols;
extern const wcol_tab wrow_code_cols;

/* The item form's pages (wf_pages of WFORM.H) of the item form of
   SCRIFORM.H: its ID, maker and model above the tabs; Overview with the
   category, status, type, condition, works state, quantity, held units,
   location, parent, path and tags; Technical with the serial number and
   the description; Acquisition with the date acquired, the source, the
   price, the value and the dates created and updated; Notes with the
   memo.  Its record buttons, WROW_IACTS of them, its parts, service
   records, loans and holds and what is used with it, in the order of
   WS_K_PARTS to WS_K_USED (WSCR.H). */
#define WROW_IACTS      4

extern const wf_pages wrow_item_pages;

/* The Control Panel's formats the rows and the forms show dates and
   numbers in; zeroed (ISO dates, plain numbers) until wrow_intl_read. */
extern intl_fmt wrow_intl;

/* Read wrow_intl from WIN.INI's [intl] (INTL.H), the section's and the
   keys' names and the defaults Windows 3.0 documents for absent keys
   (M/d/yy, '.', ',', iLZero 0) the strings WROW_S_INTL to WROW_S_LZERO
   of inst. */
void wrow_intl_read(HINSTANCE inst);

/* The category code in c (WROW_CATW + 1 bytes) followed, when cats has
   a name for it, by a blank and that name, as the rows of items show
   it; an empty one stays so. */
void wrow_cat(ctab *cats, char *c);

/* Make the row of r, text and key as an lv_fmt_fn of PAGER.H makes
   them, the key the shared source's: an item's, in the order of index
   order of itl_src, its category named from cats; a part's, named from
   cats; a location's, its type named from types; a service record's,
   with its item when withitem is 1 (wrow_svc_cols[0]), else without
   (wrow_svc_cols[1]); a loan's in the list q shows, every item's
   (wrow_loan_cols[0]) when q->item is "", else the item's own
   (wrow_loan_cols[1]); a user's; an audit row's; a category's, its key
   the code. */
void wrow_item(const lv_rec *r, u8 order, ctab *cats, char *text,
               char *key);
void wrow_part(const lv_rec *r, ctab *cats, char *text, char *key);
void wrow_tree(const lv_rec *r, ctab *types, char *text, char *key);
void wrow_svc(const lv_rec *r, int withitem, char *text, char *key);
void wrow_loan(scr_lq *q, const lv_rec *r, char *text, char *key);
void wrow_user(const lv_rec *r, char *text, char *key);
void wrow_audit(const lv_rec *r, char *text, char *key);
void wrow_code(const lv_rec *r, char *text, char *key);

/* The category of the row text, as wrow_code made it (LV_ROWTEXT + 1
   bytes), into *r: its code, name and sort order as the list shows
   them, the entry a code list's commands act on (WCODE.H). */
void wrow_code_get(const char *text, ct_row *r);

#endif
