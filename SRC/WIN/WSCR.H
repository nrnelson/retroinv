/*
 * WSCR.H - the screens of INVWIN's frame: what a screen gives the frame,
 * and the frame's part that a screen names (INVWIN.C).
 *
 * The frame shows one screen at a time, a list window (WLIST.H) over the
 * screen's source whose rows are records, drawn in the screen's columns
 * (WCOLS.H), each shown in the form dialog (WFORM.H) over the screen's
 * form, and beside it, on a screen that has one, its pane (ws_pane), the
 * list of another screen, or a preview of the record, that follows the
 * row selected; the frame opens the list, titles itself for it and
 * carries out its keys as INVWIN.H
 * says.  A screen is a ws_scr, constant data of its module (WLOC.C,
 * WSVC.C, WLOAN.C, WUSER.C, WADMIN.C) or of the frame's (the item screen,
 * the search's results, a location's contents, an item's parts, the
 * categories, a category's items);
 * every record command of a screen but its own (below) is sent with the
 * record's key, the list row's, as its one word.
 *
 * A scoped screen (WS_SCOPED) lists the records of one record of the
 * screen it is opened from, the scope, an item (the parts, the service
 * records, the loans and holds of an item or what is used with it): its source
 * gets the scope's key as its arg, "" on any other screen, and a source
 * without args of its own takes the key as its command's first word; its title
 * is the message title with the scope's name (below) as ^1, a new record takes
 * the key in its field scope, and ESC goes back to the screen it was opened
 * from, in the order that screen's list had, and to the record's form when it
 * was opened from there.  A screen that names a scope field and an ask form
 * (the loans and holds, an item's host's) asks for the scope's key of a
 * new record first, in the form dialog over ask, which checks that the
 * item exists, a scoped one giving its own scope's key to its kinds'
 * host field; a tree (WS_TREE) gives its scope field the key of the record
 * selected when INS was pressed on it, as loaded, else nothing, before its
 * preset runs.
 *
 * A screen's own record commands (ws_act: the end of a loan or hold, the
 * item commands of WASSY.H) each ask for their values in the form
 * dialog over their form, captioned with the form's title and as ^1 the
 * record's key, or the scope's for a command on the scope (WA_SCOPE),
 * which needs no row selected, a location field among them as WFORM.H
 * says (ws_act's loc).  OK checks them and sends the request from the
 * dialog (wf_ops' send): the command's own (send), or the command with
 * the key as its word and each value given but a local field's
 * (FF_LOCAL) as a pair.  A
 * refusal shows in the dialog as a failed save does, the focus on the
 * field it names, and the dialog stays.  Cancel sends nothing.  A
 * command made shows its note on the status bar, as a record saved
 * does.
 *
 * A record's name is its key, then the value of each of its form's
 * names (wf_ops' names) that is not empty, after a blank
 * ("1 IBM 5150"), or those values alone when its form leaves the key
 * out (wf_ops' flags WF_O_NOKEY: "Shelf A1"), read with the screen's get
 * command (wf_name): the question before a delete names the record so,
 * as its form's caption does (WFORM.H), and a scoped screen's title its
 * scope, an item.
 *
 * The frame's part beside the screens' lists: a request sent as the
 * frame sends its own, its failure shown the same way, and a message box
 * with the frame's caption, for the commands of the Admin menu that are
 * not lists (WADMIN.H).
 */
#ifndef WSCR_H
#define WSCR_H

#include "COMPAT.H"
#include "CWIN.H"
#include "RQ.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "WFORM.H"
#include "WCOLS.H"

/* The record keys (INVWIN.H) of the commands that the Record menu alone
   gives, above every virtual key with SHIFT and CTRL: a screen's own
   (ws_act's key) and the frame's.  WS_K_PARTS to WS_K_USED open an
   item's parts, service records, loans and holds and what is used with
   it, in that order. */
#define WS_K_MOVE       0x400   /* move the record                      */
#define WS_K_INSTALL    0x401   /* install the item, or a part          */
#define WS_K_ITEM       0x402   /* show the row's item                  */
#define WS_K_REMOVE     0x403   /* take the item out of its assembly    */
#define WS_K_RETURN     0x404   /* end the loan or hold                 */
#define WS_K_SPLIT      0x405   /* split units off the item             */
#define WS_K_PARTS      0x406
#define WS_K_SVC        0x407
#define WS_K_LOANS      0x408
#define WS_K_USED       0x409
#define WS_K_ORDER      0x40A   /* the list's next order                */
#define WS_K_TOP        0x40B   /* the items not installed, or all      */
#define WS_K_BELOW      0x40C   /* a location's own items, or all below */

/* A screen's own record command. */
typedef struct {
    UINT key;                   /* its record key: a virtual key pressed
                                   without SHIFT and CTRL, or a WS_K_
                                   value; 0 ends a list of them         */
    const char *cmd;            /* the command ("LOAN RETURN")          */
    const form_def *def;        /* the values asked for, each field's
                                   name the pair's                      */
    /* The request of cmd for record key with the values of f, sent
       over l: RQ_OK or the failure's code, the failure in res; NULL for
       the key as its word and the values as pairs. */
    int  (*send)(link *l, const char *cmd, const char *key, form *f,
                 rq_res *res);
    u8   flags;                 /* WA_                                  */
    /* The note of the command made (MSG.H), or 0 for none: ^1 the
       record it changes and ^2 the other, the row's key and the first
       value asked, the path of a location asked (loc); or on the scope
       (WA_SCOPE) the first value asked and the scope's key; or, when its
       +OK gives an ID (a record made), that ID and the row's key. */
    u16  note;
    u16  label;                 /* its entry of the Record menu, a
                                   string of INVWIN.RC, or 0: none      */
    const scr_locf *loc;        /* def's location field (WFORM.H), which
                                   starts on the item's location (ITEM
                                   GET), or NULL, or left out, for none */
} ws_act;

/* ws_act's flags. */
#define WA_SCOPE        0x01    /* on the scope's key, not a row's      */
#define WA_KEEP         0x02    /* the location where the item is, which
                                   its location field starts on, chosen
                                   again sends nothing                  */
#define WA_KIND         0x04    /* def's title is the first of a
                                   message per kind, the selected row's
                                   chosen (ws_kinds), and the note,
                                   whose ^2 is the row's caption, the
                                   kind's of ws_kinds' ends, note
                                   unused                               */
#define WA_MULTI        0x08    /* on each row chosen of a list of
                                   several rows, asked once (INVWIN.H,
                                   Several rows)                        */

/* The kinds of a screen whose records each have a form of their kind
   (the loans, reservations and uses of SCRNFORM.H): kind gives the
   kind of the record of a row, from its text, form builds a kind's
   form into def and fields (FORM_MAXFIELDS of them), a new record's
   first values come from preset, given the kind, and item gives the
   item of a row, its ID into id (LINK_IDLEN + 1 bytes) and 1, or 0;
   item NULL for rows that do not name it.  ends holds the note of a
   record ended (WA_KIND), by kind.  On a scoped screen host names the
   field of a new record that takes the scope's key, as a change, the
   record's item then asked for (ws_scr's ask), and hosts has bit k set
   for each kind k such a record may be, which its keys add; host NULL
   and hosts 0 elsewhere.  The form's title and the record's caption
   are the first of a message per kind, and the screen's notes take
   that caption as ^2. */
typedef struct {
    u8   (*kind)(const char *text);
    void (*form)(u8 kind, form_def *def, form_field *fields);
    void (*preset)(form *f, u8 kind);
    int  (*item)(const char *text, char *id);
    const u16 *ends;
    const char *host;
    u8   hosts;
} ws_kinds;

typedef struct ws_scr ws_scr;

/* The pane beside a screen's list, the master (ws_scr's pane), which
   follows the row selected in the master, as INVWIN.H says (The split
   view): the list of another screen, the detail, whose scope is the
   pane's, the arg of its source, a new record's scope and the key of its
   WA_SCOPE commands; or, without a detail, a preview panel (WPREV.H) of
   the row's record, the record commands staying the master's. */
typedef struct {
    u8   cfg;                   /* its screen of WINPANE, a CFG_P_ value
                                   of CFG.H                             */
    const ws_scr *detail;       /* the screen of the pane's list, or NULL
                                   for a preview panel                  */
    const wcol_tab *wide;       /* the columns the master is as wide as
                                   while the settings give no width     */
    /* Point the pane at the master's row key, whose text is text, from
       the main loop, with the requests that takes: a list's scope for
       that row into scope (LV_KEYSZ bytes), its requests sent from w
       (ws_send); or the panel pv, NULL for a list, filled with the row's
       record, the frame held busy over its requests (ws_form_busy), a
       record that is gone said in the panel.  1, or 0 with *act the
       action of a failure, shown over w (ws_fail), or RQE_STAY and
       nothing shown when an Esc between two requests stopped a panel's
       load or the row gives a list no scope. */
    int  (*follow)(HWND w, const char *key, const char *text, char *scope,
                   HWND pv, u8 *act);
    u16  count;                 /* a list's count, a string of INVWIN.RC:
                                   %lu its rows, %s what it shows (name),
                                   the singular at the next number; 0,
                                   or left out, for the count of its
                                   screen, as when it shows alone       */
    const char *(*name)(void);  /* what a list shows, of which the count
                                   takes LINK_LOCNAMELEN characters;
                                   left out without a count             */
    u16  loading;               /* a panel's text while it loads, a
                                   string of INVWIN.RC, %s the row's
                                   key; left out for a list             */
} ws_pane;

struct ws_scr {
    const char *topic;          /* the help topic of its list (WHELP.H) */
    const lv_src *src;          /* the list's source, a template that the
                                   frame copies, giving it fmt          */
    lv_fmt_fn fmt;
    const wcol_tab *cols;       /* its rows' columns (WCOLS.H), a table
                                   for each of src's orders, or one
                                   without orders; NULL on a screen of
                                   views (WS_VIEWS), whose view chooses
                                   them (wloan_view)                    */
    u16  title;                 /* the list's title, a message, plus the
                                   index of the order in use            */
    const form_def *def;        /* the record's form, or NULL: ENTER and
                                   CTRL+E do nothing but with kinds, or
                                   on the frame's categories, whose
                                   commands are the frame's (INVWIN.H,
                                   The categories)                      */
    const wf_ops *ops;          /* the dialog's part, its busy
                                   ws_form_busy and its memo functions
                                   mio_load and mio_save, for the NOTES
                                   of def's FT_MEMO field               */
    const char *get;            /* the command whose K lines load a
                                   record into the form (ITEM GET)      */
    const char *del;            /* the command deleting one (ITEM DEL),
                                   or NULL: DEL does nothing            */
    u16  delq;                  /* the question before a delete, ^1 the
                                   record's name                        */
    void (*preset)(form *f);    /* a new record's first values, or NULL */
    void (*loaded)(form *f);    /* after get has loaded f, and with
                                   kinds after a new record's preset,
                                   or NULL                              */
    u8   flags;                 /* WS_                                  */
    const char *scope;          /* the field of a new record that takes
                                   the scope's key, or NULL             */
    const form_def *ask;        /* the form of one field, scope, that a
                                   screen not scoped asks the key in,
                                   or NULL                              */
    const ws_act *act;          /* the screen's own commands, or NULL   */
    const ws_kinds *kinds;      /* its records' kinds, def and preset
                                   then unused, or NULL                 */
    rq_fn kline;                /* the loader of get's lines, or NULL
                                   for form_kline                       */
    const u16 *notes;           /* the notes of a record added, saved,
                                   deleted and moved, by WS_N_, messages
                                   whose ^1 is its key and ^2 its name;
                                   NULL for none                        */
    const char *(*name)(void);  /* the list's title, a text, in place of
                                   title's message, or NULL             */
    /* WS_K_MOVE's move of record key, chosen over w with the link l
       and the user's level: 1 once moved, else 0 with *act the action
       of a failure, shown, RQE_STAY when nothing was chosen; NULL for
       none, when notes has no WS_N_MOVED. */
    int  (*move)(HWND w, link *l, u8 level, const char *key, u8 *act);
    const ws_pane *pane;        /* the pane beside its list, or NULL, or
                                   left out, for none                   */
};

/* ws_scr's flags. */
#define WS_SCOPED       0x01    /* a scoped screen                      */
#define WS_NONEW        0x02    /* INS does nothing (no new record)     */
#define WS_ITEMS        0x04    /* its records are items: WS_K_PARTS to
                                   WS_K_USED open the selected one's
                                   parts, service records, loans and
                                   holds, and what is used with it      */
#define WS_NEWREST      0x08    /* a new record's form is def without
                                   its first field (a user's status)    */
#define WS_HOLDS        0x10    /* its records are locations: ENTER
                                   opens the selected one's contents as
                                   a screen of their own (wloc_enter of
                                   WLOC.H), not its form, and the rows
                                   of its pane are dragged onto them    */
#define WS_TREE         0x20    /* its records are a tree, the list in
                                   tree order, a letter seeking the next
                                   whose row starts with it (wl_letters
                                   of WLIST.H): INS adds a record inside
                                   the selected one, its key in scope,
                                   and after an add, an edit or a move
                                   the list is fetched again from its
                                   start, the record sought (wl_find),
                                   as a change may move rows between
                                   pages                                */
#define WS_VIEWS        0x40    /* CTRL+PGDN shows its next view, and
                                   CTRL+PGUP the one before (wloan_view
                                   of WLOAN.H)                          */
#define WS_FILTER       0x80    /* its list holds some of its records
                                   only, which a change of a record may
                                   add to or take from it: the frame has
                                   its rows counted again (wl_recount
                                   of WLIST.H), where on another list it
                                   counts the records it adds and
                                   deletes itself                       */

/* The indexes of ws_scr's notes. */
#define WS_N_ADDED      0
#define WS_N_SAVED      1
#define WS_N_DELETED    2
#define WS_N_MOVED      3

/* The value column of a dialog asking for one value (ws_scr's ask,
   ws_act's def): after its label's slot, a form label of nine cells (W=
   in the .MSG sources), and a blank. */
#define WS_ASKV         10
/* The value column of such a dialog whose label is a prompt's of INV, a
   slot of 63 cells (W= in the .MSG sources: VID_COLS - 7 -
   TUI_PROMPTMINW), which leaves a control as wide as a date's,
   FORM_DATEW cells, within WF_MAXW. */
#define WS_PROMPTV      63

/* The form dialog's busy bracket (wf_ops): the dialog held as the frame
   is for a request, or the frame itself for dlg NULL. */
void ws_form_busy(void *arg, HWND dlg, int on);

/* The frame's pick list of a host field of a form dialog (wf_ops'
   pick): the active items (STATUS A) holding the text typed, or the
   item of the ID typed, as WFORM.H says. */
int ws_pick_item(void *arg, HWND dlg, link *l, u8 level, const char *typed,
                 int find, char *code, u8 *act);

/* The frame's link, for a request a shared helper sends itself, which
   its caller holds busy (ws_form_busy) as ws_send would. */
link *ws_link(void);

/* The categories the frame holds for the names in its rows while a list
   of items or parts shows; its b NULL otherwise. */
ctab *ws_cats(void);

/* 1 when the active window is one of the program's, the frame, a dialog
   or a message box, rather than another program's that the user went
   to, whose activation a focus set would take. */
int ws_active(void);

/* Send q on the frame's link from w, the frame or a dialog over it (NULL
   for the frame), held as for every request (the hourglass, w disabled,
   the test driver held), its lines to fn with arg (rq_send); RQ_OK, or
   the failure's code with res its response, shown by no one. */
int ws_send(HWND w, rq *q, rq_fn fn, void *arg, rq_res *res);

/* Show the failure res in a message box over w (ws_say), with the icon
   a list's move gives it (WLIST.H); its action (rqe_class, at the
   user's level), which the caller leaves by. */
u8 ws_fail(HWND w, const rq_res *res);

/* Show text, in the Windows set, in a message box over w captioned with
   the program's title, of style st: an MB_ICON value for the OK button
   alone, or the buttons as well.  Returns the button chosen. */
int ws_say(HWND w, UINT st, const char *text);

/* ws_say of text in CP437, at most MSG_MAXLEN characters of it. */
int ws_say_oem(HWND w, UINT st, const char *text);

/* The help panel of the frame's status bar while a long request runs:
   text (CP437), painted at once, as no message may be dispatched
   meanwhile; NULL empties it. */
void ws_info(const char *text);

/* A modeless dialog of a screen's, shown while the frame is held for a
   request, or NULL: the frame's yield gives it its keys as a dialog's
   (IsDialogMessage), so that its Cancel takes Esc. */
extern HWND ws_modeless;

/* The help topics (WHELP.H) of the item lists and of the parts, which
   the asks of other modules name too. */
extern const char ws_k_items[];
extern const char ws_k_parts[];

/* A location's contents (WLOC.H), a screen of the frame's: items, as
   the item screen's, titled with the location, a new one taking it; the
   detail of the locations' pane (WPANE.H). */
extern const ws_scr ws_contents;

/* An item's parts (WASSY.H), a scoped screen of the frame's, the detail
   of the assemblies' pane (WPANE.H). */
extern const ws_scr ws_parts;

/* A category's items, a scoped screen of the frame's, its scope the
   category's code: the detail of the categories' pane (WPANE.H). */
extern const ws_scr ws_catitems;

#endif
