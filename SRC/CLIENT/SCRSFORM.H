/*
 * SCRSFORM.H - the item search's form table and its searches
 * (SCRSFORM.C), which the search screen of each client runs: INV's
 * (SCRSRCH.C) and the Windows client's.
 *
 * One table of local fields serves both clients' search forms: INV draws it in
 * a box, the Windows client maps the fields to the controls of a dialog by
 * index.  The fields: the text, the serial number, and the filters category,
 * location (scr_search_loc, a location field of SCRLFORM.H: blank for any, "0"
 * for none), its path, "all below" (a logical field: the items of every
 * location inside it too, ALL=1, in tree order), status, available (a logical
 * field), tags (the items carrying every tag typed, their blanks sent as
 * commas by ct_tags_bare of CODETAB.H, a pick list of the tags in use on
 * F4) and works state (blank for
 * any, a state, or "Y?I", every one but known broken), on five rows.  A search
 * runs the first that applies: the
 * text with ITEM FIND and the filters given, else the serial number with ITEM
 * SERIAL, else the filters with ITEM LIST, in location order when a location
 * is given.  A status beside available is refused before any request, as the
 * server refuses STATUS= beside AVAIL=1.  Its results are an item list, rows
 * as ITEMLS.H makes them, with the command's default fields.  The same
 * searches find the items a field naming an item lists to choose from (a new
 * loan or hold, its host, an install), when anything but an ID is typed
 * (scr_search_item): every client's item pick list.
 *
 * The form starts on the text (SCMD_FIND of SCR.H), the serial number
 * (SCMD_SERIAL) or the category (SCMD_FILTER).  A form moves between
 * its fields in a wrapping cycle from its first, in INV's form and in a
 * dialog alike, so the three starts are the same cycle of fields
 * entered at three places: one table holding the cycle and the start of
 * the next turn, and the form definition pointed at the place the
 * command starts on (scr_search_form).  The texts are messages of
 * SCRSRCH.MSG.
 */
#ifndef SCRSFORM_H
#define SCRSFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "FMODEL.H"
#include "CODETAB.H"
#include "SCRLFORM.H"

/* The form's title and labels, the empty results' text, the results'
   titles in the order of the searches (SCR_SQ_) and that of a text
   search with filters, and the refusals of a search (scr_search_why). */
#define MS_TITLE        620     /* Search items                         */
#define MS_L_TEXT       622
#define MS_L_SERIAL     623
#define MS_L_CAT        624
#define MS_L_LOC        625
#define MS_L_STATUS     626
#define MS_L_AVAIL      632
#define MS_L_WHERE      635
#define MS_L_ALL        650
#define MS_L_TAG        643
#define MS_L_WORKS      644
#define MS_NONE         627     /* No items found                       */
#define MS_T_FIND       628     /* Items containing "^1"                */
#define MS_T_SERIAL     629     /* Items with serial number ^1          */
#define MS_T_FILTER     630     /* Items matching the filters           */
#define MS_T_FINDF      633     /* Items containing "^1" matching ...   */
#define MS_EMPTYQ       631     /* nothing to search for                */
#define MS_BOTHQ        634     /* a status beside available            */

/* The names of an item's statuses (A D), conditions (NEW EXC GD FR PR)
   and works states (? Y I N, and Y?I of a filter), each in the order of
   its choices, which the search form's Status and Works and the item
   form's Status, Condition and Works show (form_choice_name of
   FMODEL.H); the report forms have their own (SCRRFORM.H). */
#define MS_C_A          636     /* Active                               */
#define MS_C_D          637
#define MS_C_NEW        638     /* New                                  */
#define MS_C_EXC        639
#define MS_C_GD         640
#define MS_C_FR         641
#define MS_C_PR         642
#define MS_C_WQ         645     /* Not tested                           */
#define MS_C_WY         646
#define MS_C_WI         647
#define MS_C_WN         648
#define MS_C_WK         649     /* Not known broken: Y?I                */

/* The choices of a Works filter, the search form's and the report
   forms', and their longest. */
extern const char scr_works_filt[];
#define SCR_WORKSW      3

/* Indexes of the fields in scr_search_fields, from the text's turn. */
#define SCR_SF_TEXT     0
#define SCR_SF_SERIAL   1
#define SCR_SF_CAT      2
#define SCR_SF_LOC      3
#define SCR_SF_WHERE    4
#define SCR_SF_ALL      5
#define SCR_SF_STATUS   6
#define SCR_SF_AVAIL    7
#define SCR_SF_TAG      8
#define SCR_SF_WORKS    9
#define SCR_SF_COUNT    10      /* the fields of one turn               */

/* The searches, in the order of their commands and result titles. */
#define SCR_SQ_FIND     0       /* the text and filters, ITEM FIND      */
#define SCR_SQ_SERIAL   1       /* the serial number, ITEM SERIAL       */
#define SCR_SQ_FILTER   2       /* the filters, ITEM LIST               */
#define SCR_SQ_NONE     3       /* refused: no value, or a status beside
                                   available (scr_search_why)           */

/* The values of one search, as its requests need them. */
typedef struct {
    char text[LINK_MAXFIND + 1];
    char serial[LINK_SERIALLEN + 1];
    char cat[LINK_CATLEN + 1];
    char loc[LINK_LOCLEN + 1];
    char status[LINK_STATUSLEN + 1];
    char tag[LINK_TAGFILT + 1];
    char works[LINK_NWORKS + 1];
    u8   avail;                 /* 1: available items only (AVAIL=1)    */
    u8   below;                 /* 1: those below the location too
                                   (ALL=1)                              */
    u8   kind;                  /* the search they ask for, SCR_SQ_     */
} scr_query;

/* The cycle of fields, one turn from the text and the start of the
   next, and the form of one turn from the text, titled MS_TITLE, with
   the help topic SEARCH, on INV's work area across the screen. */
extern const form_field scr_search_fields[];
extern const form_def scr_search_def;

/* The location filter and its path, a filter's location field. */
extern const scr_locf scr_search_loc;

/* The command words of each search but SCR_SQ_NONE, by SCR_SQ_. */
extern const char *const scr_search_cmds[];

/* scr_search_def into *def with its fields starting at the one command
   cmd starts on: SCMD_FIND, SCMD_SERIAL or SCMD_FILTER. */
void scr_search_form(form_def *def, u8 cmd);

/* The values of f, a search form, into *q, the tags' blanks made
   commas, and the search they ask for into q->kind, which is also
   returned. */
u8 scr_search_get(scr_query *q, form *f);

/* Why search q, of kind SCR_SQ_NONE, was refused: MS_BOTHQ for a status
   beside available, else MS_EMPTYQ. */
u16 scr_search_why(const scr_query *q);

/* An lv_args_fn adding to a request the parts of the search arg, a
   scr_query: the serial number as its word; or the text as its word,
   if any, then the filters given as CAT=, LOC=, STATUS=, AVAIL=1, TAG=
   and WORKS= pairs, with ALL=1 after a location when below is 1, else
   ORDER=LOC when ITEM LIST has a location. */
void scr_search_args(void *arg, rq *q);

/* The title of the results of search q (a msg_sub result). */
const char *scr_search_title(const scr_query *q);

/* Which items an item field's search lists (scr_search_item). */
#define SCR_SI_ALL      0       /* every item                           */
#define SCR_SI_AVAIL    1       /* the available ones (AVAIL=1)         */
#define SCR_SI_ACTIVE   2       /* the active ones (STATUS=A)           */

/* What was typed into a field naming an item, typed, its blanks at both
   ends left out, into q->text, and *q the search listing the items to
   choose the item from: those holding the text (SCR_SQ_FIND), or, when
   there is none, every item (SCR_SQ_FILTER), of those only says
   (SCR_SI_).  Returns 1 when the text may be an item's ID (1 to
   LINK_IDLEN digits alone): a caller that finds no such item searches
   for the digits as text, as models and serial numbers can be all
   digits. */
int scr_search_item(scr_query *q, const char *typed, u8 only);

/* The ID that q->text, as scr_search_item left it for a text that may
   be an ID, names once the server has the item: the digits without
   their leading zeros, as the server writes an ID ("0" stays). */
const char *scr_search_id(const scr_query *q);

/* The item list's source (itl_src of ITEMLS.H) for the results of search q
   into *src: q's command and parts (scr_search_args, arg q), no orders, for
   no rows MS_NONE, or MI_NOAVAIL (SCRIFORM.H) when q keeps the available
   items only, and its format function, the rows of the item list in its
   first order. */
void scr_search_src(lv_src *src, scr_query *q);

#endif
