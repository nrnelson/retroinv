/*
 * SCRNFORM.H - the forms and list rows of loans and holds (SCRNFORM.C),
 * which the loan screens of each client show: INV's (SCRLOAN.C) and the
 * Windows client's.
 *
 * A row of LOANS takes units of an item out of stock in one of three
 * kinds (SCR_K_): lent to a borrower; reserved for a purpose, a person
 * or another item, its host; or in use with something or with a host.
 * Each kind has its form, built by scr_loan_form from one table of
 * fields: LOAN_ID, ITEM_ID, WHAT, QTY, HOLDER, HOST_ID, HOSTWHAT,
 * CONTACT, OUT_DATE, DUE_DATE, RET_DATE and NOTES, a loan lacking the
 * host's two and a use DUE_DATE, so a client reaches them by name.
 * LOAN_ID is FF_RO, set by the server; ITEM_ID, the FF_KEY field the
 * kind's command takes as its word, is FF_RO too, preset by the client
 * for a new row; WHAT and HOSTWHAT are local FF_RO texts without a
 * label, the maker and model of the item and of the host, which
 * scr_loan_kitem and scr_loan_khost fill from ITEM GET; QTY and
 * OUT_DATE are FF_NEWONLY; RET_DATE is FF_RO, set by LOAN RETURN.  A
 * loan's HOLDER and DUE_DATE are FF_REQ; a reservation or a use needs a
 * HOLDER or a HOST_ID, which the server checks.  HOST_ID is an FT_CODE
 * field of LINK_MAXFIND characters, where the host's ID, or a text to
 * find it, is typed (scr_search_item of SCRSFORM.H).
 *
 * The lists are paged LOAN commands with fixed columns (no FIELDS=, so the End
 * walk fetches whole rows).  scr_loan_src is a template, as itl_src of
 * ITEMLS.H is: a client copies it, sets arg to a scr_lq of its own and sets
 * both for what it shows, a view of every item's rows (scr_loan_view), one
 * item's rows (LOAN LIST ITEM=, open and ended, in row order) or a host's
 * (LOAN LIST HOST=, the open rows of the items reserved for it or in use with
 * it).  A row holds a '!' when it is late (open past its due date: the LATE
 * column is 1), which also marks it; then its ID, its kind (MH_K_NAME of
 * ITEMLS.H), in the rows of every item the item's ID, the quantity, the holder
 * (when a row names none, its host's ID after MH_L_HOST, MH_L_HOSTUSE for a
 * use, but blank among the host's own rows), the due date and
 * either the item's maker and model or, for one item's rows and every row of
 * every item, the date the row ended, in the columns of MH_HDR (MH_HDRTAB,
 * MH_HDREND).  Its key is LOAN_ID.
 *
 * A text that depends on the kind is SCR_NKINDS messages in SCR_K_
 * order, so the kind's is the first plus the kind; the texts are
 * messages of SCRNFORM.MSG.
 */
#ifndef SCRNFORM_H
#define SCRNFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "ITEMLS.H"

/* The kinds, in the order of their letters in ITL_WHYS. */
#define SCR_K_LOAN      0
#define SCR_K_RES       1
#define SCR_K_USE       2
#define SCR_NKINDS      3

/* The views of every item's rows (scr_loan_view): the open rows of
   every kind, those of one kind (SCR_LV_KIND + kind), the overdue ones
   and every row. */
#define SCR_LV_OPEN     0
#define SCR_LV_KIND     1
#define SCR_LV_OVER     (SCR_LV_KIND + SCR_NKINDS)
#define SCR_LV_ALL      (SCR_LV_OVER + 1)
#define SCR_NVIEWS      (SCR_LV_ALL + 1)

/* The lists' titles, MH_T_VIEW + view; the column headers of every
   item's rows and of one item's; the texts of no rows: of a view
   (MH_EMPTYOVER for SCR_LV_OVER) or of one item, and of a host; the
   Windows client's titles of an item's rows and of its host's. */
#define MH_T_VIEW       895     /* Open loans and holds                 */
#define MH_T_VLOAN      896
#define MH_T_VRES       897
#define MH_T_VUSE       898
#define MH_T_VOVER      899
#define MH_T_VALL       900
#define MH_HDR          901
#define MH_HDRTAB       902
#define MH_EMPTY        903     /* No loans or holds                    */
#define MH_EMPTYOVER    904     /* Nothing is overdue                   */
#define MH_EMPTYHOST    905     /* Nothing is used with it              */
#define MH_T_ITEM       906     /* Loans and holds of item ^1           */
#define MH_T_HOST       907     /* Used with item ^1                    */

/* The kinds' names, MH_K_NAME + kind, for a row's kind column, are the
   first reasons of ITEMLS.H's. */

/* By kind: the title of a new row's form, also the entry choosing the
   kind for a new row; the title of a saved row's, ^1 its ID; the
   question before a row is ended, ^1 its ID. */
#define MH_T_NEW        914     /* New loan                             */
#define MH_T_NEWRES     915
#define MH_T_NEWUSE     916
#define MH_TITLE        917     /* Loan ^1                              */
#define MH_TITLERES     918
#define MH_TITLEUSE     919
#define MH_Q_END        924     /* Return loan ^1?                      */
#define MH_Q_ENDRES     925
#define MH_Q_ENDUSE     926

/* The notes of a row added, saved, returned (a loan) and released (the
   other kinds), ^2 its title (MH_TITLE's, too long for the ID that
   lv_note of LISTV.H takes as ^1); the label of the date a row ends;
   the title of the choice of a new row's kind. */
#define MH_ADDED        920     /* ^2 added                             */
#define MH_SAVED        921
#define MH_ENDED        922
#define MH_RELEASED     923
#define MH_L_RETDATE    927
#define MH_Q_NEW        928

/* The form's labels; by kind the holder's and the date out's, the due
   date's but for a use, the host's but for a loan (MH_L_HOST + kind -
   SCR_K_RES); the end date's of a loan (MH_L_RET) and of the others. */
#define MH_L_ID         929
#define MH_L_ITEM       930
#define MH_L_QTY        931
#define MH_L_NONE       932
#define MH_L_HOLDER     933
#define MH_L_HOLDRES    934
#define MH_L_HOLDUSE    935
#define MH_L_HOST       936
#define MH_L_HOSTUSE    937
#define MH_L_CONTACT    938
#define MH_L_OUT        939
#define MH_L_OUTRES     940
#define MH_L_OUTUSE     941
#define MH_L_DUE        942
#define MH_L_UNTIL      943
#define MH_L_RET        944
#define MH_L_REL        945
#define MH_L_NOTES      946

/* The item form's line of held units (SCRIFORM.H): the units available
   when none is held, ^1 and ^2 the quantity; the rows of other items
   naming the item as their host, ^1 their count. */
#define MH_AVAIL        947
#define MH_HOSTS        948

/* The column header of every row of every item (scr_lq's ended). */
#define MH_HDREND       949

/* The fields of a kind's form, at most, and the indexes of HOLDER,
   DUE_DATE and RET_DATE in scr_loan_fields, every kind's fields. */
#define SCR_NF_COUNT    12
#define SCR_NF_HOLDER   4
#define SCR_NF_DUE      9
#define SCR_NF_RET      10

/* Widths of a loan's fields (LOANS.DBF). */
#define LOAN_QTYW       5
#define LOAN_NAMEW      30      /* HOLDER, CONTACT                      */
#define LOAN_NOTESW     60

/* Columns of a row (the headers MH_HDR and MH_HDRTAB match them): the
   late mark, the row's ID, the kind (LC_KINDAT, for scr_loan_rowkind),
   in the rows of every item the item's ID (LC_ITEMAT), the quantity,
   the holder (cut in the rows of every item, less where the end date
   takes the place of the maker and model), the due date, then the
   item's maker and model or the end date, the dates MSG_DATEW of MSG.H
   wide.  An item's own rows give the holder LOAN_NAMEW. */
#define LC_ID           8       /* row and item IDs                     */
#define LC_KIND         8
#define LC_QTY          4
#define LC_HOLDER       15
#define LC_NAME         17
#define LC_HOLDEND      (LC_HOLDER + LC_NAME - MSG_DATEW)
#define LC_KINDAT       (1 + 1 + LC_ID + 1)
#define LC_ITEMAT       (LC_KINDAT + LC_KIND + 1)

/* LOAN, the loans' table and the forms' help topic; LOAN_ID, the row's
   ID field and the lists' key; ITEM, the items' table, LOAN LIST's pair
   of the item and the field a refused row of an item names; HOST_ID;
   the item's and the host's maker and model (local); the commands of
   the lists and of the overdue rows. */
extern const char scr_loan_w_loan[];
extern const char scr_loan_k_id[];
extern const char scr_loan_k_item[];
extern const char scr_loan_k_host[];
extern const char scr_loan_k_what[];
extern const char scr_loan_k_hwhat[];
extern const char scr_loan_k_kind[];
extern const form_field scr_loan_fields[];
extern const char scr_loan_w_list[];
extern const char scr_loan_w_over[];

/* The kinds' letters, the KIND values of SCR_K_, are the first of
   ITL_WHYS (ITEMLS.H). */

/* The kind (SCR_K_) of the KIND value v; SCR_K_LOAN for any other. */
u8 scr_loan_kind(const char *v);

/* Kind's form into def and fields (SCR_NF_COUNT entries), which must
   outlive the form: the kind's fields, labelled for it, in INV's pop-up
   box of SCR_POPW columns at SCR_POPTOP, SCR_POPLEFT (SCRSHR.H), its
   rows closed up where a loan has no host, titled MH_T_NEW + kind. */
void scr_loan_form(u8 kind, form_def *def, form_field *fields);

/* Make f, a FORM_NEW form of kind, a new row: QTY 1, the server's
   default, loaded as unchanged, so that it is sent only when changed,
   and the kind's command word (OUT, RESERVE or USE) for its save
   (form_set_add). */
void scr_loan_new(form *f, u8 kind);

/* After LOAN GET has loaded f: a HOST_ID of 0 loaded as empty, so the
   field shows blank; an empty HOST_ID sent is no host too. */
void scr_loan_loaded(form *f);

/* rq_fns of ITEM GET, arg the form: its MFR and MODEL, joined by a
   blank, loaded into WHAT (scr_loan_kitem) or HOSTWHAT
   (scr_loan_khost); MFR comes first and replaces the old value.  Each
   returns 0. */
int scr_loan_kitem(void *arg, const rq_line *ln);
int scr_loan_khost(void *arg, const rq_line *ln);

/* ---------------------------------------------------------------------
 * The lists.
 * --------------------------------------------------------------------- */

/* What a list shows: item's rows (ITEM=, MH_HDRTAB's columns), or, with
   item "", every item's (MH_HDR's, or with ended 1 MH_HDREND's, the
   date a row ended in place of the item's maker and model), narrowed
   to those naming host (HOST=) when it is not "", to one kind (KIND=)
   when kind is below SCR_NKINDS, and to the open rows (OPEN=1) when
   open is 1. */
typedef struct {
    char item[LINK_IDLEN + 1];
    char host[LINK_IDLEN + 1];
    u8   kind;
    u8   open;
    u8   ended;
} scr_lq;

/* The template: LOAN LIST, the functions below, MH_HDR, MH_EMPTY, arg
   NULL. */
extern const lv_src scr_loan_src;

/* Set src, a copy of scr_loan_src, and *q, its arg, to view v (SCR_LV_)
   of every item's rows: the command, its args, the header, the text of
   no rows and *q's kind, open rows and ended (for SCR_LV_ALL), every
   other part of *q cleared. */
void scr_loan_view(lv_src *src, scr_lq *q, u8 v);

/* The source's functions, arg a scr_lq: LOAN LIST's pairs; the row;
   its mark. */
void scr_loan_args(void *arg, rq *q);
void scr_loan_fmt(void *arg, const lv_rec *r, char *text, char *key);
int scr_loan_mark(void *arg, const lv_rec *r);

/* Read back from a row's display text (as scr_loan_fmt made it): its
   kind; in MH_HDR's columns, the ID of its item into id (LINK_IDLEN +
   1 bytes), 1, or 0 when the text holds none. */
u8 scr_loan_rowkind(const char *text);
int scr_loan_rowitem(const char *text, char *id);

#endif
