/*
 * WFORMT.C - the test of the form dialog (SRC/WIN/WFORM.C): the item
 * form of the seeded data and a form with a field of every type, opened
 * by keys of the frame, which a key script types into and snaps.
 *
 * usage: WIN WFORMT dir /KEYS:WFORMT.KEY /SNAP:WFORMT.SNP
 *
 * dir is a data directory seeded as the screen tests seed theirs (the
 * host INV /CMD /LOCAL dir /NOW:20261008120000 on TEST/KEYS/SEED.REQ),
 * and INV.MSG is in the current directory.  The program opens the LOCAL
 * link on dir and logs in as ADMIN (level WFT_LEVEL) with plain
 * requests.  It reads WIN.INI's International settings as INVWIN does
 * (wrow_intl_read of WROWS.H), so its forms show what cannot be changed
 * in the Control Panel's formats.
 *
 * The frame's title is "what #n": n counts the busy notes (1) of every
 * dialog so far, so a snapshot shows that a request went, and what says
 * what the owner did last: "WFORMT" at the start, then the form, the
 * result of wf_run (WF_ of WFORM.H) and the record's ID, g_id for the
 * form of every type, as in "ADD 1 4".  " BIG" follows once a dialog has
 * not fitted the display floor of WFORM.H (fits), " OFF" once a
 * window of PGUP or PGDN has not been placed as td_placed of TDRV.H
 * says, " CLIP" once a label of a form dialog has not held its text
 * (clips), " VCLIP" once a value has not stood on its label's line,
 * one of a fixed format not held its text, or a control of the form of
 * fixed formats not been as wide in edit mode as in view mode (vclips),
 * " LOOK" once a form dialog has not shown the grey 3D look of WDLG.H
 * (looks), " ROWS" once a pick list (WPICK.H) has not shown WP_ROWS rows
 * of its list box, measured as its rows' requests end (busy), and
 * " KEYS c" once a form dialog's access letters have failed
 * (letters), c the letter shown twice or reserved, or '-' for a field
 * that can be changed whose label shows none though its text holds a
 * letter no control takes.  Once a memo has been saved, " LINES n"
 * follows too: the lines its control showed as OK sent it
 * (EM_GETLINECOUNT), its wrap at its width included, which no snapshot
 * otherwise shows.
 * The frame's keys:
 *
 *   F2   a new item (scr_item_def, FORM_NEW) with a new item's first
 *        values (scr_item_preset); "ADD r id"; with SHIFT, the same
 *        in a copy of the item form whose SERIALNO is FF_NEWONLY too,
 *        without the location field, so that a location typed goes to
 *        the server as it is
 *
 *   F3   item g_id in FORM_EDIT; "EDIT r id"; with CTRL, the same, every
 *        read of the disk failing (cf_fault_rd of COMPAT.H, CF_EACCES)
 *        while its code lists are fetched, so that CAT LIST fails but
 *        leaves the session as it was
 *   F4   item g_id in FORM_VIEW; "VIEW r id"
 *   F5   the form of every type (all_def) in FORM_EDIT; "ALL r id";
 *        with SHIFT, the form of fixed formats (fix_def, below) in
 *        FORM_EDIT: "FIX r id"; with CTRL, the form of a wide line
 *        (wide_def, below) in FORM_EDIT: "WIDE r id"
 *   F6   the same form in FORM_VIEW; "ALL r id"; with SHIFT, the form
 *        of fixed formats in FORM_VIEW: "FIX r id"; with CTRL, a
 *        returned loan in INV's loan form (scr_loan_form of SCRNFORM.H),
 *        its three dates on one row of the table, in FORM_VIEW: "LOAN r
 *        id"
 *   F7   the same form in FORM_NEW; "ALL r id"; with CTRL, the form of
 *        a key (key_def, below) in FORM_EDIT: "KEY r id"
 *   F8   item WFT_GONEID loaded in FORM_EDIT, then deleted, then shown,
 *        so its save finds it gone; "GONE r id"
 *   F9   item g_id loaded in FORM_EDIT, then BYE, then shown without
 *        its location field (SHIFT-F2's), so that its category list's
 *        fetch, before the dialog opens, finds the session ended;
 *        "ENDED r id"; with SHIFT, the BYE is sent as the busy bracket
 *        of that fetch closes, so that the dialog opens with its lists
 *        and its save finds the session ended
 *   F1   the password form (sec_def) in FORM_EDIT, a new one each time:
 *        its save of the test's own (sec_save) refuses the first time,
 *        as a server refuses a field (102 on PASS), and then takes the
 *        values, marking them saved; its memo's save always fails, as
 *        on a disk that is full.  Once the dialog has ended, "SECRET r
 *        WIPED" when PASS's slot holds none of the bytes of k_secret,
 *        the password the script types, over its whole width, and the
 *        form is not dirty, so that its original holds none either,
 *        else "SECRET r HELD"; with SHIFT, the same form in FORM_VIEW,
 *        its first field, PASS, disabled there; with CTRL, the same form
 *        in FORM_EDIT, its first save refused again, but as a server
 *        that finds no record a field names (RQ_ENOTFOUND on PASS)
 *   HOME the frame moved, its size kept, so that its centre is the
 *        screen's top left corner; END, its bottom right one
 *   PGUP a captioned popup owned by the frame, WFT_OVER pixels wider
 *        and taller than the screen, placed by wd_centre of WDLG.H,
 *        checked and destroyed
 *   PGDN the same with a popup WFT_SMALL pixels square over the frame
 *        minimized, then restored
 *   INS  WFT_FILL items added, of category SYS, the last two tagged
 *        k_next and k_last, the others untagged, so that ITEM TAGS reads
 *        them in two pages (ENG_MAXREADS records each), the seed's
 *        included, the first without a tag; "FILL n", n the items
 *        added; with SHIFT, an Esc posted to the next tag pick list as
 *        its tags start to load (busy), which the wait after their
 *        first page then finds, before any tag: "ESC"; with CTRL,
 *        WFT_NCAP items tagged WFT_PERITEM tags each, t00 up, more tags
 *        than a code list holds (LINK_MAXCODES): "CAP n"
 *   DEL  the form dialogs' help set (wf_help): from then on a form
 *        dialog whose table names a topic has its Help button, which
 *        puts "HELP n topic" in what, n 1 when the button's owner is a
 *        form dialog, else 0, and leaves the dialog open; "HELP 0 SET";
 *        with SHIFT, a click posted on tab WFT_CLICKTAB of the next form
 *        dialog's tab strip, at the middle of its rectangle (WTM_GETRECT
 *        of WTAB.H) the first time that dialog is idle: "CLICK 0 ";
 *        with CTRL, the form of reserved letters (res_def, below) in
 *        FORM_EDIT: "RES r id"
 *   Esc  closes the frame
 *
 * g_id is "1" at the start and the ID of each item an ADD gives.  After
 * an ADD or an EDIT that ends with WF_OK, the record is read again with
 * ITEM GET into a new form and shown in FORM_VIEW, the title saying
 * "ADD r id" or "EDIT r id" meanwhile.  Every item read has its CREATED
 * and UPDATED loaded empty: the server sets them from the machine's
 * clock, which differs from run to run.  The item form's memo function
 * gives an empty memo, and it has none to save, so its NOTES is
 * read-only and empty.
 *
 * Every dialog's size is checked against the display floor (fits) the
 * first time its loop is idle, which Windows tells the frame, the
 * dialog's owner, by WM_ENTERIDLE; its place is the driver's to check
 * (td_place).  Each time a form dialog is idle its access letters and
 * labels and the values of fixed formats are checked too (letters,
 * clips, vclips), and the grey of its face and the sunken frames of its
 * fields read back pixel by pixel (looks): the test machine is a VGA
 * with Windows' default colours, whose button face is the stock
 * LTGRAY_BRUSH's light grey.
 *
 * The form of every type is one form, kept from one key to the next, so
 * its values stay: all of them FF_LOCAL, with no table, so OK only
 * checks.  It starts with ID "R-1" (FF_RO) and COND "XX", which is no
 * word of its choices; its memo functions give the text kept in g_memo,
 * at first two lines, the second with a CP437 character, and keep the
 * text OK hands them, but for the first one of more than one line whose
 * first is PART, which fails part-way; in FORM_NEW its memo starts
 * empty, as a new record's does.  Its fields, on two columns:
 *
 *   TEXT    FT_CHAR, FF_REQ          CODE    FT_CODE, FF_UPPER
 *   QTY     FT_NUM, FF_NONNEG        PRICE   FT_NUM with decimals
 *   DATE    FT_DATE                  FLAG    FT_LOGIC
 *   STATUS  FT_CHOICE "A D"          COND    FT_CHOICE, FF_REQ
 *   ID      FT_CHAR, FF_RO           SERIAL  FT_CHAR, FF_NEWONLY
 *   PASS    FT_SECRET
 *   NOTES   FT_MEMO
 *
 * SERIAL's label is a menu entry's, INVMENU.H's MN_BYID, for its '&'.
 * The item form has INVWIN's pages and record buttons (wrow_item_pages
 * of WROWS.H): in FORM_VIEW a record button, clicked or pressed with
 * Space, ends its dialog with WF_ACT + i, which the title gives as r.
 * CODE names no list, so it has no pick list; the item form's Category
 * has the categories' and its Location, its location field
 * (scr_item_loc of SCRIFORM.H), the location tree's.
 *
 * The password form holds PASS and NOTES of the same table.
 *
 * The form of reserved letters, res_def, has a label for each letter a
 * form's buttons and tab strip may reserve that it would otherwise take:
 * File, then F4, whose F is taken, so that it takes its 4, as the Edit
 * button of a form that OK saves (res_ops' table, though it is never
 * saved) has no letter; Holder, whose H is the Help button's once
 * DEL has set the help, Record and New password, whose R and N are those
 * of the strip that its memo, NOTES, gives it, the memo's page "&Notes"
 * after the fields' "&Record".  Its
 * topic is the item form's.
 *
 * The form of fixed formats, fix_def, shows on one page, without a memo,
 * a value of each format a view static must hold whole (WFORM.H), each
 * at its widest: a choice of WFT_WIDEW W's, the widest letter, all
 * digits of a count and of a price, a date and an ID; FF_LOCAL, no table
 * and no topic, so its dialog shows Close alone in view mode.
 *
 * The form of a wide line, wide_def, has one line: TEXT, WFT_WIDEA cells,
 * then SERIAL, WFT_WIDEB, a cell more than an edit control keeps when
 * its line is cut (WF_SHARECELLS of WFORM.C).  The line passes the
 * dialog's widest by so much that SERIAL's share of the cut takes it
 * below that, so it keeps its least, and what its share left is cut
 * from TEXT on the right: the dialog then fits (no " BIG").
 *
 * The form of a key, key_def, holds CODE, FF_KEY, NAME and NOTES of a
 * table, NAME loaded with k_bar, which holds a CP437 line-drawing
 * character that the Windows set lacks, so that a control left alone
 * must not count as changed though its text does not come back the same;
 * its memo's load, as the dialog opens, failing as on a disk that cannot
 * be read (RQ_EIO), and its save of the test's own (key_save) refusing
 * the first time as a server refuses a duplicate it names no field of
 * (RQ_EDUP), then as one whose session has no login (RQ_ENOLOGIN).  Its
 * first busy bracket, its memo's load as the dialog opens, runs the form
 * of every type, which wf_run must refuse while a form dialog is open
 * (WF_EFAIL); the title says "NESTED r id" in place of "KEY r id" when it
 * did not.
 *
 * A failure to start puts "FAIL step n" in what, step the part that
 * failed and n its code, for the snapshot to show.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "ENGINE.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "FMODEL.H"
#include "MEMOIO.H"
#include "INVMENU.H"
#include "SCRIFORM.H"
#include "SCRNFORM.H"
#include "SCRRFORM.H"
#include "SCRZFORM.H"
#include "TDRV.H"
#include "WCOLS.H"
#include "WDLG.H"
#include "WEDIT.H"
#include "WFORM.H"
#include "WLIST.H"
#include "WPICK.H"
#include "WROWS.H"
#include "WTAB.H"

#define WFT_CMDMAX      128     /* a DOS command tail and its NUL       */
#define WFT_WHATSZ      40      /* the title but the count              */
#define WFT_LEVEL       3       /* the access level of the seed's ADMIN */
#define WFT_GONEID      "3"     /* the item F8 deletes                  */
/* The display floor of WFORM.H, EGA, in pixels, and the dialog base
   units of its system font (EGASYS.FON of Windows 3.0): Windows makes
   them its average character width plus one, 7 + 1, and its height,
   12. */
#define WFT_FLOORX      640
#define WFT_FLOORY      350
#define WFT_EGAX        8
#define WFT_EGAY        12
#define WFT_OVER        64      /* pixels PGUP's popup passes the screen */
#define WFT_SMALL       64      /* pixels of a side of PGDN's popup     */
#define WFT_FILL        ENG_MAXREADS    /* items INS adds               */
#define WFT_NCAP        11      /* items CTRL-INS adds, with            */
#define WFT_PERITEM     5       /* tags each                            */
#define WFT_CHARS       256     /* the characters of a set, for letters */
#define WFT_BSTYPE      0x0FL   /* the type (BS_) in a button's style   */
#define WFT_LABELID     0xFFFFu /* a label's control ID (WFORM.H)       */
#define WFT_CLICKTAB    1       /* the tab SHIFT-DEL's click chooses    */
#define WFT_ROWSMSG     (WM_USER + 0)   /* the frame's: measure the pick
                                   list dialog wParam (rows)            */
#define WFT_WIDEW       16      /* characters of fix_def's choice       */
#define WFT_WIDEA       60      /* cells of wide_def's TEXT             */
#define WFT_WIDEB       13      /* and of its SERIAL                    */
#define WFT_FACE        RGB(192, 192, 192)      /* LTGRAY_BRUSH's       */
#define WFT_WHITE       RGB(255, 255, 255)      /* WHITE_PEN's          */

/* Labels and the title of all_def and res_def, messages of INV.MSG that
   the screens keep to themselves: SCRSRCH.C's MS_TITLE, MS_L_TEXT,
   MS_L_LOC and MS_L_STATUS, SCRUSER.C's MU_L_PW, TUI.C's MK_F4 and
   RPT.C's MR_H_HOLDER. */
#define WFT_TITLE       620
#define WFT_L_TEXT      622
#define WFT_L_LOC       625
#define WFT_L_STATUS    626
#define WFT_L_PASS      777
#define WFT_L_F4        353
#define WFT_L_HOLDER    984
#define WFT_IPASS       10      /* PASS's place in all_fields           */
/* The columns of all_def: label and value of each. */
#define WFT_C1          1
#define WFT_V1          14
#define WFT_C2          34
#define WFT_V2          47

static const char k_keys[] = "/KEYS:";
static const char k_snap[] = "/SNAP:";
static const char k_class[] = "WFORMT";
static const char k_item[] = "ITEM";
static const char k_created[] = "CREATED";
static const char k_updated[] = "UPDATED";
static const char k_part[] = "PART";
static const char k_pass[] = "PASS";
static const char k_secret[] = "Sesame42";
static const char k_edit[] = "Edit";
static const char k_static[] = "Static";
static const char k_button[] = "Button";
static const char k_combo[] = "ComboBox";
static const char k_wtab[] = "WTAB";
static const char k_wlist[] = "WLIST";
static const char k_next[] = "next";
static const char k_last[] = "zlast";

static const form_field all_fields[] = {
    { "TEXT", WFT_L_TEXT, 0, WFT_C1, WFT_V1, FT_CHAR, 12, 0,
      FF_LOCAL | FF_REQ, 0, NULL },
    { "CODE", WFT_L_LOC, 0, WFT_C2, WFT_V2, FT_CODE, LINK_LOCLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { "QTY", MI_L_QTY, 1, WFT_C1, WFT_V1, FT_NUM, 5, 0,
      FF_LOCAL | FF_NONNEG, 0, NULL },
    { "PRICE", MI_L_PRICE, 1, WFT_C2, WFT_V2, FT_NUM, LINK_PRICELEN,
      LINK_PRICEDEC, FF_LOCAL, 0, NULL },
    { "DATE", MI_L_ACQ, 2, WFT_C1, WFT_V1, FT_DATE, 10, 0, FF_LOCAL, 0,
      NULL },
    { "FLAG", MQ_L_OVERDUE, 2, WFT_C2, WFT_V2, FT_LOGIC, 1, 0, FF_LOCAL, 0,
      NULL },
    { "STATUS", WFT_L_STATUS, 3, WFT_C1, WFT_V1, FT_CHOICE, 1, 0, FF_LOCAL,
      0, "A D" },
    { "COND", MI_L_COND, 3, WFT_C2, WFT_V2, FT_CHOICE, 3, 0,
      FF_LOCAL | FF_REQ, 0, "NEW EXC GD" },
    { "ID", MI_L_ID, 4, WFT_C1, WFT_V1, FT_CHAR, 8, 0, FF_LOCAL | FF_RO, 0,
      NULL },
    { "SERIAL", MN_BYID, 4, WFT_C2, WFT_V2, FT_CHAR, 10, 0,
      FF_LOCAL | FF_NEWONLY | FF_UPPER, 0, NULL },
    { "PASS", WFT_L_PASS, 5, WFT_C1, WFT_V1, FT_SECRET, LINK_PWMAX, 0,
      FF_LOCAL, 0, NULL },
    { "NOTES", MI_L_NOTES, 6, WFT_C1, WFT_V1, FT_MEMO, 5, 0, FF_LOCAL, 0,
      NULL }
};

static const form_def all_def = {
    WFT_TITLE, NULL, 0, 0, 0, 0,
    (u8)(sizeof all_fields / sizeof all_fields[0]), all_fields
};

/* PASS and NOTES, the two fields of all_fields from WFT_IPASS. */
static const form_def sec_def = {
    WFT_TITLE, NULL, 0, 0, 0, 0, 2, all_fields + WFT_IPASS
};

static const form_field res_fields[] = {
    { "FILE", MQ_L_FILE, 0, WFT_C1, WFT_V1, FT_CHAR, 8, 0, FF_LOCAL, 0,
      NULL },
    { "F4", WFT_L_F4, 1, WFT_C1, WFT_V1, FT_CHAR, 8, 0, FF_LOCAL, 0, NULL },
    { "HOLDER", WFT_L_HOLDER, 2, WFT_C1, WFT_V1, FT_CHAR, 8, 0, FF_LOCAL, 0,
      NULL },
    { "RECORD", MZ_L_REC, 3, WFT_C1, WFT_V1, FT_CHAR, 8, 0, FF_LOCAL, 0,
      NULL },
    { "NEWPW", WFT_L_PASS, 4, WFT_C1, WFT_V1, FT_CHAR, 8, 0, FF_LOCAL, 0,
      NULL },
    { "NOTES", MI_L_NOTES, 5, WFT_C1, WFT_V1, FT_MEMO, 5, 0, FF_LOCAL, 0,
      NULL }
};

static const form_def res_def = {
    WFT_TITLE, k_item, 0, 0, 0, 0,
    (u8)(sizeof res_fields / sizeof res_fields[0]), res_fields
};

static const form_field wide_fields[] = {
    { "TEXT", WFT_L_TEXT, 0, WFT_C1, WFT_V1, FT_CHAR, WFT_WIDEA, 0,
      FF_LOCAL, 0, NULL },
    { "SERIAL", MN_BYID, 0, WFT_C2, WFT_V2, FT_CHAR, WFT_WIDEB, 0,
      FF_LOCAL, 0, NULL }
};

static const form_def wide_def = {
    WFT_TITLE, NULL, 0, 0, 0, 0,
    (u8)(sizeof wide_fields / sizeof wide_fields[0]), wide_fields
};

static const form_field key_fields[] = {
    { "CODE", WFT_L_LOC, 0, WFT_C1, WFT_V1, FT_CHAR, LINK_LOCLEN, 0,
      FF_KEY | FF_UPPER, 0, NULL },
    { "NAME", WFT_L_TEXT, 1, WFT_C1, WFT_V1, FT_CHAR, 20, 0, 0, 0, NULL },
    { "NOTES", MI_L_NOTES, 2, WFT_C1, WFT_V1, FT_MEMO, 5, 0, 0, 0, NULL }
};

/* key_def's NAME: CP437's vertical bar, B3h, then a word. */
static const char k_bar[] = "\xB3 bar";

static const form_def key_def = {
    WFT_TITLE, NULL, 0, 0, 0, 0,
    (u8)(sizeof key_fields / sizeof key_fields[0]), key_fields
};

/* fix_def's choice, its one word, which is its value too. */
static const char k_wide[] = "WWWWWWWWWWWWWWWW";

static const form_field fix_fields[] = {
    { "WIDE", WFT_L_STATUS, 0, WFT_C1, WFT_V1, FT_CHOICE, WFT_WIDEW, 0,
      FF_LOCAL, 0, k_wide },
    { "QTY", MI_L_QTY, 1, WFT_C1, WFT_V1, FT_NUM, 5, 0,
      FF_LOCAL | FF_NONNEG, 0, NULL },
    { "PRICE", MI_L_PRICE, 2, WFT_C1, WFT_V1, FT_NUM, LINK_PRICELEN,
      LINK_PRICEDEC, FF_LOCAL, 0, NULL },
    { "DATE", MI_L_ACQ, 3, WFT_C1, WFT_V1, FT_DATE, 10, 0, FF_LOCAL, 0,
      NULL },
    { "ID", MI_L_ID, 4, WFT_C1, WFT_V1, FT_NUM, LINK_IDLEN, 0,
      FF_LOCAL | FF_RO | FF_ID, 0, NULL }
};

/* fix_fields' values, in their order. */
static const char *const fix_vals[] = {
    k_wide, "99999", "999999.99", "19881228", "88888888"
};

static const form_def fix_def = {
    WFT_TITLE, NULL, 0, 0, 0, 0,
    (u8)(sizeof fix_fields / sizeof fix_fields[0]), fix_fields
};

static HINSTANCE g_inst;
static HWND g_frame;
static HWND g_focus;            /* the focus when busy began            */
static link *g_l;
static form *g_all;             /* the form of every type               */
static u16 g_busy;              /* busy notes so far                    */
static HWND g_checked;          /* the dialog fits has checked, or NULL */
static u8 g_big;                /* a dialog did not fit the floor       */
static u8 g_off;                /* a popup was placed elsewhere         */
static u8 g_clip;               /* a label did not hold its text        */
static u8 g_vclip;              /* a value did not (vclips)             */
static u8 g_look;               /* a dialog's look failed (looks)       */
static u8 g_rows;               /* a pick list's height failed (rows)   */
static u8 g_notfound;           /* CTRL-F1: the refusal RQ_ENOTFOUND    */
static u8 g_rdfail;             /* CTRL-F3: the code lists' reads fail  */
static int g_fixw[FORM_MAXFIELDS];      /* fix_def's controls' widths in
                                   FORM_VIEW, 0 before (vclips)         */
static form_def g_loandef;      /* the loan form (CTRL-F6)              */
static form_field g_loanfields[SCR_NF_COUNT];
static const form_def *g_def;   /* the table of the form shown last     */
static u8 g_click;              /* SHIFT-DEL's click still to post    */
static char g_keyfail;          /* the access letter that failed, '-'
                                   for a label without one though one
                                   is free, or 0                        */
static u8 g_parted;             /* a PART memo's save has failed        */
static u8 g_secsaves;           /* sec_save's calls so far              */
static u8 g_keysaves;           /* key_save's calls so far              */
static u8 g_nest;               /* CTRL-F7's nested wf_run: 0 still to
                                   come, 1 refused, 2 not refused       */
static u8 g_esc;                /* SHIFT-INS: an Esc for the tags' load */
static u8 g_bye;                /* SHIFT-F9: a BYE as the first busy
                                   bracket over the frame closes        */
static int g_lines;             /* lines the memo saved last showed, 0
                                   before a save                        */
/* The item form with SERIALNO FF_NEWONLY too (SHIFT-F2). */
static form_field g_newonly[SCR_IF_COUNT];
static form_def g_newdef;
static char g_id[LINK_IDLEN + 1] = "1";
static char g_what[WFT_WHATSZ] = "WFORMT";
static char g_memo[WF_MEMOSZ] = "First line\r\nCaf\x82 au lait";

/* ---------------------------------------------------------------------
 * The owner.
 * --------------------------------------------------------------------- */

/* Show g_what and the count of busy notes in the frame's title. */
static void show_title(void)
{
    char t[WFT_WHATSZ + sizeof " #" + TXT_U32LEN +
           sizeof " BIG OFF CLIP VCLIP LOOK ROWS" + sizeof " KEYS c" +
           sizeof " LINES " + TXT_U32LEN];
    int n;

    n = wsprintf(t, "%s #%u%s%s%s%s%s%s", (LPCSTR)g_what, g_busy,
                 (LPCSTR)(g_big ? " BIG" : ""),
                 (LPCSTR)(g_off ? " OFF" : ""),
                 (LPCSTR)(g_clip ? " CLIP" : ""),
                 (LPCSTR)(g_vclip ? " VCLIP" : ""),
                 (LPCSTR)(g_look ? " LOOK" : ""),
                 (LPCSTR)(g_rows ? " ROWS" : ""));
    if (g_keyfail != 0)
        n += wsprintf(t + n, " KEYS %c", g_keyfail);
    if (g_lines > 0)
        wsprintf(t + n, " LINES %d", g_lines);
    SetWindowText(g_frame, t);
}

/* Say what, then the number n and the text s. */
static void title(const char *what, int n, const char *s)
{
    wsprintf(g_what, "%s %d %s", (LPCSTR)what, n, (LPCSTR)s);
    show_title();
}

/* 1 when the dialog dlg fits the display floor: on a screen at least
   WFT_FLOORX x WFT_FLOORY its window lies on the screen, and on any
   screen its window's size in dialog units comes to at most that many
   pixels at the EGA system font's base units. */
static int fits(HWND dlg)
{
    RECT w;
    RECT u;
    int sx;
    int sy;

    GetWindowRect(dlg, &w);
    SetRect(&u, 0, 0, 4, 8);
    MapDialogRect(dlg, &u);
    sx = GetSystemMetrics(SM_CXSCREEN);
    sy = GetSystemMetrics(SM_CYSCREEN);
    if (sx >= WFT_FLOORX && sy >= WFT_FLOORY &&
        (w.left < 0 || w.top < 0 || w.right > sx || w.bottom > sy))
        return 0;
    return (i32)(w.right - w.left) * WFT_EGAX <= (i32)WFT_FLOORX * u.right &&
           (i32)(w.bottom - w.top) * WFT_EGAY <= (i32)WFT_FLOORY * u.bottom;
}

/* The lines the memo control of dlg, its multi-line edit control,
   shows, or 0 when it has none. */
static int memo_lines(HWND dlg)
{
    char cls[sizeof k_edit + 1];
    HWND c;

    for (c = GetWindow(dlg, GW_CHILD); c != NULL;
         c = GetWindow(c, GW_HWNDNEXT))
        if (GetClassName(c, cls, (int)sizeof cls) > 0 &&
            txt_ieq(cls, k_edit) &&
            (GetWindowLong(c, GWL_STYLE) & ES_MULTILINE))
            return (int)SendMessage(c, EM_GETLINECOUNT, 0, 0L);
    return 0;
}

/* Check that dlg fits, the first time it is idle. */
static void check(HWND dlg)
{
    if (dlg == g_checked)
        return;
    g_checked = dlg;
    if (!fits(dlg))
        g_big = 1;
}

/* The next access letter of a control's text from *t on: the character
   after the first '&' that does not double another, upper case, *t then
   past it; or 0 for none, *t then at the text's end. */
static int letter(const char **t)
{
    const char *p;

    for (p = *t; *p != '\0'; p++)
        if (*p == '&' && *++p != '&')
            break;
    *t = *p != '\0' ? p + 1 : p;
    return TXT_UPPER(*p);
}

/* 1 when the control of class cls and style st is that of a field that
   can be changed, shown and enabled: an edit control that is not
   read-only, a drop-down list or a check box. */
static int field(const char *cls, LONG st)
{
    return (st & WS_VISIBLE) && !(st & WS_DISABLED) &&
           ((txt_ieq(cls, k_edit) && !(st & WE_READONLY)) ||
            txt_ieq(cls, k_combo) ||
            (txt_ieq(cls, k_button) &&
             (st & WFT_BSTYPE) == BS_AUTOCHECKBOX));
}

/* 1 when t holds a digit or ASCII letter that seen does not. */
static int has_free(const char *t, const u8 *seen)
{
    for (; *t != '\0'; t++)
        if ((TXT_ISDIGIT(*t) || TXT_ISALPHA(*t)) && !seen[TXT_UPPER(*t)])
            return 1;
    return 0;
}

/* 1 when dlg's caption is that of FORM_EDIT on a form that OK saves: it
   ends as WF_S_EDITCAP does after its "%s". */
static int saves(HWND dlg)
{
    char f[MSG_MAXLEN + 1];
    char t[MSG_MAXLEN + 1];
    const char *s;
    int n;
    int m;

    if (LoadString(g_inst, WF_S_EDITCAP, f, (int)sizeof f) == 0 ||
        (s = strstr(f, "%s")) == NULL)
        return 0;
    s += 2;
    n = (int)strlen(s);
    m = GetWindowText(dlg, t, (int)sizeof t);
    return m >= n && strcmp(t + m - n, s) == 0;
}

/* Check the access letters of dlg when it is a form dialog, one with a
   field's control (WF_ID0): every letter of its tab strips (WTAB, each
   '&' of its tabs' text), labels and buttons (Static and Button windows,
   a Static with SS_NOPREFIX left out, the first '&' of each) shown
   once, hidden ones too, so that no label takes a letter of a tab, of
   the Edit button, which FORM_EDIT on a form that OK saves reserves
   without showing (saves), or of the Help button.  The label of each
   field that can be changed (field), the Static before its control,
   shows a letter unless every digit and ASCII letter of its text is
   another control's or the Edit button's.  The first failure goes into
   g_keyfail and the title. */
static void letters(HWND dlg)
{
    char t[MSG_MAXLEN + 1];
    char cls[sizeof k_combo + 1];
    u8 seen[WFT_CHARS];
    const char *p;
    LONG st;
    HWND c;
    HWND lab;
    int strip;
    int k;

    if (g_keyfail != 0 || GetDlgItem(dlg, WF_ID0) == NULL)
        return;
    memset(seen, 0, sizeof seen);
    if (saves(dlg) && LoadString(g_inst, WF_S_EDIT, t, (int)sizeof t) > 0) {
        p = t;
        seen[(u8)letter(&p)] = 1;
    }
    for (c = GetWindow(dlg, GW_CHILD); c != NULL && g_keyfail == 0;
         c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetClassName(c, cls, (int)sizeof cls) == 0)
            cls[0] = '\0';
        st = GetWindowLong(c, GWL_STYLE);
        strip = txt_ieq(cls, k_wtab);
        t[0] = '\0';
        if (strip || (txt_ieq(cls, k_static) && !(st & SS_NOPREFIX)) ||
            txt_ieq(cls, k_button))
            GetWindowText(c, t, (int)sizeof t);
        p = t;
        do {
            k = letter(&p);
            if (k != 0 && seen[(u8)k]++)
                g_keyfail = (char)k;
        } while (k != 0 && strip);
    }
    lab = NULL;
    for (c = GetWindow(dlg, GW_CHILD); c != NULL && g_keyfail == 0;
         c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetClassName(c, cls, (int)sizeof cls) == 0)
            cls[0] = '\0';
        st = GetWindowLong(c, GWL_STYLE);
        if (lab != NULL && field(cls, st)) {
            GetWindowText(lab, t, (int)sizeof t);
            p = t;
            if (((GetWindowLong(lab, GWL_STYLE) & SS_NOPREFIX) != 0 ||
                 letter(&p) == 0) && has_free(t, seen))
                g_keyfail = '-';
        }
        lab = txt_ieq(cls, k_static) ? c : NULL;
    }
    if (g_keyfail != 0)
        show_title();
}

/* The display context of control c, its font selected, the one that
   font replaced into *old (NULL for none); font_done gives it back. */
static HDC font_dc(HWND c, HFONT *old)
{
    HFONT font;
    HDC dc;

    dc = GetDC(c);
    font = (HFONT)SendMessage(c, WM_GETFONT, 0, 0L);
    *old = font != NULL ? (HFONT)SelectObject(dc, font) : NULL;
    return dc;
}

static void font_done(HWND c, HDC dc, HFONT old)
{
    if (old != NULL)
        SelectObject(dc, old);
    ReleaseDC(c, dc);
}

/* Check that each label of dlg, when it is a form dialog, holds its text:
   a Static of ID WFT_LABELID, shown or hidden, whose text, if any (3.0's
   DT_CALCRECT makes an empty text wide in an empty box), laid out in
   the label's font as a static of Windows 3.0 lays it out (DrawText with
   DT_WORDBREAK in its client width), takes one line, no wider than its
   client area.  The first that does not sets g_clip, which the title
   shows. */
static void clips(HWND dlg)
{
    char t[MSG_MAXLEN + 1];
    char cls[sizeof k_static + 1];
    TEXTMETRIC tm;
    RECT r;
    RECT d;
    HFONT old;
    HDC dc;
    HWND c;

    if (g_clip || GetDlgItem(dlg, WF_ID0) == NULL)
        return;
    for (c = GetWindow(dlg, GW_CHILD); c != NULL && !g_clip;
         c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetClassName(c, cls, (int)sizeof cls) == 0 ||
            !txt_ieq(cls, k_static) ||
            (UINT)GetDlgCtrlID(c) != WFT_LABELID)
            continue;
        if (GetWindowText(c, t, (int)sizeof t) == 0)
            continue;
        dc = font_dc(c, &old);
        GetTextMetrics(dc, &tm);
        GetClientRect(c, &r);
        SetRect(&d, 0, 0, r.right, 0);
        DrawText(dc, t, -1, &d, DT_CALCRECT | DT_WORDBREAK);
        if (d.bottom > tm.tmHeight || d.right > r.right)
            g_clip = 1;
        font_done(c, dc, old);
    }
    if (g_clip)
        show_title();
}

/* Check the values of dlg, when it is a form dialog of g_def: each one
   shown as a static (its control WF_ID0 + i), shown or hidden, stands
   on its label's line, its top the top of the label before it, and
   one of a fixed format (FT_DATE, FT_NUM or FT_CHOICE) holds its text
   on that line, the text's extent in its font no wider than its client
   area; and each fixed-format control of fix_def is as wide in
   FORM_EDIT as its static was in FORM_VIEW (g_fixw).  The first that
   fails sets g_vclip, which the title shows. */
static void vclips(HWND dlg)
{
    char t[MSG_MAXLEN + 1];
    char cls[sizeof k_static + 1];
    RECT r;
    RECT l;
    HFONT old;
    HDC dc;
    HWND c;
    u16 i;
    int fixed;
    int text;
    int n;
    u8 ty;

    if (g_vclip || g_def == NULL || GetDlgItem(dlg, WF_ID0) == NULL)
        return;
    for (i = 0; i < g_def->nfields && !g_vclip; i++) {
        ty = g_def->fields[i].type;
        fixed = ty == FT_DATE || ty == FT_NUM || ty == FT_CHOICE;
        c = GetDlgItem(dlg, WF_ID0 + i);
        if (c == NULL)
            continue;
        text = GetClassName(c, cls, (int)sizeof cls) > 0 &&
               txt_ieq(cls, k_static);
        GetWindowRect(c, &r);
        if (g_def == &fix_def && fixed) {
            if (text)
                g_fixw[i] = r.right - r.left;
            else if (g_fixw[i] != 0 && g_fixw[i] != r.right - r.left)
                g_vclip = 1;
        }
        if (!text)
            continue;
        GetWindowRect(GetWindow(c, GW_HWNDPREV), &l);
        if (l.top != r.top)
            g_vclip = 1;
        n = GetWindowText(c, t, (int)sizeof t);
        if (!fixed || n == 0)
            continue;
        dc = font_dc(c, &old);
        GetClientRect(c, &r);
        if ((int)LOWORD(GetTextExtent(dc, t, n)) > r.right)
            g_vclip = 1;
        font_done(c, dc, old);
    }
    if (g_vclip)
        show_title();
}

/* 1 when control c of class cls and style st is a well, which a grey
   dialog frames sunken (WDLG.H): one with WS_BORDER, an edit control
   (whose border is not WS_BORDER in Windows 3.0), a drop-down list or a
   list window (WLIST.H). */
static int well(const char *cls, LONG st)
{
    return (st & WS_BORDER) || txt_ieq(cls, k_edit) ||
           txt_ieq(cls, k_combo) || txt_ieq(cls, k_wlist);
}

/* Control c's window rectangle in dlg's client area, into *r. */
static void spot(HWND dlg, HWND c, RECT *r)
{
    GetWindowRect(c, r);
    ScreenToClient(dlg, (POINT FAR *)&r->left);
    ScreenToClient(dlg, (POINT FAR *)&r->right);
}

/* 1 when the point x, y of dlg's client area lies on a shown control of
   dlg but c, or on the pixel around one, where its frame may be: that
   control paints or frames it, not c's frame; or when a window not dlg,
   such as a drop-down list open, shows there. */
static int covered(HWND dlg, HWND c, int x, int y)
{
    POINT p;
    RECT r;
    HWND o;

    p.x = x;
    p.y = y;
    for (o = GetWindow(dlg, GW_CHILD); o != NULL;
         o = GetWindow(o, GW_HWNDNEXT)) {
        if (o == c || !(GetWindowLong(o, GWL_STYLE) & WS_VISIBLE))
            continue;
        spot(dlg, o, &r);
        InflateRect(&r, 1, 1);
        if (PtInRect(&r, p))
            return 1;
    }
    ClientToScreen(dlg, &p);
    return WindowFromPoint(p) != dlg;
}

/* 1 when every pixel of control c's client area is face: a disabled
   control whose grayed text or box the face has swallowed. */
static int faint(HWND c, COLORREF face)
{
    RECT r;
    HDC dc;
    int x;
    int y;
    int all;

    dc = GetDC(c);
    GetClientRect(c, &r);
    all = 1;
    for (y = 0; y < r.bottom && all; y++)
        for (x = 0; x < r.right && all; x++)
            all = GetPixel(dc, x, y) == face;
    ReleaseDC(c, dc);
    return all;
}

/* The brush dlg answers to WM_CTLCOLOR of type ty from control c on dc. */
static HBRUSH brush(HWND dlg, HWND c, HDC dc, UINT ty)
{
    return (HBRUSH)(UINT)SendMessage(dlg, WM_CTLCOLOR, (WPARAM)dc,
                                     MAKELPARAM(c, ty));
}

/* Check the grey 3D look of dlg (WDLG.H) when it is a form dialog shown:
   its WM_CTLCOLOR answer LTGRAY_BRUSH for the dialog and another brush
   for an edit control and for a drop-down list asking as a static, so
   that the margin about its text stays white; a bare pixel, (1, 1)
   inside the margin, the face (WFT_FACE as the display shows it);
   around each shown well (well), l, t, r and b its rectangle, (l-1,
   t-1) the shadow (wcol_unlike's COLOR_BTNSHADOW) and ((l+r)/2, b)
   white, each unless something else covers it (covered); around each
   hidden one, (l-1, t-1) the face unless something covers it, so that
   a page turned leaves no frame behind; each
   shown, disabled button, and static with a text, a pixel other than
   the face (faint); and its tab strip's point (right-2, 1), right of the
   last tab and above its line, the face.  The first failure sets
   g_look, which the title shows. */
static void looks(HWND dlg)
{
    char cls[sizeof k_combo + 1];
    HBRUSH grey;
    COLORREF face;
    COLORREF shadow;
    COLORREF white;
    RECT r;
    LONG st;
    HWND c;
    HDC dc;
    int bad;
    int x;

    if (g_look || GetDlgItem(dlg, WF_ID0) == NULL || !IsWindowVisible(dlg))
        return;
    grey = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    dc = GetDC(dlg);
    face = GetNearestColor(dc, WFT_FACE);
    shadow = wcol_unlike(dc, COLOR_BTNSHADOW);
    white = GetNearestColor(dc, WFT_WHITE);
    bad = brush(dlg, dlg, dc, CTLCOLOR_DLG) != grey ||
          brush(dlg, dlg, dc, CTLCOLOR_EDIT) == grey ||
          GetPixel(dc, 1, 1) != face;
    for (c = GetWindow(dlg, GW_CHILD); c != NULL && !bad;
         c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetClassName(c, cls, (int)sizeof cls) == 0)
            cls[0] = '\0';
        st = GetWindowLong(c, GWL_STYLE);
        spot(dlg, c, &r);
        x = (r.left + r.right) / 2;
        if (txt_ieq(cls, k_combo) &&
            brush(dlg, c, dc, CTLCOLOR_STATIC) == grey)
            bad = 1;
        else if (!well(cls, st))
            bad = (st & (WS_VISIBLE | WS_DISABLED)) ==
                  (WS_VISIBLE | WS_DISABLED) &&
                  (txt_ieq(cls, k_button) || (txt_ieq(cls, k_static) &&
                   GetWindowTextLength(c) > 0)) && faint(c, face);
        else if (st & WS_VISIBLE)
            bad = (!covered(dlg, c, r.left - 1, r.top - 1) &&
                   GetPixel(dc, r.left - 1, r.top - 1) != shadow) ||
                  (!covered(dlg, c, x, r.bottom) &&
                   GetPixel(dc, x, r.bottom) != white);
        else
            bad = !covered(dlg, c, r.left - 1, r.top - 1) &&
                  GetPixel(dc, r.left - 1, r.top - 1) != face;
    }
    ReleaseDC(dlg, dc);
    c = GetDlgItem(dlg, WF_STRIPID);
    if (!bad && c != NULL && IsWindowVisible(c)) {
        dc = GetDC(c);
        GetClientRect(c, &r);
        bad = GetPixel(dc, r.right - 2, 1) != GetNearestColor(dc, WFT_FACE);
        ReleaseDC(c, dc);
    }
    if (bad) {
        g_look = 1;
        show_title();
    }
}

/* SHIFT-DEL's click, once, when dlg has a tab strip: the mouse's down
   and up posted to the strip at the middle of tab WFT_CLICKTAB's
   rectangle (WTM_GETRECT). */
static void click(HWND dlg)
{
    HWND strip;
    RECT r;
    LPARAM at;

    strip = GetDlgItem(dlg, WF_STRIPID);
    if (!g_click || strip == NULL)
        return;
    g_click = 0;
    SetRectEmpty(&r);
    SendMessage(strip, WTM_GETRECT, WFT_CLICKTAB, (LPARAM)(RECT FAR *)&r);
    at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    PostMessage(strip, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessage(strip, WM_LBUTTONUP, 0, at);
}

/* Mark the title " ROWS" unless the pick list dialog dlg, if it still
   runs, shows WP_ROWS rows: its list box, the list window's (WL_BOXID)
   for a paged list, as high inside as WP_ROWS of its first row. */
static void rows(HWND dlg)
{
    HWND box;
    RECT r;
    RECT k;

    if (!IsWindow(dlg))
        return;
    box = GetDlgItem(dlg, WF_C_PICK);
    if (GetDlgItem(box, WL_BOXID) != NULL)
        box = GetDlgItem(box, WL_BOXID);
    GetClientRect(box, &k);
    if (SendMessage(box, LB_GETITEMRECT, 0, (LPARAM)(RECT FAR *)&r) ==
        LB_ERR || k.bottom != WP_ROWS * (r.bottom - r.top)) {
        g_rows = 1;
        show_title();
    }
}

/* Move the frame, its size kept, so that its centre is the screen's
   top left corner (vk VK_HOME) or its bottom right one (VK_END). */
static void corner(UINT vk)
{
    RECT r;
    int x;
    int y;

    GetWindowRect(g_frame, &r);
    x = vk == VK_HOME ? 0 : GetSystemMetrics(SM_CXSCREEN);
    y = vk == VK_HOME ? 0 : GetSystemMetrics(SM_CYSCREEN);
    MoveWindow(g_frame, x - (r.right - r.left) / 2, y - (r.bottom - r.top) / 2,
               r.right - r.left, r.bottom - r.top, TRUE);
}

/* Make a captioned popup cx by cy pixels owned by the frame, place it
   by wd_centre, mark the title " OFF" unless td_placed agrees, and
   destroy it. */
static void popup(int cx, int cy)
{
    HWND w;

    w = CreateWindow(k_static, "", WS_POPUP | WS_CAPTION, 0, 0, cx, cy,
                     g_frame, NULL, g_inst, NULL);
    if (w == NULL) {
        title("FAIL POPUP", 0, "");
        return;
    }
    wd_centre(w);
    if (!td_placed(w))
        g_off = 1;
    DestroyWindow(w);
    show_title();
}

/* The disk's reads while CTRL-F3's code lists are fetched: refused. */
static int rd_fail(int op)
{
    return op == CF_OP_READ ? CF_EACCES : 0;
}

/* The busy bracket: the driver held, the dialog kept from input, the
   focus given back; SHIFT-INS's Esc posted to a pick list's dialog, the
   one that holds a WF_C_PICK control, and that dialog's rows measured
   once the bracket closes (WFT_ROWSMSG); SHIFT-F9's BYE sent as the
   first bracket over the frame, the code lists' fetch, closes, and
   CTRL-F3's failing reads held over it. */
static void busy(void *arg, HWND dlg, int on)
{
    rq q;
    rq_res res;

    (void)arg;
    if (on) {
        g_busy++;
        g_focus = GetFocus();
        if (g_esc && GetDlgItem(dlg, WF_C_PICK) != NULL) {
            g_esc = 0;
            PostMessage(dlg, WM_KEYDOWN, VK_ESCAPE, 0L);
        }
        if (g_rdfail && dlg == g_frame)
            cf_fault_rd = rd_fail;
    } else {
        cf_fault_rd = NULL;
        if (dlg == g_frame)
            g_rdfail = 0;
        if (GetDlgItem(dlg, WF_C_PICK) != NULL)
            PostMessage(g_frame, WFT_ROWSMSG, (WPARAM)dlg, 0L);
    }
    td_busy(on);
    SetCursor(LoadCursor(NULL, on ? IDC_WAIT : IDC_ARROW));
    EnableWindow(dlg, !on);
    if (!on && g_focus != NULL)
        SetFocus(g_focus);
    if (!on && g_bye && dlg == g_frame) {
        g_bye = 0;
        rq_begin(&q, "BYE");
        rq_send(g_l, &q, NULL, NULL, &res);
    }
}

/* The memo functions of the form of every type, mio_load and mio_save
   on g_memo in place of the link; the first save of a memo of more
   than one line whose first is k_part fails after that line, as one the
   server's disk stopped (RQ_EFULL), keeping g_memo. */
static int memo_load(mio *m, link *l, const char *table, const char *id,
                     rq_res *res)
{
    (void)l;
    (void)table;
    (void)id;
    (void)res;
    mio_set_text(m, g_memo);
    return RQ_OK;
}

static int memo_save(mio *m, link *l, const char *table, const char *id,
                     u16 *done, u16 *total, rq_res *res)
{
    char lb[MEMO_SLOT];

    (void)l;
    (void)table;
    (void)id;
    *total = m->n;
    lb[0] = '\0';
    if (m->n > 1)
        mio_get(m, 0, lb);
    if (!g_parted && strcmp(lb, k_part) == 0) {
        g_parted = 1;
        *done = 1;
        memset(res, 0, sizeof *res);
        res->code = RQ_EFULL;
        return RQ_EFULL;
    }
    mio_text(m, g_memo, (u16)sizeof g_memo);
    *done = m->n;
    g_lines = memo_lines(g_checked);
    return RQ_OK;
}

/* The item form's: an empty memo. */
static int no_memo(mio *m, link *l, const char *table, const char *id,
                   rq_res *res)
{
    (void)m;
    (void)l;
    (void)table;
    (void)id;
    (void)res;
    return RQ_OK;
}

static const wf_ops item_ops = {
    k_item, "ITEM_ID", MI_TITLE, busy, no_memo, NULL, NULL, NULL, NULL,
    &wrow_item_pages, 0, NULL, &scr_item_loc
};

/* The same without the location field, for SHIFT-F2, a location typed
   going to the server as it is, and for F9, whose dialog then makes no
   request as it opens but its category list's fetch. */
static const wf_ops plain_ops = {
    k_item, "ITEM_ID", MI_TITLE, busy, no_memo, NULL, NULL, NULL, NULL,
    &wrow_item_pages
};

static const wf_ops all_ops = {
    NULL, NULL, 0, busy, memo_load, memo_save
};

/* The password form's save in form_save's place (wf_ops): the first
   call refused on PASS (RQ_EARG, or after CTRL-F1 RQ_ENOTFOUND), the
   others taking every value as saved. */
static int sec_save(form *f, link *l, const char *table, char *id,
                    u16 idsz, rq_res *res, u16 *note)
{
    (void)l;
    (void)table;
    (void)id;
    (void)idsz;
    (void)note;
    memset(res, 0, sizeof *res);
    if (g_secsaves++ == 0) {
        res->code = g_notfound ? RQ_ENOTFOUND : RQ_EARG;
        g_notfound = 0;
        strcpy(res->field, k_pass);
        strcpy(res->text, "REFUSED");
        return res->code;
    }
    form_saved(f);
    return RQ_OK;
}

/* The password form's memo save, which always fails (RQ_EFULL). */
static int sec_memo(mio *m, link *l, const char *table, const char *id,
                    u16 *done, u16 *total, rq_res *res)
{
    (void)l;
    (void)table;
    (void)id;
    *total = m->n;
    *done = 0;
    memset(res, 0, sizeof *res);
    res->code = RQ_EFULL;
    return RQ_EFULL;
}

static const wf_ops sec_ops = {
    k_item, NULL, 0, busy, memo_load, sec_memo, NULL, NULL, sec_save
};

/* The form of a key's memo load, which always fails (RQ_EIO). */
static int key_memo(mio *m, link *l, const char *table, const char *id,
                    rq_res *res)
{
    (void)m;
    (void)l;
    (void)table;
    (void)id;
    memset(res, 0, sizeof *res);
    res->code = RQ_EIO;
    return RQ_EIO;
}

/* Its save in form_save's place: RQ_EDUP without a field, then
   RQ_ENOLOGIN. */
static int key_save(form *f, link *l, const char *table, char *id,
                    u16 idsz, rq_res *res, u16 *note)
{
    (void)f;
    (void)l;
    (void)table;
    (void)id;
    (void)idsz;
    (void)note;
    memset(res, 0, sizeof *res);
    res->code = g_keysaves++ == 0 ? RQ_EDUP : RQ_ENOLOGIN;
    return res->code;
}

/* Its busy bracket: busy's, the first one opened also running the
   form of every type, nested (g_nest). */
static void key_busy(void *arg, HWND dlg, int on)
{
    busy(arg, dlg, on);
    if (on && g_nest == 0)
        g_nest = (u8)(wf_run(g_all, dlg, g_l, WFT_LEVEL, &all_ops, NULL,
                             g_id, LINK_IDLEN + 1) == WF_EFAIL ? 1 : 2);
}

static const wf_ops key_ops = {
    k_item, NULL, 0, key_busy, key_memo, memo_save, NULL, NULL, key_save
};

/* res_def's: a table, so that the Edit button's 4 is reserved, but no
   memo functions; its dialog is only shown and left. */
static const wf_ops res_ops = { k_item, NULL, 0, busy, NULL, NULL };

/* DEL's help of the form dialogs: what says "HELP n topic", n 1 when
   owner is a form dialog, one with a field's control (WF_ID0). */
static void help(HWND owner, const char *topic)
{
    title("HELP", GetDlgItem(owner, WF_ID0) != NULL, topic);
}

/* Show f with ops over the frame, id its record's ID (LINK_IDLEN + 1
   bytes); wf_run's result.  The dialog is checked once its loop is idle
   (check): Windows may give it the handle of one before. */
static int run(form *f, const wf_ops *ops, char *id)
{
    g_checked = NULL;
    return wf_run(f, g_frame, g_l, WFT_LEVEL, ops, NULL, id,
                  LINK_IDLEN + 1);
}

/* wf_open, def kept in g_def for vclips: every form but the form of
   every type (g_all), whose keys set g_def themselves. */
static int open_form(form **f, const form_def *def, u8 mode)
{
    g_def = def;
    return wf_open(f, def, mode);
}

/* Send q, its response into res; res->code. */
static int request(rq *q, rq_res *res)
{
    return rq_send(g_l, q, NULL, NULL, res);
}

/* A form of the item id in mode, read with ITEM GET, its dates loaded
   empty; NULL, with the failure in the title, when it could not be. */
static form *item_form(const char *id, u8 mode)
{
    form *f;
    rq q;
    rq_res res;

    if (open_form(&f, &scr_item_def, mode) != 0) {
        title("FAIL FORM", 0, k_item);
        return NULL;
    }
    rq_begin(&q, "ITEM GET");
    rq_word(&q, id);
    if (rq_send(g_l, &q, form_kline, f, &res) != RQ_OK) {
        title("FAIL GET", res.code, id);
        form_close(f);
        return NULL;
    }
    form_load(f, k_created, "");
    form_load(f, k_updated, "");
    return f;
}

/* Run f, an item form of the record id (LINK_IDLEN + 1 bytes), with ops
   and what for the title; after WF_OK, id is g_id and the record is read
   again and shown in FORM_VIEW. */
static void item_run(form *f, const wf_ops *ops, const char *what, char *id)
{
    int r;

    r = run(f, ops, id);
    form_close(f);
    title(what, r, id);
    if (r != WF_OK)
        return;
    strcpy(g_id, id);
    f = item_form(id, FORM_VIEW);
    if (f != NULL) {
        run(f, ops, id);
        form_close(f);
    }
}

/* Item ID g_id in mode, or the item that F8 or F9 spoil first. */
static void item_key(UINT vk)
{
    char id[LINK_IDLEN + 1];
    form *f;
    rq q;
    rq_res res;
    int r;

    strcpy(id, vk == VK_F8 ? WFT_GONEID : g_id);
    f = item_form(id, (u8)(vk == VK_F4 ? FORM_VIEW : FORM_EDIT));
    if (f == NULL)
        return;
    if (vk == VK_F8) {
        rq_begin(&q, "ITEM DEL");
        rq_word(&q, id);
        request(&q, &res);
    } else if (vk == VK_F9 && GetKeyState(VK_SHIFT) < 0) {
        g_bye = 1;
    } else if (vk == VK_F9) {
        rq_begin(&q, "BYE");
        request(&q, &res);
    }
    if (vk == VK_F3) {
        item_run(f, &item_ops, "EDIT", id);
        return;
    }
    r = run(f, vk == VK_F9 ? &plain_ops : &item_ops, id);
    form_close(f);
    title(vk == VK_F4 ? "VIEW" : vk == VK_F8 ? "GONE" : "ENDED", r, id);
}

/* Add n items of category SYS, item k tagged by tags(k, t), t
   LINK_DESCRLEN + 1 bytes; the items added, up to the first refusal. */
static int add_items(int n, void (*tags)(int k, char *t))
{
    char t[LINK_DESCRLEN + 1];
    rq q;
    rq_res res;
    int k;

    for (k = 0; k < n; k++) {
        tags(k, t);
        rq_begin(&q, "ITEM ADD");
        rq_pair(&q, "CATEGORY", "SYS");
        rq_pair(&q, "TAGS", t);
        if (request(&q, &res) != RQ_OK)
            break;
    }
    return k;
}

/* INS's tags: k_next on the last item but one, k_last on the last. */
static void fill_tags(int k, char *t)
{
    strcpy(t, k == WFT_FILL - 2 ? k_next : k == WFT_FILL - 1 ? k_last : "");
}

/* CTRL-INS's: tNN, NN from WFT_PERITEM * k up, joined by commas. */
static void cap_tags(int k, char *t)
{
    int j;
    int n;

    t[0] = '\0';
    for (j = 0; j < WFT_PERITEM; j++) {
        n = WFT_PERITEM * k + j;
        wsprintf(t + strlen(t), "%st%d%d", (LPCSTR)(j > 0 ? "," : ""),
                 n / 10, n % 10);
    }
}

/* The password form in its dialog in mode (FORM_), then what its PASS
   slot holds (this file's header, F1). */
static void secret(u8 mode)
{
    const char *v;
    form *f;
    u16 i;
    int held;
    int r;

    if (open_form(&f, &sec_def, mode) != 0) {
        title("FAIL FORM", 0, k_pass);
        return;
    }
    r = run(f, &sec_ops, g_id);
    v = form_get(f, k_pass);
    held = form_dirty(f);
    for (i = 0; i <= LINK_PWMAX; i++)
        if (v[i] != '\0' && strchr(k_secret, v[i]) != NULL)
            held = 1;
    form_close(f);
    title("SECRET", r, held ? "HELD" : "WIPED");
}

/* The form of fixed formats in mode, its values fix_vals. */
static void fixed(u8 mode)
{
    form *f;
    u16 i;

    if (open_form(&f, &fix_def, mode) != 0) {
        title("FAIL FORM", 0, k_wide);
        return;
    }
    for (i = 0; i < fix_def.nfields; i++)
        form_load(f, fix_fields[i].name, fix_vals[i]);
    title("FIX", run(f, &all_ops, g_id), g_id);
    form_close(f);
}

/* The form of a key (key_def) in FORM_EDIT, its saves refused anew. */
static void keyed(void)
{
    form *f;
    int r;

    if (open_form(&f, &key_def, FORM_EDIT) != 0) {
        title("FAIL FORM", 0, "KEY");
        return;
    }
    form_load(f, key_fields[0].name, "K1");
    form_load(f, key_fields[1].name, k_bar);
    g_keysaves = 0;
    g_nest = 0;
    r = run(f, &key_ops, g_id);
    title(g_nest == 1 ? "KEY" : "NESTED", r, g_id);
    form_close(f);
}

/* The form of a wide line (wide_def) in FORM_EDIT. */
static void wide(void)
{
    form *f;

    if (open_form(&f, &wide_def, FORM_EDIT) != 0) {
        title("FAIL FORM", 0, "WIDE");
        return;
    }
    title("WIDE", run(f, &all_ops, g_id), g_id);
    form_close(f);
}

/* A returned loan in the loan form (scr_loan_form's for SCR_K_LOAN) in
   FORM_VIEW, its values in scr_loan_fields' order, a field the loan
   form lacks left out: its out, due and return dates on one row of the
   table. */
static void loan(void)
{
    static const char *const vals[SCR_NF_COUNT] = {
        "1", "1", "1", "IBM 5150", "Carol King", "", "", "",
        "20200101", "20200201", "20261008", ""
    };
    form *f;
    u16 k;

    scr_loan_form(SCR_K_LOAN, &g_loandef, g_loanfields);
    if (open_form(&f, &g_loandef, FORM_VIEW) != 0) {
        title("FAIL FORM", 0, "LOAN");
        return;
    }
    for (k = 0; k < SCR_NF_COUNT; k++)
        if (vals[k][0] != '\0')
            form_load(f, scr_loan_fields[k].name, vals[k]);
    title("LOAN", run(f, &all_ops, g_id), g_id);
    form_close(f);
}

/* The frame's keys, as this file's header gives them. */
static void key(UINT vk)
{
    char id[LINK_IDLEN + 1];
    form *f;

    switch (vk) {
    case VK_F2:
        if (open_form(&f, GetKeyState(VK_SHIFT) < 0 ? &g_newdef :
                          &scr_item_def, FORM_NEW) != 0) {
            title("FAIL FORM", 0, k_item);
            return;
        }
        scr_item_preset(f);
        id[0] = '\0';
        item_run(f, GetKeyState(VK_SHIFT) < 0 ? &plain_ops : &item_ops,
                 "ADD", id);
        break;
    case VK_F3:
    case VK_F4:
    case VK_F8:
    case VK_F9:
        g_rdfail = vk == VK_F3 && GetKeyState(VK_CONTROL) < 0;
        item_key(vk);
        break;
    case VK_F5:
    case VK_F6:
    case VK_F7:
        if (vk != VK_F7 && GetKeyState(VK_SHIFT) < 0) {
            fixed((u8)(vk == VK_F5 ? FORM_EDIT : FORM_VIEW));
            break;
        }
        if (vk == VK_F6 && GetKeyState(VK_CONTROL) < 0) {
            loan();
            break;
        }
        if (vk == VK_F5 && GetKeyState(VK_CONTROL) < 0) {
            wide();
            break;
        }
        if (vk == VK_F7 && GetKeyState(VK_CONTROL) < 0) {
            keyed();
            break;
        }
        g_def = &all_def;
        form_mode_set(g_all, (u8)(vk == VK_F5 ? FORM_EDIT :
                                  vk == VK_F6 ? FORM_VIEW : FORM_NEW));
        title("ALL", run(g_all, &all_ops, g_id), g_id);
        break;
    case VK_F1:
        if (GetKeyState(VK_CONTROL) < 0) {
            g_secsaves = 0;
            g_notfound = 1;
        }
        secret((u8)(GetKeyState(VK_SHIFT) < 0 ? FORM_VIEW : FORM_EDIT));
        break;
    case VK_HOME:
    case VK_END:
        corner(vk);
        break;
    case VK_PRIOR:
        popup(GetSystemMetrics(SM_CXSCREEN) + WFT_OVER,
              GetSystemMetrics(SM_CYSCREEN) + WFT_OVER);
        break;
    case VK_NEXT:
        ShowWindow(g_frame, SW_MINIMIZE);
        popup(WFT_SMALL, WFT_SMALL);
        ShowWindow(g_frame, SW_RESTORE);
        break;
    case VK_INSERT:
        if (GetKeyState(VK_SHIFT) < 0) {
            g_esc = 1;
            title("ESC", 0, "");
        } else if (GetKeyState(VK_CONTROL) < 0) {
            title("CAP", add_items(WFT_NCAP, cap_tags), "");
        } else {
            title("FILL", add_items(WFT_FILL, fill_tags), "");
        }
        break;
    case VK_DELETE:
        if (GetKeyState(VK_SHIFT) < 0) {
            g_click = 1;
            title("CLICK", 0, "");
        } else if (GetKeyState(VK_CONTROL) < 0) {
            if (open_form(&f, &res_def, FORM_EDIT) != 0) {
                title("FAIL FORM", 0, k_item);
                return;
            }
            title("RES", run(f, &res_ops, g_id), g_id);
            form_close(f);
        } else {
            wf_help(help);
            title("HELP", 0, "SET");
        }
        break;
    case VK_ESCAPE:
        PostMessage(g_frame, WM_CLOSE, 0, 0L);
        break;
    default:
        break;
    }
}

LRESULT WCALLBACK wft_frame(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    switch (wm) {
    case WM_KEYDOWN:
        key(wp);
        return 0;
    case WFT_ROWSMSG:
        rows((HWND)wp);
        return 0;
    case WM_ENTERIDLE:
        if (wp == MSGF_DIALOGBOX) {
            check((HWND)LOWORD(lp));
            letters((HWND)LOWORD(lp));
            clips((HWND)LOWORD(lp));
            vclips((HWND)LOWORD(lp));
            looks((HWND)LOWORD(lp));
            click((HWND)LOWORD(lp));
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(w, wm, wp, lp);
}

/* ---------------------------------------------------------------------
 * Start and end.
 * --------------------------------------------------------------------- */

/* Make the frame; 0 when Windows refuses. */
static int make_frame(int show)
{
    WNDCLASS wc;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wft_frame;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = k_class;
    if (RegisterClass(&wc) == 0)
        return 0;
    g_frame = CreateWindow(k_class, k_class, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL,
                           g_inst, NULL);
    if (g_frame == NULL)
        return 0;
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    return 1;
}

/* Load the messages, open the link on dir, log in and make the form of
   every type; a failure said in the title. */
static void start(const char *dir)
{
    char why[LINK_WHYSZ];
    rq q;
    rq_res res;
    int rc;

    rc = msg_load("", MSG_FILE);
    if (rc != 0) {
        title("FAIL MSG", rc, dir);
        return;
    }
    rc = link_open_local(&g_l, dir, NULL, why, (u16)sizeof why);
    if (rc != 0) {
        title("FAIL OPEN", rc, dir);
        return;
    }
    rq_begin(&q, "HELLO");
    rq_word(&q, k_class);
    if (request(&q, &res) == RQ_OK) {
        rq_begin(&q, "LOGIN");
        rq_word(&q, "ADMIN");
        rq_word(&q, "Retro1986");
        request(&q, &res);
    }
    if (res.code != RQ_OK) {
        title("FAIL LOGIN", res.code, dir);
        return;
    }
    if (wf_open(&g_all, &all_def, FORM_EDIT) != 0) {
        title("FAIL ALL", 0, dir);
        return;
    }
    form_load(g_all, "ID", "R-1");
    form_load(g_all, "COND", "XX");
    memcpy(g_newonly, scr_item_fields, sizeof g_newonly);
    g_newonly[SCR_IF_SERIAL].flags |= FF_NEWONLY;
    memcpy(&g_newdef, &scr_item_def, sizeof g_newdef);
    g_newdef.fields = g_newonly;
    SetFocus(g_frame);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char words[WFT_CMDMAX];
    const char *keys;
    const char *snap;
    const char *dir;
    char *p;
    MSG m;

    (void)prev;
    for (p = words; *cmd != '\0' && p < words + sizeof words - 1; )
        *p++ = *cmd++;
    *p = '\0';
    keys = NULL;
    snap = NULL;
    dir = "";
    for (p = words; *p != '\0'; ) {
        while (*p == ' ')
            *p++ = '\0';
        if (txt_ieqn(p, k_keys, sizeof k_keys - 1))
            keys = p + sizeof k_keys - 1;
        else if (txt_ieqn(p, k_snap, sizeof k_snap - 1))
            snap = p + sizeof k_snap - 1;
        else if (*p != '\0')
            dir = p;
        while (*p != '\0' && *p != ' ')
            p++;
    }
    td_init(inst, keys, snap);
    td_place();
    g_inst = inst;
    dos_inst = (u16)inst;
    wrow_intl_read(inst);
    m.wParam = 0;
    if (make_frame(show)) {
        start(dir);
        while (GetMessage(&m, NULL, 0, 0)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    form_close(g_all);
    link_close(g_l);
    msg_free();
    td_done();
    return (int)m.wParam;
}
