/*
 * INVWIN.C - the RETROINV client for Windows 3.0, INVWIN.EXE: start-up,
 * the frame window, the link, the login, the menu bar and the screens,
 * among them the item list with its item form.
 *
 *   INVWIN [/LOCAL [dir] | /NET name | /COM1 | /COM2 | /TCP:addr[:port] |
 *          /MBOX:dir] [/CFG:file]
 *
 * The switches are INV's, /TCP: and /MBOX:, read by args_parse (ARGS.H)
 * from the words of the command line: /LOCAL runs the engine inside the
 * program on the data directory dir, else DATA= of the settings, else
 * DATA\ under the current directory; /NET names a server, /COM1 or
 * /COM2 a serial port, /TCP: the address of the TCP bridge INVTCP, and
 * its port when it is not LINK_TCPPORT, and /MBOX: the directory of the
 * server's mailbox; /CFG: names the settings file (default CFG_NAME,
 * read with CFG.C as INV reads it).  A usage error shows the usage text
 * and ends the program, as a settings file or a message file that
 * cannot be read does: INV.MSG (MSG_FILE), beside the program or in the
 * current directory, holds the texts of the menus and of failed
 * requests; the help, INVWIN.HLP (WH_HLP) for Windows Help and its text
 * INVWIN.HTX (WH_TEXT), is found the same way when it is asked for
 * (WHELP.H).
 *
 * One instance runs at a time: a second start brings the first one's
 * window to the front and ends.  The LOCAL engine opens its data files
 * denying every other open, which only SHARE enforces: without it a
 * second engine on the same directory would write the same files.
 *
 * The frame, its menus and dialogs are as INVWIN.H gives them.  Once the frame
 * is shown, its status bar naming the connection, it opens the link of the
 * switches, as INV does: LOCAL on the data directory, NET by a call to the
 * server, SERIAL on the port at BAUD= with RTSCTS= of the settings, TCP by a
 * connection to the bridge, which Esc gives up, MBOX on the mailbox with the
 * station of sn_station as its request files' name.  It then sends HELLO with
 * that station and VER, whose text the login dialog and the About box show,
 * and runs the login dialog.  A login builds the menu bar for the user's level
 * from invmenu_bar and INV.MSG, the message loop taking CTRL+F (find_key) and
 * F1 (wh_key) before it translates a key, adds the Record menu, which each
 * screen fills with its record keys (rec_menu), and opens the item screen.  A
 * screen (WSCR.H) is a list window (WLIST.H) over a shared source, the item
 * screen's that of ITEMLS.H, an item's parts' that of SCRAFORM.H, the
 * locations screen's (WLOC.C) that of SCRLFORM.H, the service screens'
 * (WSVC.C) those of SCRMFORM.H, the loan screens' (WLOAN.C) those of
 * SCRNFORM.H, the search results' (WSRCH.C), a location's contents' (WLOC.C)
 * and the assemblies' (WASSY.C) that of ITEMLS.H for their command, the users
 * screen's (WUSER.C) and the audit log's (WADMIN.C) those of SCRZFORM.H, the
 * categories' one of its own, CAT LIST, a category's items' ITEMLS.H's, whose
 * records show in the form dialog (WFORM.H) over a shared form (SCRIFORM.H,
 * SCRLFORM.H, SCRMFORM.H, SCRNFORM.H, SCRZFORM.H), a memo NOTES included,
 * which the dialog loads and saves through mio_load and mio_save (MEMOIO.H).
 * The frame shows one screen at a time, a screen that has a pane (ws_pane of
 * WSCR.H) with it beside its list, as the location tree with the selected
 * location's contents (the split view, its panes iw_panes), and
 * carries out the record commands of each the same way, a screen's own
 * commands too (the item commands of WASSY.C).  Export CSV and Import CSV are
 * WCSV.C's dialogs over the form of SCRCFORM.H, the file of the local link
 * chosen in the file name dialog of WFILE.C.  The Reports entries run the
 * report dialogs of WRPT.C, with the page and label sizes of the settings,
 * printing on Windows' default printer (CPRNW.C).  A record command runs from
 * the list's note of its key, the last thing the list does in its call, so
 * that the command may move the list, or from the key's entry of the Record
 * menu (rec_key).  Log out drops the menu bar, its keys and the list, sends
 * HELLO again, which ends the login on the session (SESS.H), and runs the
 * login dialog again.  The message loop is also where the list is opened again
 * for a new size of the frame and where a list's pane loads once the list's
 * selection has settled (settle), as no move, request, dialog or menu is
 * under way there, so that no request nests, where a key or a mouse click ends
 * a note on the status bar and where the Find box's keys are taken.  Every
 * request, and the opening of the link, which may rebuild damaged indexes,
 * runs with the hourglass, the window that sent it (the frame or a dialog)
 * disabled and the test driver held (td_busy), and the keys and mouse clicks
 * given meanwhile are dropped, but an Esc, which stops a load of several
 * requests at the wait before its next one (wl_wait of WLIST.H).  The LOCAL
 * engine never calls dos_idle, so Windows waits for each request to end; the
 * remote links let the other programs run through dos_yield, which dispatches
 * the messages waiting, keeping such an Esc for that wait (yield), and no
 * window takes input meanwhile, so none reaches the link from there, but for
 * a screen's modeless dialog (ws_modeless of WSCR.H), whose Cancel stops a
 * report between its requests.  A list's key is a record command only while
 * the frame is enabled (idle): one the list had posted itself before a
 * request, a command's rows or a dialog began comes to it inside their loops,
 * and is dropped there, as it was typed for the screen before them.  A link
 * that cannot open, with INV's reason, and a refused HELLO or VER are said in
 * a message box and end the program.
 * At the end the link is closed, LOCAL's data files flushed; so it is when
 * Windows ends while the program runs.  The close holds the program as a
 * request does, the driver held and the frame refusing commands, but it leaves
 * the windows as they are: the yield skips the keys and mouse clicks instead,
 * and the moves a list posts itself.  The program then waits, with the
 * hourglass, while the NetBIOS provider still owns commands of a closed NET
 * link (nba_close), which it may still write into: IW_DRAIN at a time, after
 * which a message box, over the program's window if one is left, asks whether
 * to wait again or end all the same, as a key ends INV's wait.
 *
 * Passwords.  Each is read from its control into a buffer on the stack only to
 * be sent, and the buffer is wiped once the request is made (sn_login wipes
 * its own).  The password controls are those of WEDIT.H, which keep no undo
 * copy, and each is wiped (we_wipe): the login dialog's at each attempt, every
 * one as its dialog is destroyed, however it ends, and the open dialog's when
 * Windows ends while it runs, which destroys no window.  A user's, typed in
 * the form dialog, goes from its control into the form's slot and is wiped
 * from both once the save that sent it is over (WFORM.H).
 *
 * Inside the program text is in the OEM character set (CP437), as the
 * data, the protocol, the settings and INV.MSG are, and the command
 * line, which Windows gives in its own character set, is converted at
 * once; a text goes back to the Windows set (OemToAnsi) as it is shown,
 * and a text typed into a control comes in through AnsiToOem.  The
 * program's own texts are the string table of INVWIN.RC.
 *
 * Test switches, left out of the usage text: /KEYS:file and /SNAP:file,
 * the key script and the snapshot file of the test driver (TDRV.H),
 * which runs the program from the script and, when /SNAP: was given or
 * a Q line ended the session, ends Windows after it; /SNAP: without
 * /KEYS: is a usage error.
 */
#define PAGER_CALLS
#define RQERR_CLASS
#define COMPAT_FIND
#define COMPAT_NETBIOS
#define COMPAT_UART
#define COMPAT_SOCKET
#define LINK_SERIAL
#define LINK_TCP
#define LINK_MBOX
#define ARGS_WINSW
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "MBXCLI.H"
#include "RQ.H"
#include "CFG.H"
#include "MSG.H"
#include "RQERR.H"
#include "FMODEL.H"
#include "MEMOIO.H"
#include "TUI.H"
#include "SCR.H"
#include "SESS.H"
#include "ARGS.H"
#include "INVMENU.H"
#include "RPT.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "SCRLFORM.H"
#include "SCRNFORM.H"
#include "SCRSFORM.H"
#include "SCRAFORM.H"
#include "SCRZFORM.H"
#include "SCRCFORM.H"
#include "SCRRFORM.H"
#include "TDRV.H"
#include "WDLG.H"
#include "WEDIT.H"
#include "WFORM.H"
#include "WCOLS.H"
#include "KEYSET.H"
#include "WLIST.H"
#include "WSCR.H"
#include "WPICK.H"
#include "WLOC.H"
#include "WCODE.H"
#include "WSVC.H"
#include "WLOAN.H"
#include "WUSER.H"
#include "WADMIN.H"
#include "WSRCH.H"
#include "WASSY.H"
#include "WCSV.H"
#include "WRPT.H"
#include "WSET.H"
#include "WHELP.H"
#include "WROWS.H"
#include "WTOOL.H"
#include "WSTAT.H"
#include "WSPLIT.H"
#include "WPREV.H"
#include "WPANE.H"
#include "INVWIN.H"

#define IW_CMDMAX       128     /* a DOS command tail and its NUL       */
/* argv: the program, then the words of the longest tail, of one
   character and a blank each. */
#define IW_MAXARGV      (1 + IW_CMDMAX / 2)
/* A string of the table, NUL included; make lint holds INVWIN.RC's
   strings to it (TEST/lint.py --strings). */
#define IW_FMTSZ        128
/* A text formatted from a string: its arguments at their longest, a
   path, the reason of a failed open and a number. */
#define IW_TEXTSZ       (IW_FMTSZ + DOS_PATHMAX + LINK_WHYSZ + TXT_U32LEN)
/* A panel's text formatted from a string: room for the string and the
   sum of the panels' arguments, the connection's target, the user and a
   count, more than any one of them needs, a count with a location's
   name among them. */
#define IW_PANELSZ      (IW_FMTSZ + LINK_MAXDIR + LINK_USERLEN + TXT_U32LEN)
/* A record named by a string with a row's key and a count, as a caption's
   ^1 for several rows (IW_S_MORE) or a command's stop (IW_S_STOPPED,
   after the failure's text). */
#define IW_ROWSSZ       (IW_FMTSZ + LV_KEYSZ + 2 * TXT_U32LEN)
/* The panels of the status bar, each but the last, the help panel, as
   wide as its text (status) and its edges and padding (IW_PANELPAD). */
#define IW_P_LINK       0       /* the connection                       */
#define IW_P_USER       1       /* the user and level                   */
#define IW_P_COUNT      2       /* the count of the list shown          */
#define IW_P_HELP       3       /* a menu entry's help, a note or a long
                                   request's text                       */
#define IW_NPANELS      4
#define IW_PANELPAD     (2 * (1 + WSB_PADX))
/* The count the count panel is as wide as, in its widest noun, for every
   screen alike. */
#define IW_WIDECOUNT    99999L
/* The Find box's width: average characters of its font, and its border's
   pixels. */
#define IW_FINDCH       16
#define IW_FINDEDGE     2
/* The panes of the screen shown (iw_panes): its list, the master, and
   beside it the screen's pane (ws_pane of WSCR.H), the split view. */
#define IW_NPANES       2
/* The least width of a pane of the split view, in average characters of
   the list font (split_at). */
#define IW_PANEMIN      8
/* The ms the master's selection stays on a row before the pane beside it
   follows (settle): on a link whose requests are fast (iw_exact), and on
   the others, where a load holds the program for seconds; and the
   frame's timer that counts them. */
#define IW_SETTLE       300
#define IW_SETTLE_SLOW  1000
#define IW_T_SETTLE     1
/* A second timer of the frame, set and killed at once (settle). */
#define IW_T_KICK       2
/* Where the pane's next load stands (iw_settle): nothing to load; the
   master may have moved, which the main loop checks; the timer runs; it
   has run out, and the main loop loads.  From IW_SETTLE_ARMED on the
   test driver is held (hold). */
#define IW_SETTLE_NONE  0
#define IW_SETTLE_CHECK 1
#define IW_SETTLE_ARMED 2
#define IW_SETTLE_DUE   3
/* The tool bar's buttons of Record entries, those before Find
   (tool_cmds). */
#define IW_NTOOLS       7
/* The screens whose count is not of items (noun_scrs). */
#define IW_NNOUNS       11
#define IW_NDOTS        3       /* the dots of an entry that asks       */
/* The frame's own message: open the Record menu as a popup, lParam its
   top left corner in screen coordinates (popup_input). */
#define IW_WM_POPUP     WM_USER
/* Where a Shift+F10 without a selected row clicks the list box, from
   its top left corner, in pixels. */
#define IW_POPAT        2
/* What popup_input has taken and waits to see released. */
#define IW_POP_KEY      0x01    /* Shift+F10's press                    */
#define IW_POP_BUTTON   0x02    /* a right button's press on the list   */
/* A key message's lParam while ALT is held. */
#define IW_ALTKEY       0x20000000L
/* The passwords of the password dialog. */
#define IW_NPW          (IW_C_REPEAT - IW_C_OLD + 1)
/* The ticks the NetBIOS provider gets to give back the commands of a
   closed link before the user is asked whether to wait again. */
#define IW_DRAIN        (10 * TMR_SEC)
/* The item form's record buttons: its parts, service records, loans and
   holds, and what is used with it (item_scopes, WROW_IACTS of WROWS.H),
   which WS_K_PARTS to WS_K_USED open from a list. */
#define IW_NACTS        (WS_K_USED - WS_K_PARTS + 1)
/* The screens a scoped one, the user list or the audit log goes back
   through, as INV's item form goes back through the items it showed
   (iw_backs). */
#define IW_NBACK        8
/* iw_back's mode when no item form is to be shown again. */
#define IW_NOFORM       0xFF
/* What the record a form shows is (rec_show's copy). */
#define IW_REC          0       /* a record                             */
#define IW_COPY         1       /* a copy just made (ITEM COPY), which a
                                   cancel before any save deletes       */
#define IW_COPYKEPT     2       /* a copy kept since its form went to a
                                   tab, which a cancel notes            */

/* A record key (key_of) is a virtual key, plus IW_M_SHIFT (INVWIN.H)
   with SHIFT and IW_CTRL with CTRL, or a command of the Record menu alone,
   a WS_K_ value of WSCR.H above them all (the build fails otherwise). */
#define IW_CTRL         0x200
extern char iw_codes_fit[1];
extern char iw_codes_fit[WS_K_MOVE > IW_CTRL + IW_M_SHIFT + 0xFF ? 1 : 2];

/* What a record key does on the screen shown (key_of); those from
   IW_K_EDIT to IW_K_MOVE change records, and need TUI_LVL_OPER. */
#define IW_K_NONE       0
#define IW_K_EDIT       1       /* the record in the form, FORM_EDIT    */
#define IW_K_NEW        2       /* a new record                         */
#define IW_K_DEL        3       /* the record deleted, after asking     */
#define IW_K_ACT        4       /* one of the screen's own commands     */
#define IW_K_COPY       5       /* the item duplicated (ITEM COPY)      */
#define IW_K_HOLD       6       /* a new loan, reservation or use       */
#define IW_K_MOVE       7       /* the record moved (ws_scr's move)     */
#define IW_K_VIEW       8       /* the record in the form, FORM_VIEW    */
#define IW_K_HOLDS      9       /* a location's contents                */
#define IW_K_SCOPE      10      /* an item's parts, service, loans and
                                   holds, or what is used with it       */
#define IW_K_ITEM       11      /* the row's item in the item form      */
#define IW_K_NEXT       12      /* the list's next view (wloan_view)    */
#define IW_K_ORDER      13      /* the list's next order                */
#define IW_K_BACK       14      /* the screen ESC leaves for            */
#define IW_K_TOP        15      /* the items not installed or all       */
#define IW_K_BELOW      16      /* a location's own items or all below  */
#define IW_K_PANE       17      /* the focus to the other pane of the
                                   split view (pane_next)               */
#define IW_K_REFRESH    18      /* the list fetched again               */
#define IW_K_ALL        19      /* every row of the list chosen         */
#define IW_K_ONE        20      /* the focus row alone chosen           */
/* The entries of the Record menu always there (rec_keys). */
#define IW_NCORE        4
/* The keys of a new loan, reservation and use, CTRL and a letter, in the
   order of SCR_K_LOAN to SCR_K_USE (hold_keys). */
#define IW_NHOLD        3

/* The outcomes of the dialogs, and IW_D_STAY while one goes on. */
#define IW_D_STAY       0
#define IW_D_IN         1       /* logged in, or the password changed   */
#define IW_D_MUSTCHG    2       /* logged in, the password to change    */
#define IW_D_CANCEL     3       /* IDCANCEL                             */
#define IW_D_LOGIN      4       /* the login to be asked for again      */
#define IW_D_QUIT       5       /* the program to end                   */

/* The program's own switches, by their index in iw_opts.own. */
#define IW_O_KEYS       0       /* /KEYS:                               */
#define IW_O_SNAP       1       /* /SNAP:                               */
#define IW_NOWN         2

/* The settings' WINCOLS widths are the rows of the lists' widths store,
   a row for each table of WROWS.H (the build fails unless the sizes
   agree). */
extern char iw_cols_fit[1];
extern char iw_cols_fit[(CFG_COLMAX == WCOL_MAX && CFG_NCOLS == WROW_NKEEP) ?
                        1 : 2];

/* A command on several rows finds its progress and its note from its
   Record entry's string, the highest IW_S_RUSE (the build fails unless
   they stay apart). */
extern char iw_labels_fit[1];
extern char iw_labels_fit[IW_S_RUSE < IW_G_DONE - IW_G_DOING ? 1 : 2];

/* A menu entry's level is compared with sn_login's (the build fails
   unless the ranges agree). */
extern char iw_levels_fit[1];
extern char iw_levels_fit[(SN_LVLMIN == TUI_LVL_READ &&
                           SN_LVLMAX == TUI_LVL_ADMIN) ? 1 : 2];

/* try_passwd finds the text of sn_passwd's local refusal st as
   IW_S_PWNOMATCH + st - SN_PW_NOMATCH (the build fails otherwise). */
extern char iw_pwtexts_fit[1];
extern char iw_pwtexts_fit[(IW_S_PWINVALID - IW_S_PWNOMATCH ==
                            SN_PW_INVALID - SN_PW_NOMATCH &&
                            IW_S_PWSAME - IW_S_PWNOMATCH ==
                            SN_PW_SAME - SN_PW_NOMATCH &&
                            IW_S_PWOLD - IW_S_PWNOMATCH ==
                            SN_PW_OLDWRONG - SN_PW_NOMATCH) ? 1 : 2];

/* start finds the reason a remote link did not open, its code rc, as
   IW_S_ENONB + rc - LINK_ENONB (the build fails otherwise). */
extern char iw_whys_fit[1];
extern char iw_whys_fit[(IW_S_ENAME - IW_S_ENONB == LINK_ENAME - LINK_ENONB &&
                         IW_S_ECALL - IW_S_ENONB == LINK_ECALL - LINK_ENONB &&
                         IW_S_ENOMEM - IW_S_ENONB ==
                         LINK_ENOMEM - LINK_ENONB &&
                         IW_S_EREFUSED - IW_S_ENONB ==
                         LINK_EREFUSED - LINK_ENONB &&
                         IW_S_ENOPORT - IW_S_ENONB ==
                         LINK_ENOPORT - LINK_ENONB &&
                         IW_S_EBUSY - IW_S_ENONB ==
                         LINK_EBUSY - LINK_ENONB &&
                         IW_S_ESETUP - IW_S_ENONB ==
                         LINK_ESETUP - LINK_ENONB &&
                         IW_S_ENOTCP - IW_S_ENONB ==
                         LINK_ENOTCP - LINK_ENONB &&
                         IW_S_ECANCEL - IW_S_ENONB ==
                         LINK_ECANCEL - LINK_ENONB &&
                         IW_S_EMBXARG - IW_S_ENONB ==
                         LINK_EMBXARG - LINK_ENONB) ? 1 : 2];

static const char k_class[] = "INVWIN";
static const char k_empty[] = "";
static const char w_item[] = "ITEM";
static const char w_get[] = "ITEM GET";
static const char w_del[] = "ITEM DEL";
static const char w_copy[] = "ITEM COPY";
/* The help topics (WHELP.H) of the password dialog, Getting started
   and the categories with their items. */
static const char k_passwd[] = "PASSWD";
static const char k_start[] = "START";
static const char k_bycat[] = "BYCAT";

/* The names of the program's own switches, in the order of IW_O_. */
static const char *const own_names[IW_NOWN] = {
    "KEYS:", "SNAP:"
};

/* The switches of the command line, OEM text in its copy. */
typedef struct {
    args a;
    const char *own[IW_NOWN];   /* each IW_O_ switch's value, or NULL   */
} iw_opts;

/* A screen that a scoped one, the user list or the audit log was opened
   from, which ESC opens again. */
typedef struct {
    const ws_scr *scr;
    char scope[LV_KEYSZ];       /* its scope's key, "" when not scoped  */
    char sel[LV_KEYSZ];         /* its list's row, "" without rows      */
    u8   order;                 /* the order its list had, of its
                                   source's                             */
    u8   mode;                  /* the mode of the item form the scoped
                                   one was opened from, shown again,
                                   or IW_NOFORM                         */
    u8   copy;                  /* that form's record, IW_REC or
                                   IW_COPYKEPT                          */
    u8   pane;                  /* the pane that had the focus          */
} iw_back;

/* A pane of the screen shown: a list and what it was opened with. */
typedef struct {
    wl *list;                   /* NULL without one                     */
    const ws_scr *scr;          /* the screen whose list it is          */
    const wcol_tab *cols;       /* its columns' tables, the first
                                   order's first                        */
    lv_src src;                 /* its source: the screen's, with fmt   */
    u8   ps;                    /* the rows of its pager's window       */
    char scope[LV_KEYSZ];       /* its scope's key: a scoped screen's,
                                   or the one its follow gave the pane
                                   beside the master; "" without one    */
} iw_pane;

/* A screen's own command in its dialog (ask): the command, the key of
   the row its OK sends for, the ID its +OK gives (act_send), "" when it
   gives none, whether nothing was sent, the location where the record
   already is chosen (WA_KEEP), and once asked for several rows the form
   of its values, for the other rows (act_row). */
typedef struct {
    const ws_act *a;
    const char *key;
    char id[LV_KEYSZ];
    u8   same;
    form *f;
} iw_act;

/* A new loan, reservation or use asked once for several rows (rec_holds):
   the definition of its forms, the form of the values asked, the form
   each other row is saved from, and its kind. */
typedef struct {
    const form_def *def;
    form *f;
    form *g;
    u8   kind;
} iw_hold;

/* A row's request of a command on several rows (each): sent for the row
   of key, with arg; RQ_OK or the failure's code, the failure in res. */
typedef int (*iw_row_fn)(void *arg, const char *key, rq_res *res);

static HINSTANCE iw_inst;
static HWND iw_frame;           /* NULL once destroyed                  */
static HWND iw_tool;            /* the tool bar, a child of iw_frame    */
static HWND iw_find;            /* its Find box                         */
static HWND iw_status;          /* the status bar, a child of iw_frame  */
static int iw_countw;           /* its count panel's width (chrome)     */
static HWND iw_focus;           /* the focus when busy began            */
static HWND iw_pwdlg;           /* the login or password dialog open    */
static link *iw_l;              /* NULL until open and once closed      */
/* The panes: the screen shown's list first, then, in the split view,
   its pane's. */
static iw_pane iw_panes[IW_NPANES];
static iw_pane *iw_cur = iw_panes;      /* the pane with the focus      */
static iw_pane *iw_move;        /* the pane whose move is under way     */
static HWND iw_splitter;        /* the split view's splitter, or NULL   */
static HWND iw_view;            /* the preview panel beside the master,
                                   in place of a second list, or NULL   */
static u8 iw_inview;            /* it has the focus, iw_cur the master  */
/* WINPANE. of the settings (CFG.H), by screen, the width of the
   locations' WINSPLIT='s. */
static u8 iw_wpane[CFG_NPANES][CFG_PANEVALS];
static char iw_shown[LV_KEYSZ]; /* the master's row that the pane beside
                                   it shows, "" for none                */
static u8 iw_noload;            /* the preview panel says IW_S_PNONE    */
static u8 iw_cw;                /* the list font's average width        */
static u8 iw_settle;            /* IW_SETTLE_                           */
static ctab iw_cats;            /* the categories while a list of items
                                   or parts shows; its b NULL otherwise */
/* The screens to go back to while a scoped one, the user list or the
   audit log is shown, the last opened last, iw_nback of them. */
static iw_back iw_backs[IW_NBACK];
static u8 iw_nback;
static u8 iw_order;             /* the order the frame's title names    */
static u8 iw_fit;               /* a pane's ps no longer fits: the main
                                   loop opens the screen again          */
static const char *iw_cfgpath;  /* the settings file, /CFG: or CFG_NAME */
static UINT iw_kind;            /* IW_S_LOCAL, IW_S_NET, IW_S_COM,
                                   IW_S_TCP or IW_S_MBOX                */
static u8 iw_busy;              /* a request is outstanding             */
static u8 iw_noted;             /* the help panel shows a note (note)   */
static u8 iw_moved;             /* the focused list has moved: its count
                                   is to be shown once the move is over
                                   (moved)                              */
static u8 iw_exact;             /* the list counts its rows (WL_EXACT)  */
static u8 iw_closing;           /* the link closes (close_link)         */
static u8 iw_level;             /* the level LOGIN gave, 0 when no login
                                   is on the session                    */
/* The connection's directory, server or port, in the Windows set, cut
   to fit; VER's text; the station; the user name the login dialog
   offers, the one logged in after a login; ITEMORD= of the settings. */
static char iw_target[LINK_MAXDIR + 1];
static char iw_version[RQ_TEXTLEN + 1];
static char iw_station[LINK_STALEN + 1];
static char iw_user[LINK_USERLEN + 1];
static char iw_itemord[CFG_ORDLEN + 1];
static const char *iw_mbox;     /* /MBOX:'s directory, OEM text         */
static char iw_sname[WF_NAMESZ];        /* the name of a scoped screen's
                                           scope, "" on any other       */
static HMENU iw_rmenu;          /* the Record menu, while logged in     */
static u8 iw_rsel;              /* a row was selected as it was filled,
                                   in the focused list                  */
static u8 iw_pop;               /* IW_POP_ flags (popup_input)          */
static rpt_geom iw_geom;        /* PAGELEN=, LABELW= and LABELH= of the
                                   settings, for the reports            */

HWND ws_modeless;
const char ws_k_items[] = "ITEMS";
const char ws_k_parts[] = "PARTS";

/* ---------------------------------------------------------------------
 * Texts.
 * --------------------------------------------------------------------- */

int ws_say(HWND owner, UINT st, const char *text)
{
    char title[IW_FMTSZ];

    LoadString(iw_inst, IW_S_TITLE, title, sizeof title);
    return MessageBox(owner, text, title, st);
}

int ws_say_oem(HWND owner, UINT st, const char *text)
{
    char t[MSG_MAXLEN + 1];

    txt_copy(t, text, (u16)sizeof t);
    OemToAnsi(t, t);
    return ws_say(owner, st, t);
}

/* Say string ids over owner (ws_say).  Its conversions take the strings
   given, s1 then s2, each in the Windows set and left out when NULL,
   then n. */
static void fail(HWND owner, UINT ids, const char *s1, const char *s2,
                 int n)
{
    char fmt[IW_FMTSZ];
    char text[IW_TEXTSZ];

    LoadString(iw_inst, ids, fmt, sizeof fmt);
    if (s2 != NULL)
        wsprintf(text, fmt, (LPCSTR)s1, (LPCSTR)s2, n);
    else if (s1 != NULL)
        wsprintf(text, fmt, (LPCSTR)s1, n);
    else
        wsprintf(text, fmt, n);
    ws_say(owner, MB_ICONSTOP, text);
}

/* Check the Browse entry of the screen shown and no other: that of the
   item list's order while the item list, the screen titled by it, is
   shown, and Browse by category while the categories, the screen titled
   ME_CATS, are. */
static void browse_check(void)
{
    const iw_pane *p;
    HMENU m;
    UINT c;

    p = iw_panes;
    m = GetMenu(iw_frame);
    if (m == NULL)
        return;
    for (c = SCMD_ITEMS_ID; c <= SCMD_ITEMS_SER; c++)
        CheckMenuItem(m, c, p->list != NULL && p->scr->title == MI_T_ID &&
                            c - SCMD_ITEMS_ID == iw_order ? MF_CHECKED
                                                          : MF_UNCHECKED);
    CheckMenuItem(m, IW_M_BYCAT, p->list != NULL && p->scr->title == ME_CATS ?
                                 MF_CHECKED : MF_UNCHECKED);
}

/* Mark the View menu's entries for the screen shown: Pane beside the
   list checked while the split view shows and grayed on a screen that
   has no pane, Refresh grayed without a list. */
static void view_check(void)
{
    const iw_pane *p;
    HMENU m;

    p = iw_panes;
    m = GetMenu(iw_frame);
    if (m == NULL)
        return;
    CheckMenuItem(m, IW_M_PANE, iw_splitter != NULL ? MF_CHECKED
                                                    : MF_UNCHECKED);
    EnableMenuItem(m, IW_M_PANE, p->list != NULL && p->scr->pane != NULL ?
                                 MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IW_M_KEY + VK_F5, p->list != NULL ? MF_ENABLED
                                                        : MF_GRAYED);
}

/* Give the frame its title: the program's, then, while a list is open, the
   screen's in the order its list (the first pane's) has in use, as INV
   titles it, a scoped screen's for its scope, or the one the screen names,
   a location's contents' followed by the items they show (IW_S_CTITLE),
   all or those not installed in another, at the location or everywhere
   below it too, as IW_S_LTITLE joins two titles; and check the Browse
   entry of that order (browse_check) and mark the View menu's
   (view_check). */
static void caption(void)
{
    char name[IW_FMTSZ];
    char fmt[IW_FMTSZ];
    char list[MSG_MAXLEN + 1];
    char text[2 * IW_FMTSZ + MSG_MAXLEN];
    const iw_pane *p;

    p = iw_panes;
    LoadString(iw_inst, IW_S_TITLE, name, sizeof name);
    if (p->list != NULL)
        iw_order = wl_order(p->list);
    browse_check();
    view_check();
    if (p->list == NULL) {
        SetWindowText(iw_frame, name);
        return;
    }
    if (p->scr->name != NULL)
        txt_copy(list, p->scr->name(), (u16)sizeof list);
    else if (p->scr->flags & WS_SCOPED)
        txt_copy(list, msg_sub(p->scr->title, iw_sname, NULL),
                 (u16)sizeof list);
    else
        msg_get((u16)(p->scr->title + iw_order), list, (u16)sizeof list);
    if (p->scr->src == &wloc_csrc) {
        LoadString(iw_inst, IW_S_CTITLE, fmt, sizeof fmt);
        wsprintf(text, fmt, (LPCSTR)list,
                 (LPCSTR)msg(wloc_top ? MO_S_TOP : MO_S_ALL),
                 (LPCSTR)msg(wloc_all ? MO_S_BELOW : MO_S_HERE));
        txt_copy(list, text, (u16)sizeof list);
    }
    OemToAnsi(list, list);
    LoadString(iw_inst, IW_S_LTITLE, fmt, sizeof fmt);
    wsprintf(text, fmt, (LPCSTR)name, (LPCSTR)list);
    SetWindowText(iw_frame, text);
}

/* ---------------------------------------------------------------------
 * Requests.
 * --------------------------------------------------------------------- */

int ws_active(void)
{
    HWND a;

    a = GetActiveWindow();
    return a != NULL && GetWindowTask(a) == GetCurrentTask();
}

/* 1 while a record command may start: the frame enabled, so no request
   of its own (busy), no command's rows (each) and no dialog or message
   box over it is under way.  A key a list had posted itself before one
   of those began is dispatched by its loop, for a screen and rows that
   may no longer be the ones it was typed at. */
static int idle(void)
{
    return IsWindowEnabled(iw_frame);
}

/* Hold the test driver while a request is outstanding or the pane
   beside the master is to load (iw_settle), so that a key script's next
   line finds it loaded. */
static void hold(void)
{
    td_busy(iw_busy || iw_settle >= IW_SETTLE_ARMED);
}

/* End any drag in the frame's children, the tool bar's button's, the
   splitter's and a list's heading, divider or row, as the frame is
   disabled or goes busy. */
static void cancel_drags(void)
{
    const iw_pane *p;

    SendMessage(iw_tool, WM_CANCELMODE, 0, 0L);
    if (iw_splitter != NULL)
        SendMessage(iw_splitter, WM_CANCELMODE, 0, 0L);
    for (p = iw_panes; p < iw_panes + IW_NPANES; p++)
        if (p->list != NULL)
            SendMessage(wl_hwnd(p->list), WM_CANCELMODE, 0, 0L);
}

/* Drop the messages numbered first to last that wait for the program.
   Windows gives a WM_QUIT through any such range: it is posted again
   for the main loop, as the yield leaves it, and ends the dropping. */
static void discard(UINT first, UINT last)
{
    MSG m;

    while (PeekMessage(&m, NULL, first, last, PM_REMOVE | PM_NOYIELD))
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            return;
        }
}

/* Hold the window w, the frame or the dialog that sends a request, busy
   while on is 1: any drag cancelled first (cancel_drags), which gives
   the focus back to the list that had it and puts the arrow back, then
   the hourglass, shown also without a mouse, which Windows otherwise
   keeps hidden, w disabled, the test driver held.  Free it again with
   0, dropping the keys and mouse clicks queued meanwhile (discard), on
   the window's frame too, and giving the focus, which disabling w took
   away, back while the program is still active (ws_active). */
static void busy(HWND w, int on)
{
    if (on) {
        cancel_drags();
        iw_focus = GetFocus();
    } else {
        discard(WM_KEYFIRST, WM_KEYLAST);
        discard(WM_MOUSEFIRST, WM_MOUSELAST);
        discard(WM_NCMOUSEMOVE, WM_NCMBUTTONDBLCLK);
    }
    iw_busy = (u8)on;
    hold();
    SetCursor(LoadCursor(NULL, on ? IDC_WAIT : IDC_ARROW));
    if (!GetSystemMetrics(SM_MOUSEPRESENT))
        ShowCursor(on);
    EnableWindow(w, !on);
    if (!on && iw_focus != NULL && ws_active())
        SetFocus(iw_focus);
}

/* The yield hook of dos_idle: dispatch the messages waiting, letting
   the other programs run, those of ws_modeless through it as a
   dialog's, and leave a WM_QUIT for the main loop.  While the link
   closes, the keys, the mouse clicks and the private messages (from
   WM_USER on: the moves a list posts itself, WLIST.C) are skipped, as
   none may act then.  So no close may wait for a private message: NET's
   and SERIAL's poll, and TCP's waits for nothing, though its socket's
   events come as one (sk_close, COMPAT.H).  An Esc pressed during a
   request (iw_busy), the stop that wl_wait looks for between two
   requests, is not dispatched, which would lose it, but posted again
   once the others are, so that it waits in the queue for that wait, or
   for the request's end, which drops it with the other keys (busy).
   One with ALT or CTRL is Windows' own key, for the next program or
   the Task List, and dispatched as any key: Windows does nothing with
   it while the active window is disabled, as a request's is. */
static void yield(void)
{
    MSG m;
    MSG esc;

    esc.message = 0;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            return;
        }
        if (iw_closing &&
            ((m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) ||
             (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST) ||
             m.message >= WM_USER))
            continue;
        if (ws_modeless != NULL && IsDialogMessage(ws_modeless, &m))
            continue;
        if (iw_busy && m.wParam == VK_ESCAPE &&
            GetKeyState(VK_CONTROL) >= 0 &&
            (m.message == WM_KEYDOWN || (m.message == WM_SYSKEYDOWN &&
                                         !(m.lParam & IW_ALTKEY)))) {
            esc = m;
            continue;
        }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    if (esc.message != 0)
        PostMessage(esc.hwnd, esc.message, esc.wParam, esc.lParam);
}

int ws_send(HWND w, rq *q, rq_fn fn, void *arg, rq_res *res)
{
    int rc;

    if (w == NULL)
        w = iw_frame;
    busy(w, 1);
    rc = rq_send(iw_l, q, fn, arg, res);
    busy(w, 0);
    return rc;
}

u8 ws_fail(HWND w, const rq_res *res)
{
    rqe e;

    rqe_class(&e, res, iw_level);
    OemToAnsi(e.text, e.text);
    ws_say(w, WD_FAILICON(e.show), e.text);
    return e.act;
}

/* Send q from the frame, its lines to fn with arg (ws_send); RQ_OK, or
   the code of the failure res, shown over the frame (ws_fail), with
   *act its action, RQE_STAY on success. */
static int request(rq *q, rq_fn fn, void *arg, rq_res *res, u8 *act)
{
    int rc;

    rc = ws_send(iw_frame, q, fn, arg, res);
    *act = rc == RQ_OK ? RQE_STAY : ws_fail(iw_frame, res);
    return rc;
}

/* The mailbox directory dir, its folder of requests and its folder of
   responses, which INVSERV needs and never makes, each "" or a folder
   name after dir (mbox_lack). */
static const char *const mbx_parts[] = { "", MBXCLI_REQDIR, MBXCLI_RSPDIR };

/* The index in mbx_parts of the first of them that the mailbox dir lacks
   (dos_find_first's CF_ENOPATH), or -1 when it has them all, or when dir
   is too long for a mailbox, which the link's open refuses. */
static int mbox_lack(const char *dir)
{
    char pat[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    dos_find f;
    u16 n;
    int i;

    n = (u16)strlen(dir);
    if (n > MBXCLI_MAXDIR)
        return -1;
    for (i = 0; i < (int)(sizeof mbx_parts / sizeof mbx_parts[0]); i++) {
        strcpy(pat, dir);
        if (n > 0 && strchr("\\/:", pat[n - 1]) == NULL)
            strcat(pat, "\\");
        strcat(pat, mbx_parts[i]);
        strcat(pat, i > 0 ? "\\*.*" : "*.*");
        if (dos_find_first(&f, pat, name) == CF_ENOPATH)
            return i;
    }
    return -1;
}

/* Send HELLO, and VER the first time (sn_hello), from the frame; 1, or
   0 with the failure said: no answer when the link was lost or closed,
   as it is when no server reads a mailbox directory or a serial line,
   but a mailbox directory that is not there or lacks a folder
   (mbox_lack), through which no server could answer; else the server's
   refusal, or a failure without its reason (a response that could not
   be read) as a request's is said (ws_fail). */
static int hello(void)
{
    char fmt[IW_FMTSZ];
    char why[LINK_WHYSZ];
    rq_res res;
    int rc;

    busy(iw_frame, 1);
    rc = sn_hello(iw_l, iw_station, iw_version, &res);
    busy(iw_frame, 0);
    if (rc == RQ_OK)
        return 1;
    if (rc == RQ_ELOST || rc == RQ_ECLOSED) {
        rc = iw_kind == IW_S_MBOX ? mbox_lack(iw_mbox) : -1;
        if (rc >= 0) {
            LoadString(iw_inst, rc == 0 ? IW_S_ENODIR : IW_S_ENOSUB, fmt,
                       sizeof fmt);
            wsprintf(why, fmt, (LPCSTR)mbx_parts[rc]);
            fail(iw_frame, IW_S_NOMBOX, iw_target, why, 0);
        } else {
            fail(iw_frame, IW_S_NOANSWER, iw_target, NULL, 0);
        }
    } else if (res.text[0] == '\0') {
        ws_fail(iw_frame, &res);
    } else {
        OemToAnsi(res.text, res.text);
        fail(iw_frame, IW_S_REFUSED, iw_target, res.text, res.code);
    }
    return 0;
}

/* Open the link of the switches o, with the settings c: LOCAL on the
   data directory dir, NET, SERIAL, TCP or MBOX; then greet it from the
   station of sn_station (hello), taken before the open for MBOX, whose
   request files it names, else once the link is open, NET's from the
   name it called from.  MBOX is refused sn_station's fallback, with the
   settings file named: every computer without a name would share its
   files, MS Network Client giving DOS no machine name.  1, or 0 with
   the failure said: LOCAL's reason and error, else INV's reason and any
   NetBIOS return code or Winsock error. */
static int start(const iw_opts *o, const cfg *c, const char *dir)
{
    char path[DOS_PATHMAX];
    char why[LINK_WHYSZ];
    UINT ids;
    u16 wserr;
    u8 nbrc;
    int rc;

    nbrc = NB_OK;
    wserr = 0;
    if (iw_kind == IW_S_MBOX && sn_station(iw_station, NULL, c->station)) {
        OemToAnsi(iw_cfgpath, path);
        fail(iw_frame, IW_S_NOSTA, iw_target, path, 0);
        return 0;
    }
    busy(iw_frame, 1);
    if (iw_kind == IW_S_NET)
        rc = link_open_net(&iw_l, o->a.net, &nbrc);
    else if (iw_kind == IW_S_TCP)
        rc = link_open_tcp(&iw_l, o->a.ip, o->a.port, &wserr);
    else if (iw_kind == IW_S_COM)
        rc = link_open_serial(&iw_l, o->a.com, c->baud,
                              (u8)(c->rtscts ? UART_F_RTSCTS : 0));
    else if (iw_kind == IW_S_MBOX)
        rc = link_open_mbox(&iw_l, o->a.mbox, iw_station);
    else
        rc = link_open_local(&iw_l, dir, NULL, why, (u16)sizeof why);
    busy(iw_frame, 0);
    if (rc != 0) {
        ids = IW_S_NOOPEN;
        if (iw_kind == IW_S_LOCAL) {
            OemToAnsi(why, why);
        } else {
            LoadString(iw_inst, IW_S_ENONB + rc - LINK_ENONB, why,
                       sizeof why);
            ids = iw_kind == IW_S_COM ? IW_S_NOPORT :
                  iw_kind == IW_S_MBOX ? IW_S_NOMBOX :
                  nbrc != NB_OK ? IW_S_NOCALLNB :
                  wserr != 0 ? IW_S_NOCALLWS : IW_S_NOCALL;
            rc = nbrc != NB_OK ? nbrc : wserr;
        }
        fail(iw_frame, ids, iw_target, why, rc);
        return 0;
    }
    if (iw_kind != IW_S_MBOX)
        sn_station(iw_station, link_netname(iw_l), c->station);
    return hello();
}

/* Close the link, holding the program as busy does, as nothing may
   reach the link while it waits: the frame refusing commands (iw_busy),
   the test driver held, and the input to every window and the lists'
   posted moves skipped by the yield (iw_closing), which leaves the
   windows as they are.  Then, while
   the NetBIOS provider still owns commands of a closed NET link
   (nba_stuck), let the other programs run, IW_DRAIN at a time, until
   the user, asked over the frame's last active window while it is
   there, no longer chooses to wait again.  The hourglass shows
   meanwhile; the frame still refuses commands after.  link_close's
   result. */
static int close_link(void)
{
    char fmt[IW_FMTSZ];
    char text[IW_TEXTSZ];
    u32 t;
    u16 n;
    int rc;

    iw_busy = 1;
    td_busy(1);
    iw_closing = 1;
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    rc = link_close(iw_l);
    iw_l = NULL;
    ct_close(&iw_cats);
    for (;;) {
        t = tmr_ticks();
        while ((n = nba_stuck()) != 0 && tmr_since(t) < IW_DRAIN)
            dos_idle();
        if (n == 0)
            break;
        LoadString(iw_inst, IW_S_NBBUSY, fmt, sizeof fmt);
        wsprintf(text, fmt, (int)n);
        if (ws_say(iw_frame != NULL ? GetLastActivePopup(iw_frame) : NULL,
                   MB_RETRYCANCEL | MB_ICONEXCLAMATION, text) != IDRETRY)
            break;
        SetCursor(LoadCursor(NULL, IDC_WAIT));
    }
    iw_closing = 0;
    td_busy(0);
    return rc;
}

/* ---------------------------------------------------------------------
 * Dialogs.
 * --------------------------------------------------------------------- */

/* Run the dialog of template id over the frame with the procedure proc,
   lp its WM_INITDIALOG lParam; the dialog's result, or -1 when it could
   not be made, which IW_S_NOFORM says. */
static int dialog(UINT id, FARPROC proc, LPARAM lp)
{
    FARPROC p;
    int r;

    p = MakeProcInstance(proc, iw_inst);
    r = -1;
    if (p != NULL) {
        r = DialogBoxParam(iw_inst, MAKEINTRESOURCE(id), iw_frame,
                           (DLGPROC)p, lp);
        FreeProcInstance(p);
    }
    if (r == -1)
        fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
    return r;
}

/* Give the focus to the control id of the dialog dlg, its text
   selected. */
static void focus(HWND dlg, int id)
{
    SendMessage(dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(dlg, id), 1L);
}

/* Wipe every password control of the dialog dlg (we_wipe). */
static void wipe_pw(HWND dlg)
{
    HWND e;
    int id;

    for (id = IW_C_PASS; id <= IW_C_REPEAT; id++) {
        e = GetDlgItem(dlg, id);
        if (e != NULL)
            we_wipe(e);
    }
}

/* Show the failure res of a request sent from the dialog dlg as
   rqe_class says: in IW_C_MSG or in a message box over dlg.  The
   outcome that follows, IW_D_STAY while the session goes on. */
static int failed(HWND dlg, const rq_res *res)
{
    rqe e;

    rqe_class(&e, res, iw_level);
    OemToAnsi(e.text, e.text);
    if (e.show == RQE_LINE)
        SetDlgItemText(dlg, IW_C_MSG, e.text);
    else
        ws_say(dlg, MB_ICONSTOP, e.text);
    return e.act == RQE_STAY ? IW_D_STAY :
           e.act == RQE_LOGIN ? IW_D_LOGIN : IW_D_QUIT;
}

/* Send LOGIN with the user name and password of the login dialog dlg,
   when both are given, else move the focus to the one missing; the
   outcome.  A refused login holds the dialog busy for about a second
   once its refusal shows, as INV pauses, so that guesses typed at it
   come no faster. */
static int try_login(HWND dlg)
{
    char user[LINK_USERLEN + 1];
    char pw[SN_PWSZ];
    char name[LINK_FULLNAMELEN + 1];
    rq_res res;
    u8 level;
    int st;
    int r;

    if (GetDlgItemText(dlg, IW_C_USER, user, (int)sizeof user) == 0) {
        focus(dlg, IW_C_USER);
        return IW_D_STAY;
    }
    if (GetDlgItemText(dlg, IW_C_PASS, pw, (int)sizeof pw) == 0) {
        focus(dlg, IW_C_PASS);
        return IW_D_STAY;
    }
    we_wipe(GetDlgItem(dlg, IW_C_PASS));
    SetDlgItemText(dlg, IW_C_MSG, k_empty);
    AnsiToOem(user, user);
    AnsiToOem(pw, pw);
    busy(dlg, 1);
    st = sn_login(iw_l, user, pw, &level, name, &res);
    busy(dlg, 0);
    if (st == SN_IN || st == SN_MUSTCHG) {
        strcpy(iw_user, user);
        iw_level = level;
        return st == SN_IN ? IW_D_IN : IW_D_MUSTCHG;
    }
    if (st == SN_TOOMANY) {
        fail(dlg, IW_S_TOOMANY, NULL, NULL, 0);
        return IW_D_QUIT;
    }
    focus(dlg, IW_C_PASS);
    r = failed(dlg, &res);
    if (st == SN_REFUSED) {
        busy(dlg, 1);
        tmr_delay(TMR_SEC);
        busy(dlg, 0);
    }
    return r;
}

/* Send PASSWD with the passwords of the password dialog dlg once
   sn_passwd's checks pass, else show the refusal; the outcome. */
static int try_passwd(HWND dlg)
{
    char pw[IW_NPW][SN_PWSZ];
    char fmt[IW_FMTSZ];
    char text[IW_TEXTSZ];
    rq_res res;
    int st;
    int i;

    for (i = 0; i < IW_NPW; i++) {
        GetDlgItemText(dlg, IW_C_OLD + i, pw[i], SN_PWSZ);
        AnsiToOem(pw[i], pw[i]);
    }
    SetDlgItemText(dlg, IW_C_MSG, k_empty);
    busy(dlg, 1);
    st = sn_passwd(iw_l, pw[0], pw[1], pw[2], &res);
    busy(dlg, 0);
    txt_wipe(pw, (u16)sizeof pw);
    if (st == SN_PW_DONE)
        return IW_D_IN;
    if (st == SN_PW_FAILED)
        return failed(dlg, &res);
    LoadString(iw_inst, IW_S_PWNOMATCH + st - SN_PW_NOMATCH, fmt,
               sizeof fmt);
    wsprintf(text, fmt, LINK_PWMIN, LINK_PWMAX);
    SetDlgItemText(dlg, IW_C_MSG, text);
    focus(dlg, st == SN_PW_OLDWRONG ? IW_C_OLD : IW_C_NEW);
    return IW_D_STAY;
}

/* Make the dialog dlg iw_pwdlg, each of its password controls one of
   WEDIT.H, or disabled when it cannot be, so that no password goes into
   an edit control that keeps undo copies. */
static void pw_open(HWND dlg)
{
    HWND e;
    int id;

    iw_pwdlg = dlg;
    for (id = IW_C_PASS; id <= IW_C_REPEAT; id++) {
        e = GetDlgItem(dlg, id);
        if (e != NULL && !we_attach(e, 0))
            EnableWindow(e, FALSE);
    }
}

/* The messages the login and password dialogs share, for the dialog
   dlg: IDOK runs send (try_login or try_passwd), whose outcome but
   IW_D_STAY ends the dialog, IDCANCEL ends it with IW_D_CANCEL, and
   WM_DESTROY, however the dialog ends, wipes its password controls and
   gives them back their own procedure (we_detach).  TRUE when wm has
   been handled. */
static BOOL pw_dialog(HWND dlg, UINT wm, WPARAM wp, int (*send)(HWND))
{
    HWND e;
    int id;
    int r;

    if (wm == WM_DESTROY) {
        for (id = IW_C_PASS; id <= IW_C_REPEAT; id++) {
            e = GetDlgItem(dlg, id);
            if (e != NULL)
                we_detach(e);
        }
        iw_pwdlg = NULL;
        return FALSE;
    }
    if (wm != WM_COMMAND || (wp != IDOK && wp != IDCANCEL))
        return FALSE;
    r = wp == IDOK ? send(dlg) : IW_D_CANCEL;
    if (r != IW_D_STAY)
        EndDialog(dlg, r);
    return TRUE;
}

BOOL WCALLBACK login_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    char text[RQ_TEXTLEN + 1];

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    if (wm != WM_INITDIALOG)
        return pw_dialog(dlg, wm, wp, try_login);
    wd_centre(dlg);
    OemToAnsi(iw_version, text);
    SetWindowText(dlg, text);
    OemToAnsi(iw_user, text);
    SetDlgItemText(dlg, IW_C_USER, text);
    SendDlgItemMessage(dlg, IW_C_USER, EM_LIMITTEXT, LINK_USERLEN, 0L);
    pw_open(dlg);
    if (iw_user[0] == '\0')
        return TRUE;
    SetFocus(GetDlgItem(dlg, IW_C_PASS));
    return FALSE;
}

/* lp is 1 for the forced change after a login, 0 when chosen. */
BOOL WCALLBACK passwd_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    char text[MSG_MAXLEN + 1];

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    if (wm != WM_INITDIALOG)
        return pw_dialog(dlg, wm, wp, try_passwd);
    wd_centre(dlg);
    pw_open(dlg);
    if (lp) {
        msg_get(MT_MUSTCHG, text, (u16)sizeof text);
        OemToAnsi(text, text);
        SetDlgItemText(dlg, IW_C_MSG, text);
        LoadString(iw_inst, IW_S_LOGOUT, text, sizeof text);
        SetDlgItemText(dlg, IDCANCEL, text);
    }
    return TRUE;
}

BOOL WCALLBACK about_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    char text[RQ_TEXTLEN + 1];

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    if (wm == WM_INITDIALOG) {
        wd_centre(dlg);
        OemToAnsi(iw_version, text);
        SetDlgItemText(dlg, IW_C_VERSION, text);
        return TRUE;
    }
    if (wm == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(dlg, 0);
        return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------------------------
 * The menu bar.
 * --------------------------------------------------------------------- */

/* 1 when the shared command cmd always opens a dialog box in which the
   user completes, cancels or closes it, so that its entry's name ends
   in IW_NDOTS dots, the rule of INVWIN.H. */
static int asks(u8 cmd)
{
    switch (cmd) {
    case SCMD_ITEM_NEW:
    case SCMD_CATS:
    case SCMD_LOC_NEW:
    case SCMD_LTYPES:
    case SCMD_FIND:
    case SCMD_SERIAL:
    case SCMD_FILTER:
    case SCMD_RPT_LIST:
    case SCMD_RPT_VALUE:
    case SCMD_RPT_LOANS:
    case SCMD_LBL_ITEM:
    case SCMD_LBL_BIN:
    case SCMD_BACKUP:
    case SCMD_RESTORE:
    case SCMD_AUDIT:
    case SCMD_SESSIONS:
    case SCMD_EXPORT:
    case SCMD_IMPORT:
    case SCMD_SETTINGS:
    case SCMD_PASSWD:
        return 1;
    default:
        return 0;
    }
}

/* Message n as the menu text of command cmd into t (MSG_MAXLEN + 1
   bytes), in the Windows set: the entry's name, then IW_NDOTS dots when
   the command asks (asks).  The name of the key INV gives the entry,
   after the first two blanks in a row, is left out with the blanks: the
   keys are INV's, and INVWIN's CTRL+F gives the Find box the focus, not
   Find text's dialog. */
static void menu_text(char *t, u16 n, u8 cmd)
{
    char *k;

    msg_get(n, t, MSG_MAXLEN + 1 - IW_NDOTS);
    k = strstr(t, "  ");
    if (k == NULL)
        k = t + strlen(t);
    if (asks(cmd)) {
        memset(k, '.', IW_NDOTS);
        k += IW_NDOTS;
    }
    *k = '\0';
    OemToAnsi(t, t);
}

/* Append to the menu m the entry of command id, or with fl MF_POPUP the
   popup menu id, showing string ids. */
static void menu_add(HMENU m, UINT fl, UINT id, UINT ids)
{
    char t[IW_FMTSZ];

    LoadString(iw_inst, ids, t, sizeof t);
    AppendMenu(m, fl, id, t);
}

/* Give the frame the menu bar of the user's level, an entry above it
   grayed: File, holding the entries of the main loop's commands, Log
   out and Exit (IW_S_MEXIT in place of Quit's text), then the menus of
   the table, Browse by category, INVWIN's own, after the table's last
   Browse entry, the Record menu, which each list fills (rec_menu), View,
   whose entries each screen marks (view_check), and Help. */
static void menu_on(void)
{
    char t[MSG_MAXLEN + 1];
    const tui_menu *mn;
    const tui_item *it;
    HMENU bar;
    HMENU file;
    HMENU pop;

    bar = CreateMenu();
    file = CreatePopupMenu();
    menu_add(bar, MF_POPUP, (UINT)file, IW_S_MFILE);
    for (mn = invmenu_bar; mn < invmenu_bar + INVMENU_COUNT; mn++) {
        pop = CreatePopupMenu();
        for (it = mn->items; it < mn->items + mn->nitems; it++) {
            menu_text(t, it->label, it->cmd);
            if (it->cmd == TUI_CMD_QUIT) {
                AppendMenu(file, MF_SEPARATOR, 0, NULL);
                LoadString(iw_inst, IW_S_MEXIT, t, sizeof t);
            }
            AppendMenu(it->cmd > TUI_CMD_MAX ? file : pop,
                       it->level <= iw_level ? MF_STRING :
                                               MF_STRING | MF_GRAYED,
                       it->cmd, t);
            if (it->cmd == SCMD_ITEMS_SER)
                menu_add(pop, MF_STRING, IW_M_BYCAT, IW_S_MBYCAT);
        }
        menu_text(t, mn->label, TUI_CMD_NONE);
        AppendMenu(bar, MF_POPUP, (UINT)pop, t);
    }
    iw_rmenu = CreatePopupMenu();
    menu_add(bar, MF_POPUP, (UINT)iw_rmenu, IW_S_MREC);
    pop = CreatePopupMenu();
    menu_add(pop, MF_STRING, IW_M_PANE, IW_S_VPANE);
    menu_add(pop, MF_STRING, IW_M_KEY + VK_F5, IW_S_VREFRESH);
    menu_add(bar, MF_POPUP, (UINT)pop, IW_S_MVIEW);
    pop = CreatePopupMenu();
    menu_add(pop, MF_STRING, IW_M_HELP, IW_S_MHELPON);
    menu_add(pop, MF_STRING, IW_M_START, IW_S_MSTART);
    menu_add(pop, MF_STRING, IW_M_KEYS, IW_S_MKEYS);
    AppendMenu(pop, MF_SEPARATOR, 0, NULL);
    menu_add(pop, MF_STRING, IW_M_ABOUT, IW_S_MABOUT);
    menu_add(bar, MF_POPUP, (UINT)pop, IW_S_MHELP);
    SetMenu(iw_frame, bar);
}

/* Post the box of list v a right button's press and release at the
   middle of its selected row, else IW_POPAT pixels into it
   (popup_input). */
static void popup_click(wl *v)
{
    RECT r;
    HWND box;
    LPARAM at;
    int i;

    box = GetDlgItem(wl_hwnd(v), WL_BOXID);
    at = MAKELPARAM(IW_POPAT, IW_POPAT);
    i = wl_focus(v);
    if (i >= 0 && SendMessage(box, LB_GETITEMRECT, (WPARAM)i,
                              (LPARAM)(RECT FAR *)&r) != LB_ERR)
        at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    PostMessage(box, WM_RBUTTONDOWN, MK_RBUTTON, at);
    PostMessage(box, WM_RBUTTONUP, 0, at);
}

/* 1 when m, the right button or Shift+F10, which open the Record menu as
   a popup over the list, is taken.  A press of the right button on a
   list box, either pane's, which then has the focus (pane_follow), while
   the frame is not busy, makes the row under it the selected one, the
   rows chosen kept when it is one of them (wl_right), and its release
   there, after such a press, asks the frame to open the popup at it
   (IW_WM_POPUP) once the list has taken the press.
   Shift+F10 without CTRL and ALT, for a list box, clicks the focused one
   so at the selected row: its press is taken, so that it does not open
   the menu bar, and the click waits for its release, so that the popup
   opens once the key is up.  The preview panel takes both as a list box
   does, for the master's Record menu, the item shown its row: the press
   gives it the focus, and Shift+F10 opens the popup IW_POPAT pixels into
   it, by a release posted there.  Neither is taken while a window of the
   program holds the mouse, a press or a drag under way. */
static int popup_input(const MSG *m)
{
    const iw_pane *q;
    wl *v;
    POINT p;
    u8 was;

    was = iw_pop;
    if (m->message == WM_RBUTTONUP)
        iw_pop &= (u8)~IW_POP_BUTTON;
    if (m->message == WM_SYSKEYUP && m->wParam == VK_F10) {
        iw_pop &= (u8)~IW_POP_KEY;
        if (!(was & IW_POP_KEY))
            return 0;
        if (iw_busy)
            return 1;
        if (iw_inview) {
            iw_pop |= IW_POP_BUTTON;
            PostMessage(iw_view, WM_RBUTTONUP, 0,
                        MAKELPARAM(IW_POPAT, IW_POPAT));
        } else if (iw_cur->list != NULL) {
            popup_click(iw_cur->list);
        }
        return 1;
    }
    if (iw_busy || GetCapture() != NULL)
        return 0;
    v = NULL;
    for (q = iw_panes; q < iw_panes + IW_NPANES; q++)
        if (q->list != NULL &&
            m->hwnd == GetDlgItem(wl_hwnd(q->list), WL_BOXID))
            v = q->list;
    if (v == NULL && (iw_view == NULL || m->hwnd != iw_view))
        return 0;
    switch (m->message) {
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        if (v != NULL)
            wl_right(v, (int)LOWORD(m->lParam), (int)HIWORD(m->lParam));
        else
            SetFocus(iw_view);
        iw_pop |= IW_POP_BUTTON;
        return 1;
    case WM_RBUTTONUP:
        if (!(was & IW_POP_BUTTON))
            return 0;
        p.x = (int)LOWORD(m->lParam);
        p.y = (int)HIWORD(m->lParam);
        ClientToScreen(m->hwnd, &p);
        PostMessage(iw_frame, IW_WM_POPUP, 0, MAKELPARAM(p.x, p.y));
        return 1;
    case WM_SYSKEYDOWN:
        if (m->wParam != VK_F10 || GetKeyState(VK_SHIFT) >= 0 ||
            GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0)
            return 0;
        iw_pop |= IW_POP_KEY;
        return 1;
    default:
        return 0;
    }
}

/* End the program, as the frame's close box does. */
static void quit(void)
{
    PostMessage(iw_frame, WM_CLOSE, 0, 0L);
}

/* Leave as a failure's action act says (rqe_class): log out, by the
   command posted, or end the program; RQE_STAY does nothing. */
static void leave(u8 act)
{
    if (act == RQE_LOGIN)
        PostMessage(iw_frame, WM_COMMAND, TUI_CMD_LOGIN, 0L);
    else if (act != RQE_STAY)
        quit();
}

/* ---------------------------------------------------------------------
 * The frame's layout.
 * --------------------------------------------------------------------- */

/* The commands of the tool bar's buttons in the order of IW_S_TOOL: the
   Record entries' of New, Open, Edit, Delete, Move, Install and Lend,
   then Find's. */
static const UINT tool_cmds[IW_NTOOLS + 1] = {
    IW_M_KEY + VK_INSERT, IW_M_KEY + VK_RETURN, IW_M_KEY + IW_CTRL + 'E',
    IW_M_KEY + VK_DELETE, IW_M_KEY + WS_K_MOVE, IW_M_KEY + WS_K_INSTALL,
    IW_M_KEY + IW_CTRL + 'L', IW_M_FIND
};

/* The text of the Record entry each button but Find stands for, in the
   order of tool_cmds; New stands for Install part too, INS's on an
   item's parts (rec_menu). */
static const u8 tool_texts[IW_NTOOLS] = {
    IW_S_RNEW, IW_S_ROPEN, IW_S_REDIT, IW_S_RDEL, IW_S_RMOVE, IW_S_RINTO,
    IW_S_RLEND
};

/* Set panel i of the status bar to text, in the Windows set, NULL for
   none. */
static void panel(int i, const char *text)
{
    SendMessage(iw_status, WSBM_SETTEXT, (WPARAM)i, (LPARAM)(LPCSTR)text);
}

/* Gray every button of the tool bar and the Find box, or, with on, make
   Find and the box usable, the other buttons left to the Record menu
   (rec_menu). */
static void tools_on(int on)
{
    int k;

    if (!on)
        for (k = 0; k < IW_NTOOLS; k++)
            EnableWindow(GetDlgItem(iw_tool, tool_cmds[k]), FALSE);
    EnableWindow(GetDlgItem(iw_tool, tool_cmds[IW_NTOOLS]), on);
    EnableWindow(iw_find, on);
}

/* The width of panel i of the status bar for text, set as its text, in
   the font of dc: the text as the panel keeps it, WSB_MAXCELL bytes at
   most, and its edges and padding. */
static int fit(HDC dc, int i, const char *text)
{
    panel(i, text);
    return (int)LOWORD(GetTextExtent(dc, text,
                                     min((int)strlen(text), WSB_MAXCELL))) +
           IW_PANELPAD;
}

/* Widen the count panel (iw_countw) for the n characters of text, drawn
   in dc. */
static void widen(HDC dc, const char *text, int n)
{
    n = (int)LOWORD(GetTextExtent(dc, text, n)) + IW_PANELPAD;
    iw_countw = max(iw_countw, n);
}

/* Make the frame w's tool bar along its top, with the Find box in its
   slot, and its status bar along its bottom, of IW_NPANELS panels, both
   in ANSI_VAR_FONT, the count panel as wide as the widest count string
   for IW_WIDECOUNT or the add mode's for LV_MAXPAGE rows (iw_countw),
   every button grayed and the box disabled until a login: 1, or 0 when
   Windows refuses a window. */
static int chrome(HWND w)
{
    char fmt[IW_FMTSZ];
    char text[IW_PANELSZ];
    TEXTMETRIC tm;
    HFONT font;
    HGDIOBJ old;
    UINT ids;
    RECT r;
    HDC dc;

    GetClientRect(w, &r);
    iw_tool = wtb_create(w, &r, IW_C_TOOL, IW_S_TOOL, tool_cmds);
    GetClientRect(w, &r);
    iw_status = wsb_create(w, &r, IW_C_STATUS, NULL);
    if (iw_tool == NULL || iw_status == NULL)
        return 0;
    font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    dc = GetDC(w);
    old = SelectObject(dc, font);
    GetTextMetrics(dc, &tm);
    for (ids = IW_S_NITEMS; ids <= IW_S_NAUDIT; ids += 2) {
        LoadString(iw_inst, ids, fmt, sizeof fmt);
        widen(dc, text, wsprintf(text, fmt, (DWORD)IW_WIDECOUNT));
    }
    LoadString(iw_inst, IW_S_NADD, fmt, sizeof fmt);
    widen(dc, text, wsprintf(text, fmt, LV_MAXPAGE));
    SelectObject(dc, old);
    ReleaseDC(w, dc);
    iw_cw = (u8)tm.tmAveCharWidth;
    SendMessage(iw_tool, WTBM_SLOT, IW_FINDCH * tm.tmAveCharWidth +
                IW_FINDEDGE, (LPARAM)(RECT FAR *)&r);
    iw_find = CreateWindow(wd_k_edit, k_empty, WS_CHILD | WS_VISIBLE |
                           WS_BORDER | WS_DISABLED | ES_AUTOHSCROLL, r.left,
                           r.top, r.right - r.left, r.bottom - r.top, iw_tool,
                           (HMENU)IW_C_FIND, iw_inst, NULL);
    if (iw_find == NULL)
        return 0;
    SendMessage(iw_find, WM_SETFONT, (WPARAM)font, 0L);
    SendMessage(iw_find, EM_LIMITTEXT, LINK_MAXFIND, 0L);
    tools_on(0);
    return 1;
}

/* The list's room in the frame w, into *r: its client area between the
   tool bar and the status bar. */
static void list_room(HWND w, RECT *r)
{
    RECT s;

    GetClientRect(w, r);
    GetWindowRect(iw_status, &s);
    r->top = WTB_HEIGHT;
    r->bottom -= s.bottom - s.top;
    if (r->bottom < r->top)
        r->bottom = r->top;
}

/* 1 while the split view shows: the screen's pane beside its list. */
static int pane_shown(void)
{
    return iw_splitter != NULL;
}

/* The pane with the focus: 0 the master, 1 the pane beside it. */
static int pane_at(void)
{
    return iw_inview ? 1 : (int)(iw_cur - iw_panes);
}

/* The window of pane i: its list's, or the preview panel. */
static HWND pane_wnd(int i)
{
    return iw_panes[i].list != NULL ? wl_hwnd(iw_panes[i].list) : iw_view;
}

/* Screen s's pane while the settings show it (iw_wpane), or, when they
   leave that to the frame (CFG_PANEAUTO), on a link whose requests are
   fast (iw_exact); else NULL. */
static const ws_pane *pane_of(const ws_scr *s)
{
    u8 on;

    if (s->pane == NULL)
        return NULL;
    on = iw_wpane[s->pane->cfg][CFG_PANE_ON];
    if (on == CFG_PANEAUTO)
        on = iw_exact;
    return on ? s->pane : NULL;
}

/* The master's width in the split view of the frame w whose room for
   the lists is cx pixels wide, d the screen's pane, and the least and
   most a drag of the splitter may give it, into *lo and *hi: the
   settings' average characters (iw_wpane), or without them d's columns
   (ws_pane's wide), at most half the room; each pane at least
   IW_PANEMIN characters, or, in a room too narrow for both, half of it
   each. */
static int split_at(HWND w, const ws_pane *d, int cx, int *lo, int *hi)
{
    int least;
    int x;

    cx -= WSP_WIDTH;
    least = IW_PANEMIN * iw_cw;
    if (cx < 2 * least) {
        *lo = max(cx / 2, 0);
        *hi = *lo;
        return *lo;
    }
    x = iw_wpane[d->cfg][CFG_PANE_W];
    x = x != 0 ? x * iw_cw : min(wl_width(w, d->wide), cx / 2);
    *lo = least;
    *hi = cx - least;
    return max(least, min(x, *hi));
}

/* Pane i's rectangle in the room r for the lists, into *b: the room,
   or in the split view, the master x pixels wide (split_at), the
   master's part left of the splitter or its pane's right of it; x is -1
   without a split. */
static void pane_rect(int i, int x, const RECT *r, RECT *b)
{
    *b = *r;
    if (x < 0)
        return;
    if (i == 0)
        b->right = x;
    else
        b->left = x + WSP_WIDTH;
}

/* Fit the tool bar, the status bar and the lists between them to the
   frame w, the splitter between the split view's panes, held to its
   room (WSPM_LIMITS), the preview panel in a second list's place.
   When the rows that fit are no longer a list's,
   the main loop opens the screen again (iw_fit), woken by a posted
   WM_NULL: a size another program sets comes in a sent message, which
   GetMessage handles without returning. */
static void place(HWND w)
{
    const iw_pane *p;
    RECT r;
    RECT s;
    int lo;
    int hi;
    int x;

    GetClientRect(w, &r);
    GetWindowRect(iw_status, &s);
    MoveWindow(iw_tool, 0, 0, r.right, WTB_HEIGHT, TRUE);
    MoveWindow(iw_status, 0, r.bottom - (s.bottom - s.top), r.right,
               s.bottom - s.top, TRUE);
    list_room(w, &r);
    x = -1;
    if (pane_shown()) {
        x = split_at(w, iw_panes[0].scr->pane, r.right, &lo, &hi);
        MoveWindow(iw_splitter, x, r.top, WSP_WIDTH, r.bottom - r.top, TRUE);
        SendMessage(iw_splitter, WSPM_LIMITS, (WPARAM)lo, MAKELPARAM(hi, 0));
    }
    for (p = iw_panes; p < iw_panes + IW_NPANES; p++) {
        if (p->list == NULL)
            continue;
        pane_rect((int)(p - iw_panes), x, &r, &s);
        MoveWindow(wl_hwnd(p->list), s.left, s.top, s.right - s.left,
                   s.bottom - s.top, TRUE);
        if (wl_rows(w, s.bottom - s.top, s.right - s.left, p->cols) !=
            p->ps && !iw_fit) {
            iw_fit = 1;
            PostMessage(w, WM_NULL, 0, 0L);
        }
    }
    if (iw_view != NULL) {
        pane_rect(1, x, &r, &s);
        MoveWindow(iw_view, s.left, s.top, s.right - s.left,
                   s.bottom - s.top, TRUE);
    }
}

/* ---------------------------------------------------------------------
 * The screens.
 * --------------------------------------------------------------------- */

/* The item screen's format function: the row in the columns of the
   order the list has in use.  The pager formats rows only between
   WL_BUSY 1 and 0, while the frame refuses commands and WM_CLOSE
   (iw_busy), so iw_move is the pane whose move this is, which need not
   be the focused one: the pane beside the master loads while the master
   has the focus. */
static void item_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_item(r, wl_order(iw_move->list), &iw_cats, text, key);
}

/* The parts screen's format function. */
static void part_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_part(r, &iw_cats, text, key);
}

/* 1 when the rows of screen s, items or parts, name categories. */
static int has_cats(const ws_scr *s)
{
    return s->fmt == item_fmt || s->fmt == part_fmt;
}

/* The item pick list's format function, its rows in the first order. */
static void pick_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_item(r, 0, &iw_cats, text, key);
}

/* Load the categories held (iw_cats) with one CAT LIST, w busy while it
   runs: 1, or 0 when it failed, which drops them, so the rows show their
   codes alone (the list's own request then reports the failure). */
static int cats_load(HWND w)
{
    rq_res res;
    int rc;

    busy(w, 1);
    rc = ct_load(&iw_cats, iw_l, wp_tables[SCR_CK_CAT], &res);
    busy(w, 0);
    if (rc != RQ_OK)
        ct_close(&iw_cats);
    return rc == RQ_OK;
}

/* Hold the categories for the rows of items (cats_load): 1 when this
   call loaded them, 0 when they were held already or did not load. */
static int cats_hold(HWND w)
{
    return iw_cats.b == NULL && ct_open(&iw_cats) && cats_load(w);
}

/* Load the categories again when they are held, after the categories'
   list, an import or a restore may have changed them. */
static void cats_fresh(void)
{
    if (iw_cats.b != NULL)
        cats_load(iw_frame);
}

/* A scoped screen's args when its source has none: the scope's key,
   arg. */
static void scope_args(void *arg, rq *q)
{
    rq_word(q, (const char *)arg);
}

void ws_form_busy(void *arg, HWND dlg, int on)
{
    (void)arg;
    busy(dlg != NULL ? dlg : iw_frame, on);
}

link *ws_link(void)
{
    return iw_l;
}

ctab *ws_cats(void)
{
    return &iw_cats;
}

void ws_info(const char *text)
{
    char t[MSG_MAXLEN + 1];

    t[0] = '\0';
    if (text != NULL) {
        txt_copy(t, text, (u16)sizeof t);
        OemToAnsi(t, t);
    }
    panel(IW_P_HELP, t);
    UpdateWindow(iw_status);
}

/* The fields naming an item after its ID (wf_ops' names). */
static const char *const item_names[] = { itl_k_mfr, itl_k_model, NULL };

/* After ITEM GET has loaded item form f (ws_scr's loaded): as
   scr_item_loaded, then, for an installed item, its parent's maker and
   model in the Parent field after the ID, as INV shows them, read with
   ASSY PATH from the frame (scr_item_parent); when that fails, the
   failure is said over the frame (ws_fail) and the ID is left alone,
   any action of the failure left to the form's next request. */
static void item_loaded(form *f)
{
    rq q;
    rq_res res;

    scr_item_loaded(f);
    if (form_get(f, scr_item_fields[SCR_IF_PARENT].name)[0] == '\0')
        return;
    rq_begin(&q, "ASSY PATH");
    rq_word(&q, form_get(f, itl_k_itemid));
    if (ws_send(iw_frame, &q, scr_item_parent, f, &res) != RQ_OK)
        ws_fail(iw_frame, &res);
}

/* The item form's part, its pages and record buttons wrow_item_pages,
   the buttons opening item_scopes. */
static const wf_ops item_ops = {
    w_item, itl_k_itemid, MI_TITLE, ws_form_busy, mio_load, mio_save, NULL,
    NULL, NULL, &wrow_item_pages, 0, item_names, &scr_item_loc
};

/* The notes of the item screens (ws_scr's notes). */
static const u16 item_notes[] = { MI_ADDED, MI_SAVED, MI_DELETED };

/* The item screen: ITEM LIST through itl_src (ITEMLS.H), its records in
   the item form of SCRIFORM.H, read by scr_item_kline, with the commands
   of WASSY.H, and beside it the preview of the selected item
   (wpane_item of WPANE.H). */
static const ws_scr item_scr = {
    ws_k_items, &itl_src, item_fmt, wrow_item_cols, MI_T_ID, &scr_item_def,
    &item_ops, w_get, w_del, MI_DELQ, scr_item_preset, item_loaded,
    WS_ITEMS, NULL, NULL, wassy_item_acts, NULL, scr_item_kline, item_notes,
    NULL, NULL, &wpane_item
};

/* The search's results (WSRCH.H), titled by the search: items, as the
   item screen's, with its preview, a new one taking a filter's
   location. */
static const ws_scr found_scr = {
    ws_k_items, &wsrch_src, item_fmt, wrow_item_cols, 0, &scr_item_def,
    &item_ops, w_get, w_del, MI_DELQ, wsrch_preset, item_loaded,
    WS_ITEMS | WS_FILTER, NULL, NULL, wassy_item_acts, NULL, scr_item_kline,
    item_notes, wsrch_title, NULL, &wpane_item
};

const ws_scr ws_contents = {
    "CONTENTS", &wloc_csrc, item_fmt, wrow_item_cols, 0, &scr_item_def,
    &item_ops, w_get, w_del, MI_DELQ, wloc_cpreset, item_loaded,
    WS_ITEMS | WS_FILTER, NULL, NULL, wassy_item_acts, NULL, scr_item_kline,
    item_notes, wloc_ctitle
};

/* The assemblies, ASSY LIST TOP=1 through wassy_src (WASSY.H): items,
   as the item screen's, and beside them the parts of the selected one
   (wpane_assy of WPANE.H). */
static const ws_scr assy_scr = {
    ws_k_items, &wassy_src, item_fmt, wrow_item_cols, MA_T_LIST, &scr_item_def,
    &item_ops, w_get, w_del, MI_DELQ, scr_item_preset, item_loaded,
    WS_ITEMS | WS_FILTER, NULL, NULL, wassy_item_acts, NULL, scr_item_kline,
    item_notes, NULL, NULL, &wpane_assy
};

/* An item's parts, scoped: ASSY BOM through scr_assy_src (SCRAFORM.H),
   each part in the item form, with the parts' commands of WASSY.H in
   place of a new record and a delete. */
const ws_scr ws_parts = {
    ws_k_parts, &scr_assy_src, part_fmt, &wrow_part_cols, MA_T_ITEM,
    &scr_item_def, &item_ops, w_get, NULL, 0, NULL, item_loaded,
    WS_SCOPED | WS_NONEW | WS_FILTER, NULL, NULL, wassy_part_acts, NULL,
    scr_item_kline, item_notes
};

/* The categories' format function (WROWS.H). */
static void cat_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_code(r, text, key);
}

/* The categories' source: CAT LIST, which answers with its whole list
   and takes no PAGE= or AFTER=, so its list pages the response itself
   (wl_unpaged, list_make); its columns' table gives the header, and
   without a category, which every item has, there are no items to
   browse (MI_EMPTY). */
static const lv_src cat_src = {
    "CAT LIST", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 },
    MI_EMPTY, NULL, NULL, NULL, NULL, NULL
};

/* The categories, Items > Browse by category, titled ME_CATS: each
   one's code, name and sort order, and beside them the selected one's
   items (wpane_cat of WPANE.H).  It names no form and no delete: ENTER
   opens the category's items as a screen of their own (ws_catitems),
   and Edit, New and Delete are the Categories dialog's (cat_key). */
static const ws_scr cat_scr = {
    k_bycat, &cat_src, cat_fmt, &wrow_code_cols, ME_CATS, NULL, NULL, NULL,
    NULL, 0, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    &wpane_cat
};

/* A category's items' args, in place of the scope's key as a word
   (pane_open): that key, arg, as the search's category filter. */
static void cat_args(void *arg, rq *q)
{
    rq_pair(q, scr_search_fields[SCR_SF_CAT].name, (const char *)arg);
}

/* A category's items' title (ws_scr's name): ME_FCAT with the category
   of the first pane's scope as the rows of items name it (wrow_cat). */
static const char *cat_title(void)
{
    char c[WROW_CATW + 1];

    strcpy(c, iw_panes[0].scope);
    wrow_cat(&iw_cats, c);
    return msg_sub(ME_FCAT, c, NULL);
}

const ws_scr ws_catitems = {
    k_bycat, &itl_src, item_fmt, wrow_item_cols, 0, &scr_item_def,
    &item_ops, w_get, w_del, MI_DELQ, scr_item_preset, item_loaded,
    WS_SCOPED | WS_ITEMS | WS_FILTER, itl_k_cat, NULL, wassy_item_acts, NULL,
    scr_item_kline, item_notes, cat_title
};

/* The screens whose rows are not items, and the count string of each
   (status). */
static const ws_scr *const noun_scrs[IW_NNOUNS] = {
    &assy_scr, &ws_parts, &wloc_scr, &wsvc_recent, &wsvc_item,
    &wloan_list, &wloan_item, &wloan_host, &wuser_scr, &wadm_audit, &cat_scr
};
static const u8 noun_ids[IW_NNOUNS] = {
    IW_S_NASSY, IW_S_NPARTS, IW_S_NLOCS, IW_S_NSVC, IW_S_NSVC, IW_S_NHOLDS,
    IW_S_NHOLDS, IW_S_NHOLDS, IW_S_NUSERS, IW_S_NAUDIT, IW_S_NCATS
};

/* Show the connection and its target on the status bar, the user and
   level while one is logged in, their panels as wide as their texts
   (fit), and the rows chosen in the focused list while it has several
   (IW_S_NSEL), all of them (IW_S_NALL) or the add mode (IW_S_NADD), else
   its rows while it has counted them, on a link whose lists count
   (iw_exact, wl_count), in the noun of its screen, or for a pane beside
   the master that has one in its own string with what it shows
   (ws_pane's count and name), singular for one. */
static void status(void)
{
    int widths[IW_NPANELS - 1];
    char fmt[IW_FMTSZ];
    char user[LINK_USERLEN + 1];
    char name[LINK_LOCNAMELEN + 1];
    char text[IW_PANELSZ];
    HGDIOBJ old;
    UINT ids;
    u32 rows;
    HDC dc;
    int i;
    u16 n;

    dc = GetDC(iw_status);
    old = SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    LoadString(iw_inst, iw_kind, fmt, sizeof fmt);
    wsprintf(text, fmt, (LPCSTR)iw_target);
    widths[IW_P_LINK] = fit(dc, IW_P_LINK, text);
    text[0] = '\0';
    if (iw_level != 0) {
        LoadString(iw_inst, IW_S_USER, fmt, sizeof fmt);
        OemToAnsi(iw_user, user);
        wsprintf(text, fmt, (LPCSTR)user, (int)iw_level);
    }
    widths[IW_P_USER] = fit(dc, IW_P_USER, text);
    SelectObject(dc, old);
    ReleaseDC(iw_status, dc);
    widths[IW_P_COUNT] = iw_countw;
    SendMessage(iw_status, WSBM_PANELS, IW_NPANELS,
                (LPARAM)(const int FAR *)widths);
    text[0] = '\0';
    n = iw_cur->list != NULL ? wl_nchosen(iw_cur->list) : 0;
    ids = n == 0 ? 0 : wl_all(iw_cur->list) ? IW_S_NALL :
          wl_adding(iw_cur->list) ? IW_S_NADD : n > 1 ? IW_S_NSEL : 0;
    if (ids != 0) {
        LoadString(iw_inst, ids, fmt, sizeof fmt);
        wsprintf(text, fmt, (int)n);
    } else if (iw_exact && iw_cur->list != NULL &&
               wl_count(iw_cur->list, &rows)) {
        name[0] = '\0';
        ids = iw_cur != iw_panes ? iw_panes[0].scr->pane->count : 0;
        if (ids != 0) {
            txt_copy(name, iw_panes[0].scr->pane->name(), (u16)sizeof name);
            OemToAnsi(name, name);
        } else {
            for (i = 0; i < IW_NNOUNS && noun_scrs[i] != iw_cur->scr; i++)
                ;
            ids = i < IW_NNOUNS ? noun_ids[i] : IW_S_NITEMS;
        }
        LoadString(iw_inst, ids + (rows == 1), fmt, sizeof fmt);
        wsprintf(text, fmt, (DWORD)rows, (LPCSTR)name);
    }
    panel(IW_P_COUNT, text);
}

/* Show the focused list's count on the status bar once a move is over
   (iw_moved): after the main loop has dispatched a message, and as a
   frame's move returns, before a dialog that may follow it. */
static void moved(void)
{
    if (iw_moved)
        status();
    iw_moved = 0;
}

/* The preview panel beside the master no longer shows its row's record
   as it is, a command having changed it or a refresh being asked for:
   its next load reads the record again (pane_load). */
static void pane_stale(void)
{
    if (iw_view != NULL)
        iw_shown[0] = '\0';
}

static int pane_row(char *key, char *text);
static void pane_focus(int i);

/* 1 while the master's row (pane_row) is the row of key. */
static int master_on(const char *key)
{
    char row[LV_KEYSZ];

    return pane_row(row, NULL) && strcmp(row, key) == 0;
}

/* After a fetch of the master by row number, which may leave it on
   another record when a row came or went above its own: the row of key
   want sought (wl_find), and, when that row is gone, the row the fetch
   chose again, but after a seek that failed, which the list has said,
   so that one failure is said once.  A master then not on the row its
   pane shows (iw_shown) takes the focus from the pane, so that no key
   meant for the pane acts on a record the pane only then follows. */
static void master_keep(const char *want)
{
    char key[LV_KEYSZ];
    wl *m;

    m = iw_panes[0].list;
    if (pane_row(key, NULL) && strcmp(key, want) != 0 &&
        wl_find(m, want) == PG_OK && !master_on(want))
        wl_find(m, key);
    if (iw_cur != iw_panes && !master_on(iw_shown))
        pane_focus(0);
}

/* After a record command in one list of the split view, which may have
   changed the other's rows too, as a part taken out may leave no
   assembly and an assembly moved moves its parts: the other fetched
   again and counted (wl_recount); but beside the locations (WS_HOLDS),
   where no command of one changes the other's rows.  After the master's
   command that is the pane, while the master's row is still the one it
   shows, as another row is followed in its turn (settle).  After the
   pane's it is the master, kept on the row its pane shows, so that the
   pane, which keeps the focus, stays that record's (master_keep); but
   the categories, which no command on an item changes. */
static void other_fresh(void)
{
    wl *m;

    if (iw_panes[1].list == NULL || (iw_panes[0].scr->flags & WS_HOLDS))
        return;
    m = iw_panes[0].list;
    if (iw_cur == iw_panes) {
        if (master_on(iw_shown)) {
            wl_recount(iw_panes[1].list);
            wl_move(iw_panes[1].list, PG_RELOAD);
        }
    } else if (iw_panes[0].scr != &cat_scr) {
        wl_recount(m);
        if (wl_move(m, PG_RELOAD) == PG_OK)
            master_keep(iw_shown);
    }
}

/* After a record command, the focused list, whose command it was,
   fetched again, keeping the selection on its row number (PG_RELOAD),
   or, on a tree (WS_TREE), with key not NULL, from its start with
   record key selected (wl_find), and then, when that fetch succeeded,
   the list beside it in the split view (other_fresh), when act is
   RQE_STAY, else left as act says; a preview panel's record is read
   again (pane_stale). */
static void after_key(u8 act, const char *key)
{
    int rc;

    pane_stale();
    if (act != RQE_STAY) {
        leave(act);
    } else if (iw_cur->list == NULL) {
        return;
    } else {
        rc = key != NULL && (iw_cur->scr->flags & WS_TREE) ?
             wl_find(iw_cur->list, key) : wl_move(iw_cur->list, PG_RELOAD);
        if (rc == PG_OK)
            other_fresh();
    }
    moved();
}

/* after_key with no record to seek. */
static void after(u8 act)
{
    after_key(act, NULL);
}

/* After a command of the menu bar that may change any list (Backup,
   Restore, Pack, Reindex, Export CSV, Import CSV, a code list): every
   list shown fetched again (PG_RELOAD), but a pane's that follows no
   row yet, disabled, which has no scope to fetch, a preview panel's
   record with its master (pane_stale), when act is RQE_STAY, else left
   as act says; 1 when act is RQE_STAY and no fetch failed. */
static int after_all(u8 act)
{
    iw_pane *p;
    int ok;

    pane_stale();
    ok = act == RQE_STAY;
    if (!ok)
        leave(act);
    else
        for (p = iw_panes; p < iw_panes + IW_NPANES; p++)
            if (p->list != NULL && IsWindowEnabled(wl_hwnd(p->list)) &&
                wl_move(p->list, PG_RELOAD) != PG_OK)
                ok = 0;
    moved();
    return ok;
}

/* Before the move that shows a change the frame made to a record of
   screen s, n the rows it adds (1, 0 or -1): the focused list counted
   again by that move when its screen shows some of its records only
   (WS_FILTER, wl_recount), else, when it is s's, n rows more
   (wl_count_add). */
static void count_rows(const ws_scr *s, int n)
{
    if (iw_cur->list == NULL)
        return;
    if (iw_cur->scr->flags & WS_FILTER)
        wl_recount(iw_cur->list);
    else if (s == iw_cur->scr && n != 0)
        wl_count_add(iw_cur->list, n);
}

/* Have every list shown counted again by its next move, after a restore
   or an import has changed what any list holds (wl_recount), and the
   categories held loaded again. */
static void recount(void)
{
    iw_pane *p;

    for (p = iw_panes; p < iw_panes + IW_NPANES; p++)
        if (p->list != NULL)
            wl_recount(p->list);
    cats_fresh();
}

/* 1 when the user may change records (TUI_LVL_OPER), else 0 with INV's
   refusal said (rqe_deny). */
static int allow(void)
{
    char text[MSG_MAXLEN + 1];
    const char *no;

    no = rqe_deny(iw_level, TUI_LVL_OPER);
    if (no == NULL)
        return 1;
    OemToAnsi(no, text);
    ws_say(iw_frame, MB_ICONEXCLAMATION, text);
    return 0;
}

/* Show the note m (MSG.H), ^1 a and ^2 b, on the status bar's help
   panel until the next key or mouse button is pressed (run's loop), as
   INV shows a note on its hint row; m 0 shows none. */
static void note(u16 m, const char *a, const char *b)
{
    if (m != 0) {
        ws_info(msg_sub(m, a, b));
        iw_noted = 1;
    }
}

/* Show text formatted from string ids with the numbers i and n, the
   first alone for a string of one, on the help panel, as a note (note)
   when noted, at once (UpdateWindow). */
static void help_n(UINT ids, u16 i, u16 n, int noted)
{
    char fmt[IW_FMTSZ];
    char t[IW_FMTSZ + 2 * TXT_U32LEN];

    LoadString(iw_inst, ids, fmt, sizeof fmt);
    wsprintf(t, fmt, (int)i, (int)n);
    panel(IW_P_HELP, t);
    UpdateWindow(iw_status);
    iw_noted = (u8)noted;
}

/* Carry out fn, with arg, for each row chosen in the focused list
   (wl_chosen), every row of the list when all are chosen, from row from,
   0 the first, to row n - 1, in one busy bracket, the help panel
   counting them (IW_G_DOING plus label, the string of the command's
   Record entry); an Esc typed meanwhile, found between two rows (the
   wait of a list's move, wl_wait, for which the yield keeps one typed
   during a row's request), or the first failure stops it, said in a box
   over the frame (IW_S_HALTED; the failure's text, as ws_fail gives it,
   and IW_S_STOPPED); the program's end (WL_QUIT) stops it unsaid.  A key
   script's A line is typed ahead of it (td_ahead), its Esc found at the
   first wait.  The rows made, those before from included; *act the
   failure's action, else RQE_STAY. */
static u16 each(UINT label, iw_row_fn fn, void *arg, u16 from, u16 n,
                u8 *act)
{
    char key[LV_KEYSZ];
    char fmt[IW_FMTSZ];
    char t[RQE_TEXTLEN + IW_ROWSSZ];
    rq_res res;
    rqe e;
    int stop;
    int rc;
    u16 i;

    *act = RQE_STAY;
    rc = RQ_OK;
    stop = 0;
    td_ahead();
    busy(iw_frame, 1);
    for (i = from; i < n; i++) {
        if (i > 0 && (stop = wl_wait()) != 0)
            break;
        help_n(IW_G_DOING + label, (u16)(i + 1), n, 0);
        wl_chosen(iw_cur->list, i, key, NULL);
        rc = fn(arg, key, &res);
        if (rc != RQ_OK)
            break;
    }
    busy(iw_frame, 0);
    panel(IW_P_HELP, NULL);
    if (rc != RQ_OK) {
        rqe_class(&e, &res, iw_level);
        OemToAnsi(e.text, e.text);
        OemToAnsi(key, key);
        LoadString(iw_inst, IW_S_STOPPED, fmt, sizeof fmt);
        wsprintf(t, fmt, (LPCSTR)e.text, (LPCSTR)key, (int)i, (int)n);
        ws_say(iw_frame, WD_FAILICON(e.show), t);
        *act = e.act;
    } else if (i < n && stop != WL_QUIT) {
        LoadString(iw_inst, IW_S_HALTED, fmt, sizeof fmt);
        wsprintf(t, fmt, (int)i, (int)n);
        ws_say(iw_frame, MB_ICONEXCLAMATION, t);
    }
    return i;
}

/* After a command on several rows of the string label made each other
   row of n (each), that made k of them: its note when all are made
   (IW_G_DONE plus label), the focused list's rows counted (count_rows of
   screen s, d rows each), then it fetched again once (after). */
static void each_end(UINT label, u16 k, u16 n, const ws_scr *s, int d,
                     u8 act)
{
    if (k == n)
        help_n(IW_G_DONE + label, n, n, 1);
    count_rows(s, d * (int)k);
    after(act);
}

/* Screen s's note i (WS_N_), or 0 when it has none. */
static u16 note_of(const ws_scr *s, int i)
{
    return s->notes != NULL ? s->notes[i] : 0;
}

/* The action the end r of a form dialog (wf_run) leads to, RQE_STAY but
   after a failure that leaves; a dialog that could not be made is
   said. */
static u8 ended(int r)
{
    if (r == WF_EFAIL)
        fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
    return (u8)(r == WF_LOGIN ? RQE_LOGIN : r == WF_QUIT ? RQE_QUIT :
                r == WF_LOST ? RQE_LOST : RQE_STAY);
}

/* 1 when the dialog of f, a record copy says (rec_run), ended r so that
   the record, a copy just made, is to be deleted again: cancelled before
   its OK sent anything (wf_sent), which might have saved some of it. */
static int uncopy(int r, u8 copy, form *f)
{
    return r == WF_CANCEL && copy == IW_COPY && !wf_sent(f);
}

/* Run the form dialog of f, a record of screen s opened in mode whose
   key is id (LV_KEYSZ bytes), with ops, what copy says (IW_REC, IW_COPY
   or IW_COPYKEPT); its end (wf_run).  A dialog that saved shows the
   screen's note of a record added or saved, ^2 its name, or on a screen
   of kinds its caption; a copy cancelled, or whose dialog could not be
   made, MI_COPIED, but one to be deleted (uncopy). */
static int rec_run(const ws_scr *s, const wf_ops *ops, form *f, char *id,
                   u8 mode, u8 copy)
{
    char name[MSG_MAXLEN + 1];
    int r;

    r = wf_run(f, iw_frame, iw_l, iw_level, ops, NULL, id, LV_KEYSZ);
    if (s->kinds != NULL)
        txt_copy(name, msg_sub(ops->title, id, NULL), (u16)sizeof name);
    else
        wf_name(f, ops, id, name);
    if (r == WF_OK)
        note(note_of(s, mode == FORM_NEW ? WS_N_ADDED : WS_N_SAVED), id,
             name);
    else if (copy != IW_REC && (r == WF_CANCEL || r == WF_EFAIL) &&
             !uncopy(r, copy, f))
        note(MI_COPIED, id, name);
    return r;
}

/* The request of command a for record key with the values of f, sent
   over l: the command's own; else the command with key as its word and
   each value of f that is not empty, but a local field's, as a pair. */
static int act_req(const ws_act *a, form *f, link *l, const char *key,
                   rq_res *res)
{
    const char *v;
    rq q;
    int i;

    if (a->send != NULL)
        return a->send(l, a->cmd, key, f, res);
    rq_begin(&q, a->cmd);
    rq_word(&q, key);
    for (i = 0; i < a->def->nfields; i++) {
        v = form_get(f, a->def->fields[i].name);
        if (v[0] != '\0' && !(a->def->fields[i].flags & FF_LOCAL))
            rq_pair(&q, a->def->fields[i].name, v);
    }
    return rq_send(l, &q, NULL, NULL, res);
}

/* The request of the form dialog of command arg, an iw_act, for its row
   (wf_ops' send; id is the caption's): none, for WA_KEEP, while f holds
   the values it started with; else act_req's, the ID of its +OK into the
   iw_act. */
static int act_send(void *arg, form *f, link *l, const char *id,
                    rq_res *res)
{
    iw_act *x;
    int rc;

    (void)id;
    x = (iw_act *)arg;
    if ((x->a->flags & WA_KEEP) && !form_dirty(f)) {
        x->same = 1;
        return RQ_OK;
    }
    rc = act_req(x->a, f, l, x->key, res);
    if (rc == RQ_OK)
        rq_okval(res, itl_k_id, x->id, (u16)sizeof x->id);
    return rc;
}

/* iw_row_fn of a command asked once for several rows, arg its iw_act:
   act_req for row key with the values asked, whether or not the row is
   where they say already (WA_KEEP). */
static int act_row(void *arg, const char *key, rq_res *res)
{
    const iw_act *x;

    x = (const iw_act *)arg;
    return act_req(x->a, x->f, iw_l, key, res);
}

static void list_open(const ws_scr *s, const char *order, const char *sel);
static void list_seek(iw_pane *p, const char *sel);
static void pane_load(int open);
static void pane_toggle(void);
static void contents_again(void);
static void tree_open(const char *sel);

/* The scoped screens of an item, in the order of WS_K_PARTS to
   WS_K_USED: its parts, its service records, its loans and holds and
   what is used with it, which the item form's record buttons open, as
   INV's Parts, Service and Loans tabs do, and the Record menu on a
   screen of items (WS_ITEMS) for the selected item. */
static const ws_scr *const item_scopes[IW_NACTS] = {
    &ws_parts, &wsvc_item, &wloan_item, &wloan_host
};

/* A record button of the item form for each scoped screen. */
extern char iw_scopes_fit[1];
extern char iw_scopes_fit[IW_NACTS == WROW_IACTS ? 1 : 2];

/* Keep the screen shown as the one ESC goes back to (back), with its
   list's row, the one its pane shows (iw_shown) while the pane's list has
   the focus, the pane that has the focus and the item form to show again
   over it in mode, of the record copy says (IW_REC or IW_COPYKEPT), or
   mode IW_NOFORM for none.  The oldest screen to go back to is dropped to
   make room for the newest. */
static void push(u8 mode, u8 copy)
{
    iw_back *b;

    if (iw_nback == IW_NBACK) {
        memmove(iw_backs, iw_backs + 1, sizeof iw_backs - sizeof *iw_backs);
        iw_nback--;
    }
    b = &iw_backs[iw_nback++];
    b->scr = iw_panes[0].scr;
    b->order = wl_order(iw_panes[0].list);
    strcpy(b->scope, iw_panes[0].scope);
    if (iw_cur != iw_panes)
        strcpy(b->sel, iw_shown);
    else
        wl_selected(iw_panes[0].list, b->sel);
    b->mode = mode;
    b->copy = copy;
    b->pane = (u8)pane_at();
}

/* Open the scoped screen s for item key, named name (WSCR.H), from the
   screen shown, which ESC goes back to (push), and then to the item form
   of key in mode, of the record copy says, when it was opened from
   there, else mode IW_NOFORM. */
static void scope_open(const ws_scr *s, const char *key, const char *name,
                       u8 mode, u8 copy)
{
    push(mode, copy);
    txt_copy(iw_panes[0].scope, key, (u16)sizeof iw_panes[0].scope);
    txt_copy(iw_sname, name, (u16)sizeof iw_sname);
    list_open(s, NULL, NULL);
}

/* Open the list of s, the user list or the audit log, which ESC leaves
   for the list shown before it (push), or for the item screen when none
   was; chosen on s itself, it keeps the screen it had to go back to. */
static void admin_open(const ws_scr *s)
{
    if (iw_panes[0].list == NULL)
        iw_nback = 0;
    else if (iw_panes[0].scr != s)
        push(IW_NOFORM, IW_REC);
    list_open(s, NULL, NULL);
}

/* The item pick list of an asking form's item field over dlg (wf_ops'
   pick) of the items only names (SCR_SI_ of SCRSFORM.H), titled MI_PICK,
   or MI_PICKAVL with the empty text MI_NOAVAIL for the available items
   alone (SCR_SI_AVAIL), their rows in wrow_ipick_cols (wp_list), the
   categories held for it when no list of items holds them (cats_hold):
   every item when nothing is typed, or when the digits typed name an
   item, read with ITEM GET, that one selected, and with find 1 that item
   taken at once, its ID without leading zeros (scr_search_id) in code
   and no list shown; else the items holding the text typed, digits that
   name no item among them, as models and serial numbers may be all
   digits (scr_search_item, as INV's scr_ask_item).  A failed ITEM GET
   but 200, or 102 for an ID of 0, is shown over dlg (ws_fail), *act its
   action, and gives WP_NONE. */
static int pick_items(HWND dlg, const char *typed, int find, char *code,
                      u8 *act, u8 only, link *l, u8 level)
{
    char id[LINK_IDLEN + 1];
    const char *p;
    scr_query q;
    lv_src src;
    rq r;
    rq_res res;
    int held;
    int rc;

    id[0] = '\0';
    if (scr_search_item(&q, typed, only)) {
        p = scr_search_id(&q);
        rq_begin(&r, w_get);
        rq_word(&r, p);
        rc = ws_send(dlg, &r, NULL, NULL, &res);
        if (rc == RQ_OK) {
            txt_copy(id, p, (u16)sizeof id);
            if (find) {
                txt_copy(code, id, WP_CODESZ);
                *act = RQE_STAY;
                return WP_OK;
            }
            scr_search_item(&q, k_empty, only);
        } else if (rc != RQ_ENOTFOUND && rc != RQ_EARG) {
            *act = ws_fail(dlg, &res);
            return WP_NONE;
        }
    }
    scr_search_src(&src, &q);
    src.fmt = pick_fmt;
    held = cats_hold(dlg);
    rc = wp_list(dlg, l, level,
                 msg(only == SCR_SI_AVAIL ? MI_PICKAVL : MI_PICK), &src,
                 &wrow_ipick_cols, id, ws_form_busy, NULL, code, act);
    if (held)
        ct_close(&iw_cats);
    return rc;
}

/* pick_items of every item, an item installed or installed in (ws_act's
   asking forms). */
static int pick_any(void *arg, HWND dlg, link *l, u8 level,
                    const char *typed, int find, char *code, u8 *act)
{
    (void)arg;
    return pick_items(dlg, typed, find, code, act, SCR_SI_ALL, l, level);
}

int ws_pick_item(void *arg, HWND dlg, link *l, u8 level, const char *typed,
                 int find, char *code, u8 *act)
{
    (void)arg;
    return pick_items(dlg, typed, find, code, act, SCR_SI_ACTIVE, l, level);
}

/* pick_items of the available items, the item of a new loan (ws_scr's
   ask). */
static int pick_avail(void *arg, HWND dlg, link *l, u8 level,
                      const char *typed, int find, char *code, u8 *act)
{
    (void)arg;
    return pick_items(dlg, typed, find, code, act, SCR_SI_AVAIL, l, level);
}

/* Ask for the values of def in the form dialog, captioned with def's
   title, key as ^1, the location field of x's command loaded with loc
   when it is not NULL, an item field's pick list of every item
   (pick_any), checking them; OK then sends the command of x for its row
   from the dialog (act_send).  x NULL asks for the item of a new record
   of a screen (ws_scr's ask), picked among the available ones
   (pick_avail), which OK finds there, sending nothing.  The dialog's end
   (wf_run), or WF_EFAIL when the form could not be made; after WF_OK,
   out not NULL, *out holds the form for the caller to close. */
static int ask(const form_def *def, iw_act *x, const char *key,
               const char *loc, form **out)
{
    char id[IW_ROWSSZ];
    wf_ops ops;
    form *f;
    int r;

    r = wf_open(&f, def, FORM_EDIT);
    if (r != 0)
        return r;
    memset(&ops, 0, sizeof ops);
    ops.title = def->title;
    ops.busy = ws_form_busy;
    ops.pick = pick_avail;
    if (x != NULL) {
        ops.send = act_send;
        ops.pick = pick_any;
        ops.loc = x->a->loc;
        if (loc != NULL)
            form_load(f, x->a->loc->loc->name, loc);
    }
    txt_copy(id, key, (u16)sizeof id);
    r = wf_run(f, iw_frame, iw_l, iw_level, &ops, x, id, (u16)sizeof id);
    if (r == WF_OK && out != NULL)
        *out = f;
    else
        form_close(f);
    return r;
}

/* Read record key of screen s into f with its get command, from the
   frame; RQ_OK, or the failure's code, shown, with *act its action
   (request), or with act NULL shown by no one. */
static int rec_get(const ws_scr *s, const char *key, form *f, u8 *act)
{
    rq q;
    rq_res res;
    rq_fn fn;

    rq_begin(&q, s->get);
    rq_word(&q, key);
    fn = s->kline != NULL ? s->kline : form_kline;
    if (act == NULL)
        return ws_send(iw_frame, &q, fn, f, &res);
    return request(&q, fn, f, &res, act);
}

/* The name (WSCR.H) of record key of screen s into t (WF_NAMESZ
   bytes), read into a form of its own (rec_get, wf_name), or the key
   alone, with no request, when its form has no names: 1, or 0 once a
   form that could not be made has been said, or a failed read shown and
   gone on from as it says (after). */
static int rec_name(const ws_scr *s, const char *key, char *t)
{
    form *f;
    u8 act;
    int rc;

    if (s->ops->names == NULL) {
        txt_copy(t, key, WF_NAMESZ);
        return 1;
    }
    if (wf_open(&f, s->def, FORM_VIEW) != 0) {
        fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
        return 0;
    }
    rc = rec_get(s, key, f, &act);
    if (rc == RQ_OK)
        wf_name(f, s->ops, key, t);
    form_close(f);
    if (rc != RQ_OK)
        after(act);
    return rc == RQ_OK;
}

/* A new record's first values in f, of screen s, given the scope's key,
   of kind on a screen of kinds, as its form starts (rec_show), but those
   its loaded function reads. */
static void new_values(const ws_scr *s, form *f, const char *key, u8 kind)
{
    if (s->scope != NULL)
        form_load(f, s->scope, key);
    if (s->kinds != NULL) {
        s->kinds->preset(f, kind);
        if (s->kinds->host != NULL)
            form_put(f, s->kinds->host, iw_cur->scope);
    } else if (s->preset != NULL) {
        s->preset(f);
    }
}

/* Open *f in mode for a record of screen s, of kind on a screen of
   kinds, its dialog's part into *ops, its form into *def and fields
   (FORM_MAXFIELDS of them): the kind's form, captioned for it, else the
   screen's, without its first field for a new record of WS_NEWREST; 1,
   or 0 once IW_S_NOFORM has been said. */
static int form_for(const ws_scr *s, u8 mode, u8 kind, wf_ops *ops,
                    form_def *def, form_field *fields, form **f)
{
    /* Copied, not assigned: Watcom C 6.5 refuses to assign a const
       struct. */
    memcpy(ops, s->ops, sizeof *ops);
    if (s->kinds != NULL) {
        s->kinds->form(kind, def, fields);
        ops->title = (u16)(ops->title + kind);
    } else {
        memcpy(def, s->def, sizeof *def);
    }
    if (mode == FORM_NEW && (s->flags & WS_NEWREST)) {
        def->fields++;
        def->nfields--;
    }
    if (wf_open(f, def, mode) == 0)
        return 1;
    fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
    return 0;
}

/* Show record key of screen s, read with its get command, in the form
   dialog in mode, or in FORM_NEW a new record with key the scope's key and
   then its first values, of the record copy says (rec_run), then go on as
   the dialog, a failed read or a form not made says (after_key, a tree of s
   seeking the record saved), the list left as it was, its rows chosen kept,
   after a dialog cancelled having sent nothing, or, after a record button
   or its key in the item form, open the button's scoped screen for the
   item, named from the form, which goes back to the form in the mode it had
   then, a copy kept.  On a screen of kinds the form, its caption and the
   notes are those of the selected row's kind, or of kind for a new record,
   which its preset and loaded functions fill.  The rows are counted after a
   dialog that sent a request, a new record saved or a copy kept being a row
   more (count_rows), as is a copy whose form could not be made or read.  A
   copy just made whose dialog was cancelled before its OK sent anything
   (uncopy) is deleted with ITEM DEL, as if never made, which leaves the
   list as it was; a failure is shown and gone on from as it says.  A
   location saved gives the contents beside the tree its new name when they
   are its own (wloc_saved). */
static void rec_show(const ws_scr *s, const char *key, u8 mode, u8 copy,
                     u8 kind)
{
    char id[LV_KEYSZ];
    char name[WF_NAMESZ];
    char text[LV_ROWTEXT + 1];
    form_field fields[FORM_MAXFIELDS];
    form_def def;
    wf_ops ops;
    form *f;
    rq q;
    rq_res res;
    int r;
    int ok;
    int del;
    int kept;
    int sent;
    u8 act;

    if (s->kinds != NULL && mode != FORM_NEW && wl_text(iw_cur->list, text))
        kind = s->kinds->kind(text);
    txt_copy(id, mode == FORM_NEW ? k_empty : key, (u16)sizeof id);
    act = RQE_STAY;
    ok = form_for(s, mode, kind, &ops, &def, fields, &f);
    if (ok && mode != FORM_NEW && rec_get(s, id, f, &act) != RQ_OK) {
        form_close(f);
        ok = 0;
    }
    if (!ok) {
        if (copy == IW_COPY)
            count_rows(s, 1);
        after(act);
        return;
    }
    if (mode == FORM_NEW) {
        new_values(s, f, key, kind);
        if (s->kinds != NULL)
            s->loaded(f);
    } else if (s->loaded != NULL) {
        s->loaded(f);
    }
    r = rec_run(s, &ops, f, id, mode, copy);
    act = ended(r);
    del = uncopy(r, copy, f);
    kept = copy == IW_COPY && !del;
    sent = wf_sent(f);
    if (sent || kept)
        count_rows(s, kept ||
                      (mode == FORM_NEW && form_mode_get(f) != FORM_NEW));
    mode = form_mode_get(f);
    if (r == WF_OK && (s->flags & WS_HOLDS))
        wloc_saved(id, f);
    if (r >= WF_ACT)
        wf_name(f, &ops, id, name);
    form_close(f);
    if (r >= WF_ACT) {
        scope_open(item_scopes[r - WF_ACT], id, name, mode,
                   copy == IW_REC ? IW_REC : IW_COPYKEPT);
    } else if (del) {
        rq_begin(&q, w_del);
        rq_word(&q, id);
        if (request(&q, NULL, NULL, &res, &act) != RQ_OK) {
            count_rows(s, 1);
            after(act);
        }
    } else if (r != WF_CANCEL || sent) {
        after_key(act, r == WF_OK && s == iw_cur->scr ? id : NULL);
    }
}

/* iw_row_fn of a new loan, reservation or use asked once for several
   rows, arg its iw_hold: a new record of the kind for item key in the
   form the rows share, its first values as the asked form's started
   (new_values), then every value asked but the item's as a change, saved
   as the dialog's OK saved the first (form_save; loan_ops has no save of
   its own). */
static int hold_row(void *arg, const char *key, rq_res *res)
{
    char id[LV_KEYSZ];
    const iw_hold *h;
    const char *name;
    u16 i;

    h = (const iw_hold *)arg;
    for (i = 0; i < h->def->nfields; i++)
        form_load(h->g, h->def->fields[i].name, k_empty);
    form_mode_set(h->g, FORM_NEW);
    new_values(&wloan_item, h->g, key, h->kind);
    for (i = 0; i < h->def->nfields; i++) {
        name = h->def->fields[i].name;
        if (strcmp(name, wloan_item.scope) != 0)
            form_put(h->g, name, form_get(h->f, name));
    }
    id[0] = '\0';
    return form_save(h->g, iw_l, wloan_item.ops->table, id, (u16)sizeof id,
                     res);
}

/* A new loan, reservation or use, kind, of the n items chosen, the first
   of key: its form shown once, for that item, as rec_show shows it, the
   help panel saying IW_S_EACH; once its OK has saved it, each other item's
   saved with the same values (hold_row), a row more for each (each_end);
   else, the help panel emptied, the end gone on from as rec_show does. */
static void rec_holds(const char *key, u8 kind, u16 n)
{
    char id[LV_KEYSZ];
    form_field fields[FORM_MAXFIELDS];
    form_def def;
    wf_ops ops;
    iw_hold h;
    const ws_scr *s;
    int r;
    int sent;
    u8 act;
    u16 k;

    s = &wloan_item;
    if (!form_for(s, FORM_NEW, kind, &ops, &def, fields, &h.f))
        return;
    if (wf_open(&h.g, &def, FORM_NEW) != 0) {
        form_close(h.f);
        fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
        return;
    }
    new_values(s, h.f, key, kind);
    s->loaded(h.f);
    help_n(IW_S_EACH, n, n, 1);
    id[0] = '\0';
    r = rec_run(s, &ops, h.f, id, FORM_NEW, IW_REC);
    if (r != WF_OK) {
        panel(IW_P_HELP, NULL);
        iw_noted = 0;
    }
    k = 0;
    act = ended(r);
    sent = wf_sent(h.f);
    if (r == WF_OK) {
        h.def = &def;
        h.kind = kind;
        k = each(IW_S_RLEND + kind, hold_row, &h, 1, n, &act);
    } else if (sent) {
        count_rows(s, form_mode_get(h.f) != FORM_NEW);
    }
    form_close(h.g);
    form_close(h.f);
    if (r == WF_OK)
        each_end(IW_S_RLEND + kind, k, n, s, 1, act);
    else if (r != WF_CANCEL || sent)
        after(act);
}

/* A new record of screen s, of kind on a screen of kinds, given the
   scope's key of a scoped screen, on a tree (WS_TREE) key, the selected
   record's, "" when none is or INS was not pressed on it, which must
   have room for one more level (wloc_room), or the item asked for in
   the dialog of s->ask, titled for kind, which finds it (ask). */
static void rec_new(const ws_scr *s, const char *key, u8 kind)
{
    char scope[LV_KEYSZ];
    form_def def;
    form *f;
    int r;
    u8 act;

    if (s->ask == NULL) {
        if (!(s->flags & WS_TREE))
            rec_show(s, iw_cur->scope, FORM_NEW, IW_REC, kind);
        else if (key[0] == '\0' || wloc_room(iw_frame, key, &act))
            rec_show(s, key, FORM_NEW, IW_REC, 0);
        else
            leave(act);
        return;
    }
    memcpy(&def, s->ask, sizeof def);
    def.title = (u16)(def.title + kind);
    r = ask(&def, NULL, k_empty, NULL, &f);
    if (r != WF_OK) {
        leave(ended(r));
        return;
    }
    form_get_text(f, s->scope, scope, (u16)sizeof scope);
    form_close(f);
    rec_show(s, scope, FORM_NEW, IW_REC, kind);
}

/* Delete record key of screen s with its delete command once the user
   has said yes to the question naming it (rec_name), showing the
   screen's note of it, a row fewer (count_rows); the list is fetched
   again after a delete, or a 200 (the record had gone), as INV does. */
static void rec_del(const ws_scr *s, const char *key)
{
    char name[WF_NAMESZ];
    char text[MSG_MAXLEN + 1];
    rq q;
    rq_res res;
    int rc;
    u8 act;

    if (!rec_name(s, key, name))
        return;
    txt_copy(text, msg_sub(s->delq, name, NULL), (u16)sizeof text);
    OemToAnsi(text, text);
    if (ws_say(iw_frame, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2,
               text) != IDYES)
        return;
    rq_begin(&q, s->del);
    rq_word(&q, key);
    rc = request(&q, NULL, NULL, &res, &act);
    if (rc == RQ_OK) {
        note(note_of(s, WS_N_DELETED), key, name);
        count_rows(s, -1);
    }
    if (rc == RQ_OK || rc == RQ_ENOTFOUND)
        after(act);
    else
        leave(act);
}

/* iw_row_fn of DEL on several rows, arg the screen: row key deleted with
   its delete command, a record gone already (200) made, as rec_del takes
   it. */
static int del_row(void *arg, const char *key, rq_res *res)
{
    rq q;
    int rc;

    rq_begin(&q, ((const ws_scr *)arg)->del);
    rq_word(&q, key);
    rc = rq_send(iw_l, &q, NULL, NULL, res);
    return rc == RQ_ENOTFOUND ? RQ_OK : rc;
}

/* Delete the n rows chosen of screen s (each) once the user has said
   yes to the question naming their number (IW_S_DELN), or, when they
   are every row of the list, saying so (IW_S_DELALL), a row fewer for
   each (each_end). */
static void rec_dels(const ws_scr *s, u16 n)
{
    char fmt[IW_FMTSZ];
    char t[IW_FMTSZ + TXT_U32LEN];
    u8 act;
    u16 k;

    LoadString(iw_inst, wl_all(iw_cur->list) ? IW_S_DELALL : IW_S_DELN, fmt,
               sizeof fmt);
    wsprintf(t, fmt, (int)n);
    if (ws_say(iw_frame, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, t) !=
        IDYES)
        return;
    k = each(IW_S_RDEL, del_row, (void *)s, 0, n, &act);
    each_end(IW_S_RDEL, k, n, s, -1, act);
}

/* Copy item key of screen s with ITEM COPY and show the copy, the ID of
   its +OK, in the form dialog in FORM_EDIT, as INV's Duplicate does, a cancel
   deleting it (rec_show); a failure is shown and left as it says, the
   list as it was. */
static void rec_copy(const ws_scr *s, const char *key)
{
    char id[LV_KEYSZ];
    rq q;
    rq_res res;
    u8 act;

    rq_begin(&q, w_copy);
    rq_word(&q, key);
    if (request(&q, NULL, NULL, &res, &act) != RQ_OK)
        leave(act);
    else if (rq_okval(&res, itl_k_id, id, (u16)sizeof id))
        rec_show(s, id, FORM_EDIT, IW_COPY, 0);
}

/* rq_fn of rec_act: the item's LOC_ID into the LINK_IDLEN + 1 bytes at
   arg. */
static int loc_line(void *arg, const rq_line *ln)
{
    if (ln->kind == 'K' && ln->n >= 2 &&
        strcmp(ln->v[0], scr_item_fields[SCR_IF_LOC].name) == 0)
        txt_copy((char *)arg, ln->v[1], LINK_IDLEN + 1);
    return 0;
}

/* The location of item key into at (LINK_IDLEN + 1 bytes), "0" for
   none, read with ITEM GET: 1, or 0 once its failure has been shown and
   the list fetched again or left as it says (after). */
static int item_at(const char *key, char *at)
{
    rq q;
    rq_res res;
    u8 act;

    strcpy(at, "0");
    rq_begin(&q, w_get);
    rq_word(&q, key);
    if (request(&q, loc_line, at, &res, &act) == RQ_OK)
        return 1;
    after(act);
    return 0;
}

/* Carry out command a of record key, the first of the n rows chosen, in
   its dialog (ask), a command's location field starting where the item
   is (item_at), its title and note those of the first row's kind for
   WA_KIND, the focus row's when every row is chosen, as the list holds
   no other row's text then, and show its note once it is made (ws_act's
   note), none when nothing was sent (WA_KEEP), and count the rows, a
   record it makes (an ID in its +OK) a row more (count_rows); then, or
   once its record has gone (WF_GONE), the list is fetched again, as INV
   does, and a failure that leaves leaves.  For several rows the caption
   names them (IW_S_MORE) and, once the dialog has made the first, each
   other row's request is sent with the values asked (act_row, each). */
static void rec_act(const ws_act *a, const char *key, u16 n)
{
    char at[LINK_IDLEN + 1];
    char text[LV_ROWTEXT + 1];
    char fmt[IW_FMTSZ];
    char cap[IW_ROWSSZ];
    iw_act x;
    form_def def;
    const char *v;
    form *f;
    int r;
    u8 kind;
    u8 act;
    u16 k;

    memcpy(&def, a->def, sizeof def);
    kind = 0;
    if ((a->flags & WA_KIND) &&
        (wl_all(iw_cur->list) ? wl_text(iw_cur->list, text) :
                                wl_chosen(iw_cur->list, 0, NULL, text)))
        kind = iw_cur->scr->kinds->kind(text);
    def.title = (u16)(def.title + kind);
    x.a = a;
    x.key = key;
    x.id[0] = '\0';
    x.same = 0;
    txt_copy(cap, key, (u16)sizeof cap);
    if (n > 1) {
        LoadString(iw_inst, IW_S_MORE, fmt, sizeof fmt);
        wsprintf(cap, fmt, (LPCSTR)key, (int)(n - 1));
    }
    if (a->loc != NULL && !item_at(key, at))
        return;
    r = ask(&def, &x, cap, a->loc != NULL ? at : NULL, &f);
    if (r == WF_OK && n > 1) {
        x.f = f;
        k = each(a->label, act_row, &x, 1, n, &act);
        form_close(f);
        each_end(a->label, k, n, iw_cur->scr, 0, act);
        return;
    }
    if (r == WF_OK && x.same) {
        form_close(f);
    } else if (r == WF_OK) {
        v = form_get(f, a->loc != NULL ? a->loc->path->name :
                                         a->def->fields[0].name);
        if (x.id[0] != '\0')
            note(a->note, x.id, key);
        else if (a->flags & WA_SCOPE)
            note(a->note, v, key);
        else if (a->flags & WA_KIND)
            note(iw_cur->scr->kinds->ends[kind], key,
                 msg_sub((u16)(iw_cur->scr->ops->title + kind), key, NULL));
        else
            note(a->note, key, v);
        count_rows(iw_cur->scr, x.id[0] != '\0');
        form_close(f);
    }
    if (r == WF_OK || r == WF_GONE)
        after(RQE_STAY);
    else
        leave(ended(r));
}

/* 1 when ESC on screen s goes back to the screen it was opened from
   (back): a scoped screen, the user list or the audit log. */
static int goes_back(const ws_scr *s)
{
    return (s->flags & WS_SCOPED) || s == &wuser_scr || s == &wadm_audit;
}

/* The name of item key, the scope of a screen gone back to, into
   iw_sname, read as rec_name does, or the key alone, said by no one,
   when the form or the read fails: the scoped list's own read then says
   why. */
static void scope_named(const char *key)
{
    form *f;

    txt_copy(iw_sname, key, (u16)sizeof iw_sname);
    if (wf_open(&f, item_scr.def, FORM_VIEW) != 0)
        return;
    if (rec_get(&item_scr, key, f, NULL) == RQ_OK)
        wf_name(f, &item_ops, key, iw_sname);
    form_close(f);
}

/* ESC on a scoped screen, the user list or the audit log: the screen it
   was opened from again, in the order its list had, on the row it had
   where list_seek reaches it and else on its first, a scoped one's name
   read again (scope_named) unless it titles itself, the location tree by
   tree_open, the focus in the pane that had it, whose list seeks so the
   record of the scope left, if any, the row that screen was opened from
   (only a pane of items or parts opens a scoped screen), and the item
   form it was opened from, if so, for that record, over it, an item's
   shown from a loan list (row_item) as the item screen's; with none
   left, the item screen, as ESC leaves other lists. */
static void back(void)
{
    char key[LV_KEYSZ];
    iw_back b;

    if (iw_nback == 0) {
        list_open(&item_scr, iw_itemord, NULL);
        return;
    }
    b = iw_backs[--iw_nback];
    strcpy(key, iw_panes[0].scope);
    strcpy(iw_panes[0].scope, b.scope);
    if ((b.scr->flags & WS_SCOPED) && b.scr->name == NULL)
        scope_named(b.scope);
    if (b.scr->flags & WS_HOLDS)
        tree_open(b.sel);
    else
        list_open(b.scr, b.scr->src->orders[b.order], b.sel);
    if (b.pane != 0 && pane_shown() && IsWindowEnabled(pane_wnd(b.pane))) {
        pane_focus(b.pane);
        if (iw_cur != iw_panes && key[0] != '\0')
            list_seek(iw_cur, key);
    }
    if (b.mode != IW_NOFORM && iw_cur->list != NULL)
        rec_show(iw_cur->scr->kinds != NULL ? &item_scr : iw_cur->scr, key,
                 b.mode, b.copy, 0);
}

/* Screen s's own command of the record key k, or NULL. */
static const ws_act *act_of(const ws_scr *s, u16 k)
{
    const ws_act *a;

    if (s->act == NULL)
        return NULL;
    for (a = s->act; a->key != 0; a++)
        if (a->key == k)
            return a;
    return NULL;
}

/* Run the search dialog over the frame for cmd (wsrch_ask) and open the
   results of its search, or leave after a failure that leaves (ended);
   after ESC on the results (TUI_CMD_NONE) its Cancel opens the item
   screen, as ESC leaves the other lists. */
static void search(u8 cmd)
{
    int r;
    u8 act;

    r = wsrch_ask(iw_frame, iw_l, iw_level, cmd);
    act = ended(r);
    if (r == WF_OK)
        list_open(&found_scr, NULL, NULL);
    else if (act != RQE_STAY)
        leave(act);
    else if (r == WF_CANCEL && cmd == TUI_CMD_NONE)
        list_open(&item_scr, iw_itemord, NULL);
}

/* Find the items holding the Find box's text, its blanks at both ends
   left out (wsrch_find), in place of the list shown, which takes the
   focus; with no text, run the search dialog, as Find text does. */
static void find_run(void)
{
    char t[LINK_MAXFIND + 1];

    GetWindowText(iw_find, t, sizeof t);
    AnsiToOem(t, t);
    if (wsrch_find(t))
        list_open(&found_scr, NULL, NULL);
    else
        search(SCMD_FIND);
}

/* 1 when m is a key of the Find box, taken: CTRL+F without SHIFT and
   ALT, in any of the frame's windows while a user is logged in, gives
   the box the focus, its text selected; in the box ENTER finds its text
   (find_run), and ESC and TAB give the focus back to the list. */
static int find_key(const MSG *m)
{
    if (m->message != WM_KEYDOWN || iw_level == 0 || iw_busy)
        return 0;
    if (m->hwnd == iw_find && (m->wParam == VK_RETURN ||
                               m->wParam == VK_ESCAPE ||
                               m->wParam == VK_TAB)) {
        if (m->wParam == VK_RETURN)
            find_run();
        else
            SetFocus(iw_frame);
        return 1;
    }
    if (m->wParam != 'F' || GetKeyState(VK_CONTROL) >= 0 ||
        GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_MENU) < 0 ||
        !IsChild(iw_frame, m->hwnd))
        return 0;
    SetFocus(iw_find);
    SendMessage(iw_find, EM_SETSEL, 0, MAKELPARAM(0, -1));
    return 1;
}

/* Open the contents of location key of the screen shown (wloc_enter),
   or go on as its failed read says (after). */
static void contents(const char *key)
{
    u8 act;

    if (wloc_enter(iw_frame, key, &act))
        list_open(&ws_contents, NULL, NULL);
    else
        after(act);
}

/* Move record key of screen s (ws_scr's move), named first (rec_name)
   for the note of the move; then the list is fetched again, a tree's
   seeking the record, and a failure that leaves leaves. */
static void rec_move(const ws_scr *s, const char *key)
{
    char name[WF_NAMESZ];
    u8 act;

    if (!rec_name(s, key, name))
        return;
    if (s->move(iw_frame, iw_l, iw_level, key, &act)) {
        note(note_of(s, WS_N_MOVED), key, name);
        after_key(RQE_STAY, key);
    } else {
        leave(act);
    }
}

/* Ask for the audit log's filter over the frame (wadm_filter) and open
   the audit log with it (admin_open). */
static void audit(void)
{
    int r;

    r = wadm_filter(iw_frame);
    if (r == WF_OK)
        admin_open(&wadm_audit);
    else if (r == WF_EFAIL)
        fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
}

/* Open the location tree, the names of the location types loaded again
   first (wloc_types), as another screen may have changed them, with the
   row of key sel selected (list_open), or leave as a failed load says. */
static void tree_open(const char *sel)
{
    u8 act;

    if (wloc_types(iw_frame, iw_l, &act) || act == RQE_STAY)
        list_open(&wloc_scr, NULL, sel);
    else
        leave(act);
}

/* ESC on the screen shown, but the item screen: the pane beside the
   master gives the master the focus; a scoped screen, the user list or the
   audit log goes back to where it was opened from (back), the search's
   results to the search dialog, a location's contents to the location
   list, any other to the item screen, as a login opens it. */
static void esc(void)
{
    const ws_scr *s;

    s = iw_panes[0].scr;
    if (pane_at() != 0)
        pane_focus(0);
    else if (goes_back(s))
        back();
    else if (s->src == &wsrch_src)
        search(TUI_CMD_NONE);
    else if (s == &ws_contents)
        tree_open(wloc_id);
    else
        list_open(&item_scr, iw_itemord, NULL);
}

/* The kind of a new loan, reservation or use whose letter is vk (with
   CTRL), SCR_K_LOAN to SCR_K_USE, or IW_NHOLD for none. */
static u8 hold_of(UINT vk)
{
    static const u8 hold_keys[IW_NHOLD] = { 'L', 'R', 'U' };
    u8 i;

    for (i = 0; i < IW_NHOLD && hold_keys[i] != vk; i++)
        ;
    return i;
}

/* 1 when the list of screen s chooses several rows (WL_MULTI of
   WLIST.H): the screen has commands of its own and is no tree. */
static int several(const ws_scr *s)
{
    return s->act != NULL && !(s->flags & WS_TREE);
}

/* The pane of the split view that F6 or TAB (k), with SHIFT the one
   before, gives the focus from the focused one, or -1: none without
   the split view or while that pane is disabled. */
static int pane_next(u16 k)
{
    int i;

    if (!pane_shown())
        return -1;
    i = (pane_at() + ((k & IW_M_SHIFT) ? IW_NPANES - 1 : 1)) % IW_NPANES;
    return IsWindowEnabled(pane_wnd(i)) ? i : -1;
}

/* What the record key k does in the focused list, a row selected when
   sel: an IW_K_ value, with *a the screen's own command of IW_K_ACT.
   ENTER (or a double click) shows the selected record, or a location's
   contents (WS_HOLDS) or a category's items, CTRL+E edits it, INS adds
   a record, DEL deletes the selected one, both without SHIFT and CTRL,
   which make them Windows' clipboard keys, on the categories too, which
   name no form and no delete (cat_key), and the key of one of the
   screen's own commands carries it out on the selected record, or on the
   scope (WA_SCOPE); CTRL+D duplicates the selected item on a screen of
   items; CTRL+L, CTRL+R and CTRL+U add a loan, a reservation or a use of
   the selected item on a screen of items, or of a new record on a screen
   of kinds that adds records, or that adds those of a host (ws_kinds'
   hosts); CTRL+PGDN shows the screen's next view, CTRL+PGUP the one
   before (WS_VIEWS); F6 and TAB, with or without SHIFT, give the focus
   to the other pane of the split view (pane_next); F5 fetches the list
   again, or the preview panel's record while it has the focus; CTRL+/
   chooses every row of a list of several rows (several) and CTRL+\ the
   focus row alone; ESC leaves the screen, or the pane beside the master
   (esc), and does nothing on the item screen's list.  The Record menu
   alone moves the selected record (WS_K_MOVE, ws_scr's move), shows the
   row's item where its rows name one (WS_K_ITEM), takes the list's
   next order (WS_K_ORDER), on a location's contents switches between
   its items not installed in another and all (WS_K_TOP, wloc_top) and
   between its own items and those of every location below it too
   (WS_K_BELOW, wloc_all), and on a screen of items opens a scoped
   screen of the selected item (WS_K_PARTS to WS_K_USED,
   item_scopes). */
static int key_of(u16 k, int sel, const ws_act **a)
{
    const ws_scr *s;
    UINT vk;
    int shows;
    int cats;

    s = iw_cur->scr;
    vk = k < WS_K_MOVE ? k % IW_M_SHIFT : 0;
    *a = act_of(s, k);
    cats = s == &cat_scr;
    shows = s->def != NULL || s->kinds != NULL || cats;
    if (vk == VK_RETURN && sel && ((s->flags & WS_HOLDS) || cats))
        return IW_K_HOLDS;
    if (vk == VK_RETURN && sel && shows)
        return IW_K_VIEW;
    if (k == IW_CTRL + 'E' && sel && shows)
        return IW_K_EDIT;
    if (k == VK_INSERT && !(s->flags & WS_NONEW))
        return IW_K_NEW;
    if (k == VK_DELETE && sel && (s->del != NULL || cats))
        return IW_K_DEL;
    if (*a != NULL && (sel || ((*a)->flags & WA_SCOPE)))
        return IW_K_ACT;
    if (k == WS_K_MOVE && sel && s->move != NULL)
        return IW_K_MOVE;
    if (k == vk + IW_CTRL && hold_of(vk) < IW_NHOLD &&
        ((sel && (s->flags & WS_ITEMS)) ||
         (s->kinds != NULL && !(s->flags & WS_NONEW)) ||
         (s->kinds != NULL && (s->kinds->hosts >> hold_of(vk) & 1))))
        return IW_K_HOLD;
    if (k == WS_K_ITEM && sel && s->kinds != NULL && s->kinds->item != NULL)
        return IW_K_ITEM;
    if ((k == IW_CTRL + VK_NEXT || k == IW_CTRL + VK_PRIOR) &&
        (s->flags & WS_VIEWS))
        return IW_K_NEXT;
    if (k == WS_K_ORDER && iw_cur->src.norders > 0)
        return IW_K_ORDER;
    if ((vk == VK_F6 || vk == VK_TAB) && !(k & IW_CTRL) &&
        pane_next(k) >= 0)
        return IW_K_PANE;
    if (k == WS_K_TOP && s == &ws_contents)
        return IW_K_TOP;
    if (k == WS_K_BELOW && s == &ws_contents)
        return IW_K_BELOW;
    if (k == VK_F5)
        return IW_K_REFRESH;
    if (k == IW_CTRL + WL_VK_ALL && sel && several(s))
        return IW_K_ALL;
    if (k == IW_CTRL + WL_VK_NONE)
        return IW_K_ONE;
    if (vk == VK_ESCAPE && (s != &item_scr || iw_inview))
        return IW_K_BACK;
    if (k >= WS_K_PARTS && k <= WS_K_USED && sel && (s->flags & WS_ITEMS))
        return IW_K_SCOPE;
    if (k == IW_CTRL + 'D' && sel && (s->flags & WS_ITEMS))
        return IW_K_COPY;
    return IW_K_NONE;
}

/* Show the item of the selected row of the screen shown in the item
   form, as a record of the item screen (rec_show), when the row names
   one. */
static void row_item(void)
{
    char text[LV_ROWTEXT + 1];
    char id[LINK_IDLEN + 1];

    if (wl_text(iw_cur->list, text) && iw_cur->scr->kinds->item(text, id))
        rec_show(&item_scr, id, FORM_VIEW, IW_REC, 0);
}

/* The master's row that the pane beside it follows, its key into key
   (LV_KEYSZ bytes) and, text not NULL, its text into text (LV_ROWTEXT +
   1 bytes): the one row chosen (wl_chosen), which add mode may leave off
   the focus row, and with several chosen, or every row of the list,
   whose texts the list does not hold, the focus row; 1, or 0 with key
   "" without rows. */
static int pane_row(char *key, char *text)
{
    wl *m;

    m = iw_panes[0].list;
    if (wl_nchosen(m) == 1 && !wl_all(m))
        return wl_chosen(m, 0, key, text);
    if (text != NULL)
        wl_text(m, text);
    return wl_selected(m, key);
}

/* The rows chosen in the focused list (wl_nchosen), the first one's key
   into key (LV_KEYSZ bytes), for a command that acts on them. */
static u16 rows_of(char *key)
{
    wl_chosen(iw_cur->list, 0, key, NULL);
    return wl_nchosen(iw_cur->list);
}

/* Edit, New or Delete, op, on the categories' list, carried out as the
   Categories dialog carries it out (wcode_form and wcode_del of
   WCODE.H), on the selected row's entry as the list shows it
   (wrow_code_get), and noted as INV notes it, ME_SAVED, ME_ADDED or
   ME_DELETED with the entry's name.  Once the entry has changed, or was
   found gone, the categories held are loaded again (cats_fresh) and the
   list is counted again and fetched from its start, the entry saved
   sought, as its sort order may have moved it, or after a delete on the
   row it had; its pane then shows no row's (iw_shown), so that it
   follows again, for the names in its rows.  A form that could not be
   made is said, and a failure that leaves leaves (ended). */
static void cat_key(int op)
{
    char text[LV_ROWTEXT + 1];
    ct_row r;
    int rc;

    r.code[0] = '\0';
    if (op != IW_K_NEW && wl_text(iw_cur->list, text))
        wrow_code_get(text, &r);
    rc = op == IW_K_DEL ? wcode_del(iw_frame, SCR_CK_CAT, &r) :
         wcode_form(iw_frame, iw_l, iw_level, SCR_CK_CAT, &r);
    if (rc == WF_OK) {
        note((u16)(op == IW_K_DEL ? ME_DELETED :
                   op == IW_K_NEW ? ME_ADDED : ME_SAVED), r.name, NULL);
    } else if (rc != WF_GONE) {
        leave(ended(rc));
        return;
    }
    cats_fresh();
    iw_shown[0] = '\0';
    wl_recount(iw_cur->list);
    if (rc == WF_OK && op != IW_K_DEL)
        wl_find(iw_cur->list, r.code);
    else
        wl_move(iw_cur->list, PG_RELOAD);
    moved();
}

/* Choose every row of the focused list (wl_select_all), the help panel
   counting the rows as their keys are collected (list_note); a list of
   more rows than can be chosen is said, with the most (IW_S_OVER), over
   the rows counted, and then the help panel is emptied. */
static void select_all(void)
{
    char fmt[IW_FMTSZ];
    char t[IW_FMTSZ + TXT_U32LEN];
    int rc;

    rc = wl_select_all(iw_cur->list);
    if (rc == WL_EOVER) {
        LoadString(iw_inst, IW_S_OVER, fmt, sizeof fmt);
        wsprintf(t, fmt, (UINT)KS_MAXROWS);
        ws_say(iw_frame, MB_ICONEXCLAMATION, t);
    }
    panel(IW_P_HELP, NULL);
}

/* Carry out the record key k in the focused list, from the list or the
   Record menu, as key_of says; a change of records is refused below
   TUI_LVL_OPER (allow).  A new loan, reservation or use of a screen of
   items is a new record of the item's own loans and holds, on the
   screen shown.  DEL, a new loan, reservation or use of a screen of
   items and a screen's command that takes several rows (WA_MULTI) act on
   the rows chosen (INVWIN.H, Several rows), the others on the selected
   one.  While the preview panel has the focus, the keys are its owner's
   (WPREV.H) and act on the master's row all the same, the selected one
   being the row the panel follows (pane_row), but ENTER in a panel that
   did not load (iw_noload), which loads it, as F5 loads it again.  On
   the categories ENTER opens the selected one's items as a screen of
   their own, scoped by its code, and Edit, New and Delete are
   cat_key's. */
static void rec_key(u16 k)
{
    char key[LV_KEYSZ];
    char name[WF_NAMESZ];
    const ws_act *a;
    int op;
    u16 n;

    op = key_of(k, iw_inview ? pane_row(key, NULL)
                             : wl_selected(iw_cur->list, key), &a);
    if (op != IW_K_NONE && op <= IW_K_MOVE && !allow())
        return;
    if (iw_cur->scr == &cat_scr && op >= IW_K_EDIT && op <= IW_K_DEL) {
        cat_key(op);
        return;
    }
    switch (op) {
    case IW_K_EDIT:
        rec_show(iw_cur->scr, key, FORM_EDIT, IW_REC, 0);
        break;
    case IW_K_NEW:
        rec_new(iw_cur->scr, key, SCR_K_LOAN);
        break;
    case IW_K_HOLD:
        if (!(iw_cur->scr->flags & WS_ITEMS)) {
            rec_new(iw_cur->scr, key, hold_of(k % IW_M_SHIFT));
            break;
        }
        n = rows_of(key);
        if (n > 1)
            rec_holds(key, hold_of(k % IW_M_SHIFT), n);
        else
            rec_show(&wloan_item, key, FORM_NEW, IW_REC,
                     hold_of(k % IW_M_SHIFT));
        break;
    case IW_K_DEL:
        n = rows_of(key);
        if (n > 1)
            rec_dels(iw_cur->scr, n);
        else
            rec_del(iw_cur->scr, key);
        break;
    case IW_K_ACT:
        n = (a->flags & WA_MULTI) ? rows_of(key) : 1;
        rec_act(a, (a->flags & WA_SCOPE) ? iw_cur->scope : key, n);
        break;
    case IW_K_COPY:
        rec_copy(iw_cur->scr, key);
        break;
    case IW_K_VIEW:
        if (iw_inview && iw_noload)
            pane_load(0);
        else
            rec_show(iw_cur->scr, key, FORM_VIEW, IW_REC, 0);
        break;
    case IW_K_HOLDS:
        if (iw_cur->scr == &cat_scr)
            scope_open(&ws_catitems, key, k_empty, IW_NOFORM, IW_REC);
        else
            contents(key);
        break;
    case IW_K_SCOPE:
        if (rec_name(iw_cur->scr, key, name))
            scope_open(item_scopes[k - WS_K_PARTS], key, name,
                       IW_NOFORM, IW_REC);
        break;
    case IW_K_ITEM:
        row_item();
        break;
    case IW_K_MOVE:
        rec_move(iw_cur->scr, key);
        break;
    case IW_K_NEXT:
        wloan_shown = (u8)((wloan_shown + (k == IW_CTRL + VK_PRIOR ?
                                           SCR_NVIEWS - 1 : 1)) %
                           SCR_NVIEWS);
        list_open(&wloan_list, NULL, NULL);
        break;
    case IW_K_ORDER:
        wl_move(iw_cur->list, PG_ORDER);
        break;
    case IW_K_BACK:
        esc();
        break;
    case IW_K_TOP:
        wloc_top = (u8)!wloc_top;
        contents_again();
        break;
    case IW_K_BELOW:
        wloc_all = (u8)!wloc_all;
        contents_again();
        break;
    case IW_K_PANE:
        pane_focus(pane_next(k));
        break;
    case IW_K_REFRESH:
        pane_stale();
        if (iw_inview) {
            pane_load(0);
        } else {
            wl_recount(iw_cur->list);
            wl_move(iw_cur->list, PG_RELOAD);
        }
        break;
    case IW_K_ALL:
        select_all();
        break;
    case IW_K_ONE:
        wl_deselect(iw_cur->list);
        break;
    default:
        break;
    }
}

/* The Record menu's record keys in its order, 0 a separator; the first
   IW_NCORE are its entries always there, IW_S_ROPEN on. */
static const u16 rec_keys[] = {
    VK_RETURN, IW_CTRL + 'E', VK_INSERT, VK_DELETE, 0,
    IW_CTRL + WL_VK_ALL, 0,
    WS_K_MOVE, IW_CTRL + 'D', WS_K_INSTALL, WS_K_ITEM, WS_K_REMOVE,
    WS_K_RETURN, WS_K_SPLIT, 0,
    IW_CTRL + 'L', IW_CTRL + 'R', IW_CTRL + 'U', 0,
    WS_K_PARTS, WS_K_SVC, WS_K_LOANS, WS_K_USED, 0,
    IW_CTRL + VK_NEXT, WS_K_ORDER, WS_K_TOP, WS_K_BELOW, VK_F6, VK_ESCAPE
};

/* The entry's string of each IW_K_ value, the View menu's for F5's
   refresh, which is no entry of rec_keys, 0 for none or for the one of
   the command (IW_K_ACT), of the kind (IW_K_HOLD) or of the scoped
   screen (IW_K_SCOPE). */
static const u8 key_texts[] = {
    0, IW_S_REDIT, IW_S_RNEW, IW_S_RDEL, 0, IW_S_RCOPY, 0, IW_S_RMOVE,
    IW_S_ROPEN, IW_S_ROPEN, 0, IW_S_RITEM, IW_S_RNEXT, IW_S_RORDER,
    IW_S_RBACK, IW_S_RTOP, IW_S_RBELOW, IW_S_RPANE, IW_S_VREFRESH,
    IW_S_RALL, 0
};

/* The string of the Record entry of record key k on the screen shown,
   named for what the key does there with a row selected, *op (key_of),
   or 0 when it has none; one of the first IW_NCORE of rec_keys always
   has its own, *op then IW_K_NONE where it does something else, but a
   screen's own command (INS installing a part). */
static UINT rec_text(u16 k, int *op)
{
    const ws_act *a;
    UINT ids;
    int i;

    *op = key_of(k, 1, &a);
    if (*op == IW_K_ACT)
        ids = a->label;
    else if (*op == IW_K_SCOPE)
        ids = IW_S_RPARTS + k - WS_K_PARTS;
    else if (*op == IW_K_HOLD)
        ids = IW_S_RLEND + hold_of(k % IW_M_SHIFT);
    else
        ids = key_texts[*op];
    for (i = 0; i < IW_NCORE && rec_keys[i] != k; i++)
        ;
    if (i < IW_NCORE && ids != (UINT)(IW_S_ROPEN + i) && *op != IW_K_ACT) {
        ids = IW_S_ROPEN + i;
        *op = IW_K_NONE;
    }
    return ids;
}

/* Fill the Record menu for the focused list, noting in iw_rsel whether
   it has a row selected: an entry for each key of rec_keys that
   has one there (rec_text).  An entry is grayed while it does nothing,
   as without a list, or nothing without a selected row and the list has
   none, or changes records and the user's level is below TUI_LVL_OPER,
   and Not installed only and Everything below are checked while they
   are on (wloc_top, wloc_all).  A separator stands between groups that
   have entries.  The entry's command is IW_M_KEY plus its record key.
   A button of the tool bar but Find is usable exactly while the menu
   holds its entry (tool_texts), not grayed. */
static void rec_menu(void)
{
    char t[IW_FMTSZ];
    char key[LV_KEYSZ];
    u8 on[IW_NTOOLS];
    const ws_act *a;
    UINT ids;
    UINT st;
    int sep;
    int op;
    int i;
    int k;

    while (DeleteMenu(iw_rmenu, 0, MF_BYPOSITION))
        ;
    memset(on, 0, sizeof on);
    iw_rsel = (u8)(iw_cur->list != NULL && wl_selected(iw_cur->list, key));
    sep = 0;
    for (i = 0; i < (int)(sizeof rec_keys / sizeof rec_keys[0]); i++) {
        if (rec_keys[i] == 0) {
            sep = 1;
            continue;
        }
        ids = rec_text(rec_keys[i], &op);
        if (ids == 0)
            continue;
        st = op == IW_K_NONE || iw_cur->list == NULL ||
             key_of(rec_keys[i], iw_rsel, &a) != op ||
             (op <= IW_K_MOVE && iw_level < TUI_LVL_OPER) ? MF_GRAYED : 0;
        if ((op == IW_K_TOP && wloc_top) || (op == IW_K_BELOW && wloc_all))
            st |= MF_CHECKED;
        if (sep)
            AppendMenu(iw_rmenu, MF_SEPARATOR, 0, NULL);
        sep = 0;
        LoadString(iw_inst, ids, t, sizeof t);
        AppendMenu(iw_rmenu, MF_STRING | st, IW_M_KEY + rec_keys[i], t);
        for (k = 0; k < IW_NTOOLS && !(st & MF_GRAYED); k++)
            if (tool_cmds[k] == IW_M_KEY + rec_keys[i] &&
                (tool_texts[k] == ids || ids == IW_S_RPART))
                on[k] = 1;
    }
    for (k = 0; k < IW_NTOOLS; k++)
        EnableWindow(GetDlgItem(iw_tool, tool_cmds[k]), on[k]);
}

/* Make pane p the focused one: its screen's help topic, the Record menu
   and the tool bar filled for it, and its count shown. */
static void pane_use(iw_pane *p)
{
    iw_cur = p;
    wh_topic(p->scr->topic);
    rec_menu();
    status();
}

/* Give pane i the focus: its list made the focused one (pane_use), or,
   for the preview panel, the master staying so (iw_inview), its Record
   menu filled again for ESC there; and the focus of Windows while the
   frame is active. */
static void pane_focus(int i)
{
    iw_inview = (u8)(iw_panes[i].list == NULL);
    pane_use(iw_inview ? iw_panes : &iw_panes[i]);
    if (GetActiveWindow() == iw_frame)
        SetFocus(pane_wnd(i));
}

/* 1 when the window h is the list window of pane p or its box. */
static int in_pane(const iw_pane *p, HWND h)
{
    return p->list != NULL && h != NULL &&
           (h == wl_hwnd(p->list) || GetParent(h) == wl_hwnd(p->list));
}

/* After the main loop's message: the pane whose list has taken the focus,
   by a click, the focused one (pane_use); the preview panel as it takes
   the focus or gives it to the master noted (iw_inview), the Record
   menu filled again for ESC there. */
static void pane_follow(void)
{
    iw_pane *p;
    HWND f;
    u8 in;

    f = GetFocus();
    for (p = iw_panes; p < iw_panes + IW_NPANES; p++)
        if (p != iw_cur && in_pane(p, f))
            pane_use(p);
    in = (u8)(f != NULL && f == iw_view);
    if (in != iw_inview && (in || in_pane(iw_panes, f))) {
        iw_inview = in;
        rec_menu();
    }
}

/* Show in the pane beside the master what belongs to the master's row
   (pane_row), in the main loop, where no move, request, dialog or menu
   is under way, so that no request nests.  A row that is not the one the
   pane shows (iw_shown) is followed first (ws_pane's follow), a preview
   panel saying meanwhile that it loads (ws_pane's loading), the keys of
   a key script's A line typed ahead of it (td_ahead); a failure is
   shown and gone on from as it says, a list then left as it was and a
   panel saying that it did not load (IW_S_PNONE, iw_noload), as after
   an Esc between its requests, and showing no row's (pane_stale), so
   that the row's next load reads it.  A list that has followed takes
   the row's scope and is enabled, counted again and fetched from its
   start, though the scope be the one it had, as another loan's of the
   same item, whose rows a command on the row before may have changed;
   with open, as the screen opens (list_open) or opens again for the rows
   that fit (refit), the list is fetched so though it follows no new row,
   keeping the count it has pending or carried over, and a panel, made
   anew, follows again.  With no row selected the pane stays as it is,
   disabled until the first, whose Record entry (Next pane on the master)
   then comes; a panel is enabled from then on, loaded or not. */
static void pane_load(int open)
{
    char key[LV_KEYSZ];
    char scope[LV_KEYSZ];
    char text[LV_ROWTEXT + 1];
    const ws_pane *d;
    iw_pane *p;
    u8 act;

    iw_settle = IW_SETTLE_NONE;
    p = &iw_panes[1];
    if (pane_shown() && pane_row(key, text)) {
        if (open)
            pane_stale();
        if (strcmp(key, iw_shown) != 0) {
            d = iw_panes[0].scr->pane;
            if (iw_view != NULL)
                wpv_text(iw_view, d->loading, key);
            iw_noload = 0;
            td_ahead();
            if (!d->follow(iw_frame, key, text, scope, iw_view, &act)) {
                leave(act);
                open = 0;
                if (iw_view != NULL) {
                    wpv_text(iw_view, IW_S_PNONE, NULL);
                    pane_stale();
                    iw_noload = 1;
                }
            } else {
                strcpy(iw_shown, key);
                if (p->list != NULL) {
                    strcpy(p->scope, scope);
                    wl_recount(p->list);
                    open = 1;
                }
            }
        }
        if ((open || iw_view != NULL) && !IsWindowEnabled(pane_wnd(1))) {
            EnableWindow(pane_wnd(1), TRUE);
            rec_menu();
        }
        if (open && p->list != NULL)
            wl_move(p->list, PG_HOME);
    }
    hold();
}

/* Show the contents again for wloc_top and wloc_all: beside the tree
   their list fetched again and counted, the Record menu's check marks
   with them; else their screen opened again, titled for them. */
static void contents_again(void)
{
    if (pane_shown()) {
        wl_recount(iw_panes[1].list);
        wl_move(iw_panes[1].list, PG_HOME);
        rec_menu();
    } else {
        list_open(&ws_contents, NULL, NULL);
    }
}

/* After a divider's drag has kept a width for the tables of keep: the
   frame's lists fitted again (place), the tree's width following a
   table of locations, and the table's WINCOLS written to the settings
   file, only when it reads, so that no default is written over a file
   that could not be read; a failure is not said, the width holding for
   the session. */
static void cols_moved(u8 keep)
{
    cfg c;

    place(iw_frame);
    if (cfg_load(&c, iw_cfgpath) == 0) {
        wcol_widths(keep, c.cols[keep - 1]);
        cfg_save(&c, iw_cfgpath);
    }
}

/* The move of a drop on several rows (drop_row): Move's command and the
   location dropped on. */
typedef struct {
    const ws_act *a;
    const char *to;
} iw_drop;

/* iw_row_fn of a drop on several rows, arg its iw_drop: item key moved
   to the location, as Move moves it; the server changes nothing for one
   there already. */
static int drop_row(void *arg, const char *key, rq_res *res)
{
    const iw_drop *d;
    rq q;

    d = (const iw_drop *)arg;
    rq_begin(&q, d->a->cmd);
    rq_word(&q, key);
    rq_pair(&q, d->a->loc->loc->name, d->to);
    return rq_send(iw_l, &q, NULL, NULL, res);
}

/* Move the rows chosen in the focused pane, the contents beside the
   tree, whose box the drag's press gave the focus (pane_follow), dragged
   onto location to of the tree, as Move moves them (act_of) with to in
   place of its dialog.  One row: where it is read first (item_at),
   nothing sent when it is there already, as Move's WA_KEEP; then the
   location's path, which the note names as Move's dialog shows it below
   its field (wloc_get), and the command, its refusal shown over the
   frame in the text Move's dialog shows; then the contents fetched
   again, or left, as the failure says, and a move made noted and counted
   as Move's.  Several: nothing sent when every one is there already, the
   contents being those of to alone (wloc_all 0); else each moved as Move
   moves them after its dialog (drop_row, each), the first row too. */
static void drop(const char *to)
{
    char key[LV_KEYSZ];
    char at[LINK_IDLEN + 1];
    scr_locinfo x;
    const ws_act *a;
    iw_drop d;
    rq q;
    rq_res res;
    u8 act;
    u16 n;
    u16 k;

    a = act_of(iw_cur->scr, WS_K_MOVE);
    n = rows_of(key);
    if (n > 1) {
        if (!wloc_all && strcmp(to, wloc_id) == 0)
            return;
        d.a = a;
        d.to = to;
        k = each(a->label, drop_row, &d, 0, n, &act);
        each_end(a->label, k, n, iw_cur->scr, 0, act);
        return;
    }
    if (n == 0 || !item_at(key, at) || strcmp(at, to) == 0)
        return;
    if (!wloc_get(iw_frame, to, &x, &act)) {
        after(act);
        return;
    }
    rq_begin(&q, a->cmd);
    rq_word(&q, key);
    rq_pair(&q, a->loc->loc->name, to);
    if (request(&q, NULL, NULL, &res, &act) == RQ_OK) {
        note(a->note, key, x.path);
        count_rows(iw_cur->scr, 0);
    }
    after(act);
}

/* Follow step dr (WL_DR_ of WLIST.H) of a row's drag out of pane p, the
   contents beside the tree: the tree's row under the pointer marked as
   the drop target while it moves; from its start the drag cursor and
   IW_S_DRAG on the help panel, and no load of the contents to come
   (settle), so that none replaces the rows dragged from; its end or
   drop takes the cursor and the text away, a drop on a tree row moves
   the item there (drop), and then the main loop checks whether the
   contents are to follow the tree. */
static void drag(iw_pane *p, UINT dr)
{
    char t[IW_H_MAXLEN + 1];
    char to[LV_KEYSZ];
    wl *tree;
    int i;

    tree = iw_panes[0].list;
    i = wl_drag_over(p->list, tree, dr == WL_DR_DROP ? to : NULL);
    wl_target(tree, dr == WL_DR_START || dr == WL_DR_OVER ? i : WL_NOROW);
    if (dr == WL_DR_START) {
        KillTimer(iw_frame, IW_T_SETTLE);
        iw_settle = IW_SETTLE_NONE;
        hold();
        SetCursor(LoadCursor(iw_inst, MAKEINTRESOURCE(IW_R_DRAG)));
        LoadString(iw_inst, IW_S_DRAG, t, sizeof t);
        panel(IW_P_HELP, t);
    } else if (dr != WL_DR_OVER) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        panel(IW_P_HELP, NULL);
        if (dr == WL_DR_DROP && i != WL_NOROW)
            drop(to);
        iw_settle = IW_SETTLE_CHECK;
    }
}

/* The record key of the virtual key vk as it was typed, with SHIFT and
   CTRL. */
static u16 typed(UINT vk)
{
    return (u16)(vk + (GetKeyState(VK_SHIFT) < 0 ? IW_M_SHIFT : 0) +
                 (GetKeyState(VK_CONTROL) < 0 ? IW_CTRL : 0));
}

/* The lists' notes, arg the pane: a move holds the frame busy, the pane
   noted as the one moving (iw_move); its last one retitles the frame for
   a new order of the first pane, fills the Record menu again once the
   focused list has a row selected where none was, or none where one
   was, and has its count shown once the move is over (moved), and on
   the master of the split view has the main loop check whether its pane
   is to follow (settle); a failure that leaves the list leaves as its
   action says; a width a drag kept is saved (cols_moved); a row's drag
   is followed (drag); the rows a choice of all has counted show on the
   help panel (IW_S_COLLECT); and a key, from the list with the focus,
   which becomes the focused one, is a record key, as it was typed
   (typed, rec_key), while one may start (idle), and is dropped
   otherwise. */
static void list_note(void *arg, wl *v, int ev, UINT val)
{
    char key[LV_KEYSZ];
    iw_pane *p;

    (void)v;
    p = (iw_pane *)arg;
    if (ev == WL_BUSY) {
        if (val)
            iw_move = p;
        busy(iw_frame, (int)val);
        if (!val && p->list != NULL) {
            if (p == iw_panes && wl_order(p->list) != iw_order)
                caption();
            if (p == iw_cur && wl_selected(p->list, key) != iw_rsel)
                rec_menu();
            if (p == iw_cur)
                iw_moved = 1;
            if (p == iw_panes && pane_shown() &&
                iw_settle != IW_SETTLE_DUE)
                iw_settle = IW_SETTLE_CHECK;
        }
        return;
    }
    if (ev == WL_LEAVE) {
        leave((u8)val);
        return;
    }
    if (ev == WL_COLS) {
        cols_moved((u8)val);
        return;
    }
    if (ev == WL_DRAG) {
        drag(p, val);
        return;
    }
    if (ev == WL_SEL) {
        if (p == iw_cur)
            iw_moved = 1;
        return;
    }
    if (ev == WL_COUNT) {
        help_n(IW_S_COLLECT, (u16)val, 0, 0);
        return;
    }
    if (!idle())
        return;
    if (p != iw_cur)
        pane_use(p);
    rec_key(typed(val));
}

/* Destroy the lists, the splitter and the preview panel, if any, the
   pane's load to come dropped, the first pane focused. */
static void list_close(void)
{
    iw_pane *p;

    for (p = iw_panes; p < iw_panes + IW_NPANES; p++) {
        if (p->list != NULL)
            DestroyWindow(wl_hwnd(p->list));
        p->list = NULL;
    }
    if (iw_splitter != NULL)
        DestroyWindow(iw_splitter);
    iw_splitter = NULL;
    if (iw_view != NULL)
        DestroyWindow(iw_view);
    iw_view = NULL;
    iw_inview = 0;
    KillTimer(iw_frame, IW_T_SETTLE);
    iw_settle = IW_SETTLE_NONE;
    hold();
    iw_cur = iw_panes;
    iw_fit = 0;
}

/* Open pane i's list over screen s, of control ID id, in place, in the
   columns of the screen or of its view (WS_VIEWS), with the rows that
   fit its part of the room r (pane_rect, x the master's width in the
   split view, else -1) and, when the first order's row is wider than
   that, its horizontal bar (wl_rows), counted (WL_EXACT) as wp_flags
   says, its rows dragged out (WL_DRAGS) for the pane beside a screen of
   locations (WS_HOLDS) while the user may move items (TUI_LVL_OPER, as
   Move is grayed below it); the pane's scope its source's arg, or its
   first word without args of its own, a category's items' its CAT=
   (cat_args).  A wl_open code. */
static int pane_open(int i, const ws_scr *s, UINT id, int x, const RECT *r)
{
    iw_pane *p;
    RECT b;

    p = &iw_panes[i];
    p->scr = s;
    memcpy(&p->src, s->src, sizeof p->src);
    p->src.fmt = s->fmt;
    p->cols = s->cols;
    if (s->flags & WS_VIEWS)
        p->cols = wloan_view(&p->src);
    if ((s->flags & WS_SCOPED) && p->src.args == NULL)
        p->src.args = s == &ws_catitems ? cat_args : scope_args;
    p->src.arg = p->scope;
    pane_rect(i, x, r, &b);
    p->ps = wl_rows(iw_frame, b.bottom - b.top, b.right - b.left, p->cols);
    return wl_open(&p->list, iw_frame, id, iw_l, &p->src, p->cols, p->ps,
                   iw_level, (u8)(wp_flags | WL_SIZES |
                                  (i == 1 && iw_level >= TUI_LVL_OPER &&
                                   (iw_panes[0].scr->flags & WS_HOLDS) ?
                                   WL_DRAGS : 0) |
                                  (several(s) ? WL_MULTI : 0)),
                   list_note, p);
}

/* Open the list of screen s, in place of any, in the order named order
   (the first when it names none), or in its view (WS_VIEWS), between
   the tool bar and the status bar (list_room), and with the screen's
   pane (pane_of) the splitter and the pane's list, or its preview panel
   (WPREV.H), beside it, disabled until it loads (pane_load), its rows
   counted (WL_EXACT) on the links
   whose requests are fast, LOCAL, NET and TCP, letters seeking a tree's
   rows (wl_letters), the categories' list paging its command's response
   itself (wl_unpaged), fill the Record menu and title the frame for it
   and give it the focus while the frame is active, fetching nothing: 1,
   or 0 when it does not open, which is said (fail), the menu's entries
   grayed and the status bar without a count. */
static int list_make(const ws_scr *s, const char *order)
{
    const ws_pane *d;
    RECT r;
    int lo;
    int hi;
    int x;
    int rc;

    list_close();
    iw_exact = (u8)(iw_kind != IW_S_COM && iw_kind != IW_S_MBOX);
    wp_flags = (u8)(iw_exact ? WL_EXACT : 0);
    d = pane_of(s);
    /* The categories stay held from one screen of items or parts to the
       next, a pane's among them, shown or not, as ENTER opens its screen
       from the master alone, and go with the first screen of other rows
       alone. */
    if (has_cats(s) || (s->pane != NULL && s->pane->detail != NULL &&
                        has_cats(s->pane->detail)))
        cats_hold(iw_frame);
    else
        ct_close(&iw_cats);
    wh_topic(s->topic);
    if (!goes_back(s))
        iw_nback = 0;
    if (!(s->flags & WS_SCOPED)) {
        iw_panes[0].scope[0] = '\0';
        iw_sname[0] = '\0';
    }
    list_room(iw_frame, &r);
    x = d != NULL ? split_at(iw_frame, d, r.right, &lo, &hi) : -1;
    rc = pane_open(0, s, IW_C_LIST, x, &r);
    if (rc == PG_OK && d != NULL) {
        iw_splitter = wsp_create(iw_frame, IW_C_SPLIT);
        if (iw_splitter == NULL) {
            rc = WL_EWIN;
        } else if (d->detail != NULL) {
            rc = pane_open(1, d->detail, IW_C_CLIST, x, &r);
        } else {
            iw_view = wpv_create(iw_frame, IW_C_PREVIEW);
            if (iw_view == NULL)
                rc = WL_EWIN;
        }
    }
    if (rc != PG_OK)
        list_close();
    rec_menu();
    if (rc != PG_OK) {
        caption();
        status();
        fail(iw_frame, IW_S_NOLIST, NULL, NULL, rc);
        return 0;
    }
    wl_set_order(iw_panes[0].list, order);
    if (s->flags & WS_TREE)
        wl_letters(iw_panes[0].list);
    if (s == &cat_scr)
        wl_unpaged(iw_panes[0].list);
    if (d != NULL)
        EnableWindow(pane_wnd(1), FALSE);
    caption();
    place(iw_frame);
    if (GetActiveWindow() == iw_frame)
        SetFocus(wl_hwnd(iw_panes[0].list));
    return 1;
}

/* The pane beside the master shows no row's: none shown (iw_shown), no
   scope. */
static void pane_reset(void)
{
    iw_shown[0] = '\0';
    iw_panes[1].scope[0] = '\0';
}

/* 1 while list v's selected row is the row of key. */
static int list_on(wl *v, const char *key)
{
    char row[LV_KEYSZ];

    return wl_selected(v, row) && strcmp(row, key) == 0;
}

/* Load the first page of pane p's list, just opened, and select the row
   of key sel, or the first row: with sel NULL or "", and whenever that
   row is not reached, so that the list never stands on a third row.  On
   a link whose requests are fast (iw_exact), and on a tree on any, the
   row is sought from the start (wl_find), over LV_SEEKMAX rows at most,
   a page's request for each page passed, and the first page fetched
   once more when it is not among them, is gone or an Esc stopped the
   seek.  On the other links the one request of the first page is all:
   the row is looked for among the rows shown, by moves that fetch
   nothing. */
static void list_seek(iw_pane *p, const char *sel)
{
    int none;
    int n;
    int i;

    none = sel == NULL || sel[0] == '\0';
    if (none || (!iw_exact && !(p->scr->flags & WS_TREE))) {
        if (wl_move(p->list, PG_HOME) != PG_OK || none)
            return;
        n = (int)SendMessage(GetDlgItem(wl_hwnd(p->list), WL_BOXID),
                             LB_GETCOUNT, 0, 0L);
        for (i = 1; i < n && !list_on(p->list, sel); i++)
            wl_move(p->list, PG_DOWN);
        if (!list_on(p->list, sel))
            for (; i > 1; i--)
                wl_move(p->list, PG_UP);
    } else if (wl_find(p->list, sel) == PG_OK && !list_on(p->list, sel)) {
        wl_move(p->list, PG_HOME);
    }
}

/* Open the list of screen s as list_make does and load its first page,
   the first row selected, or the row of key sel where list_seek reaches
   it, then the pane beside it (pane_load), which showed none
   (pane_reset). */
static void list_open(const ws_scr *s, const char *order, const char *sel)
{
    pane_reset();
    if (!list_make(s, order))
        return;
    list_seek(iw_panes, sel);
    if (pane_shown())
        pane_load(1);
    moved();
}

/* The pane beside a master left without rows (the tree's last location
   deleted) opened again, empty and disabled, showing no row's
   (pane_reset), as the split view opens on an empty master, the master
   given the focus; the split view closed, the frame titled and its View
   menu marked for no list (caption), said as list_make says, when it
   does not open.  A preview panel is emptied and disabled as it
   stands. */
static void pane_clear(void)
{
    const ws_pane *d;
    RECT r;
    int lo;
    int hi;
    int rc;

    if (pane_at() != 0)
        pane_focus(0);
    if (iw_view != NULL) {
        pane_reset();
        wpv_text(iw_view, 0, NULL);
        EnableWindow(iw_view, FALSE);
        rec_menu();
        return;
    }
    DestroyWindow(wl_hwnd(iw_panes[1].list));
    pane_reset();
    d = iw_panes[0].scr->pane;
    list_room(iw_frame, &r);
    rc = pane_open(1, d->detail, IW_C_CLIST,
                   split_at(iw_frame, d, r.right, &lo, &hi), &r);
    if (rc != PG_OK) {
        list_close();
        caption();
        fail(iw_frame, IW_S_NOLIST, NULL, NULL, rc);
    } else {
        EnableWindow(wl_hwnd(iw_panes[1].list), FALSE);
        place(iw_frame);
    }
    rec_menu();
}

/* After the master of the split view may have moved, in the main loop:
   while its row (pane_row) is not the one its pane shows (iw_shown), the
   pane's load set to come once IW_SETTLE ms, or on a link whose
   requests are slow IW_SETTLE_SLOW, have passed without another move
   (WM_TIMER, iw_settle), at once when Windows has no timer to give;
   else none to come, and with no row left the pane cleared
   (pane_clear).  Windows 3.0's USER runs every timer from one system
   timer, which SetTimer starts before it enters the new timer in its
   table: a tick in between finds no timer and stops the system timer,
   so that a timer set while no other runs may never fire, and the pane
   would wait for the master's next move.  A second timer (IW_T_KICK)
   set and killed at once counts the first and starts the system timer
   again. */
static void settle(void)
{
    char key[LV_KEYSZ];
    UINT ms;
    int sel;

    sel = pane_shown() && pane_row(key, NULL);
    if (sel && strcmp(key, iw_shown) != 0) {
        ms = iw_exact ? IW_SETTLE : IW_SETTLE_SLOW;
        iw_settle = (u8)(SetTimer(iw_frame, IW_T_SETTLE, ms, NULL) ?
                         IW_SETTLE_ARMED : IW_SETTLE_DUE);
        if (iw_settle == IW_SETTLE_ARMED &&
            SetTimer(iw_frame, IW_T_KICK, ms, NULL))
            KillTimer(iw_frame, IW_T_KICK);
    } else {
        KillTimer(iw_frame, IW_T_SETTLE);
        iw_settle = IW_SETTLE_NONE;
        if (!sel && pane_shown() && IsWindowEnabled(pane_wnd(1)))
            pane_clear();
    }
    hold();
}

/* Open the screen shown again for the rows that now fit the frame
   (iw_fit), in its order, each list's count carried over (wl_count_set)
   rather than counted again, from its first page, but for the list of a
   screen that has a pane, shown or not, which seeks the row it had
   selected, so that its pane stays (pane_load) and ENTER still opens
   that row; the focus in the pane that had it. */
static void refit(void)
{
    char key[LV_KEYSZ];
    u32 n[IW_NPANES];
    int known[IW_NPANES];
    iw_pane *p;
    int sel;
    int cur;
    int i;

    p = iw_panes;
    for (i = 0; i < IW_NPANES; i++)
        known[i] = p[i].list != NULL && wl_count(p[i].list, &n[i]);
    sel = wl_selected(p->list, key);
    cur = pane_at();
    if (!list_make(p->scr, p->src.orders[wl_order(p->list)]))
        return;
    for (i = 0; i < IW_NPANES; i++)
        if (known[i] && p[i].list != NULL)
            wl_count_set(p[i].list, n[i]);
    list_seek(p, p->scr->pane != NULL && sel ? key : NULL);
    if (pane_shown()) {
        pane_load(1);
        if (cur != 0 && IsWindowEnabled(pane_wnd(cur)))
            pane_focus(cur);
    }
}

/* ---------------------------------------------------------------------
 * Commands.
 * --------------------------------------------------------------------- */

/* Drop the list and the menu bar, gray the tool bar and empty its Find
   box, so that no user sees the last one's search, end the login on the
   session when out is 1 (HELLO again), then run the login dialog, and
   the forced change when LOGIN asks for it, until a user is logged in,
   and give the frame the user's menu bar and the item list in the order
   of ITEMORD=; end the program instead when the user or the session ends
   it. */
static void log_in(int out)
{
    HMENU m;
    int r;

    list_close();
    caption();
    m = GetMenu(iw_frame);
    SetMenu(iw_frame, NULL);
    if (m != NULL)
        DestroyMenu(m);
    tools_on(0);
    SetWindowText(iw_find, k_empty);
    for (;;) {
        iw_level = 0;
        status();
        if (out && !hello())
            break;
        out = 1;
        wh_topic("LOGIN");
        r = dialog(IW_R_LOGIN, (FARPROC)login_proc, 0L);
        if (r == IW_D_MUSTCHG) {
            wh_topic(k_passwd);
            r = dialog(IW_R_PASSWD, (FARPROC)passwd_proc, 1L);
            if (r == IW_D_CANCEL)
                r = IW_D_LOGIN;
        }
        if (r == IW_D_IN) {
            menu_on();
            tools_on(1);
            status();
            list_open(&item_scr, iw_itemord, NULL);
            return;
        }
        if (r != IW_D_LOGIN)
            break;
    }
    quit();
}

/* Run the settings dialog (WSET.H) over the frame; a new item list order
   applies at once, the item list shown being opened again in it. */
static void settings(void)
{
    char ord[CFG_ORDLEN + 1];
    int r;

    r = wset_run(iw_frame, iw_cfgpath, iw_version, cfg_defdir, ord);
    ended(r);
    if (r != WF_OK || strcmp(ord, iw_itemord) == 0)
        return;
    strcpy(iw_itemord, ord);
    if (iw_panes[0].list != NULL && iw_panes[0].scr == &item_scr)
        list_open(&item_scr, iw_itemord, NULL);
}

/* 1 while the categories' screen shows. */
static int cats_shown(void)
{
    return iw_panes[0].list != NULL && iw_panes[0].scr == &cat_scr;
}

/* Show code list kind (WCODE.H), then fetch every list shown again
   (after_all), the tree with the type names loaded again, which the
   location types' list may have changed, the categories held with
   theirs, the frame titled again, as a category's items are with its
   name, and the categories' own list counted again and, once fetched,
   kept on the category it had selected, if any (master_keep), as
   theirs may have added, deleted or moved rows of it. */
static void codes(u8 kind)
{
    char key[LV_KEYSZ];
    u8 act;

    key[0] = '\0';
    if (kind == SCR_CK_CAT && cats_shown())
        pane_row(key, NULL);
    act = wcode_run(iw_frame, iw_l, iw_level, kind);
    if (act == WC_NODIALOG) {
        fail(iw_frame, IW_S_NOFORM, NULL, NULL, 0);
        return;
    }
    if (act == RQE_STAY && iw_panes[0].list != NULL &&
        iw_panes[0].scr == &wloc_scr)
        wloc_types(iw_frame, iw_l, &act);
    if (act == RQE_STAY && kind == SCR_CK_CAT) {
        cats_fresh();
        caption();
        if (cats_shown())
            wl_recount(iw_panes[0].list);
    }
    if (after_all(act) && key[0] != '\0' && cats_shown())
        master_keep(key);
}

/* Carry out the command id of the menu bar; the dialogs of no screen,
   the password's, the operations', the sessions', the exports', the
   imports' and the reports', are given their help topics while they
   run, as the help of Getting started is. */
static void command(WPARAM id)
{
    const char *old;
    int r;
    u8 act;

    switch (id) {
    case IW_M_HELP:
        wh_show(iw_frame, NULL);
        break;
    case IW_M_START:
        old = wh_topic(k_start);
        wh_show(iw_frame, NULL);
        wh_topic(old);
        break;
    case IW_M_KEYS:
        wh_show(iw_frame, WH_HELPKEYS);
        break;
    case IW_M_ABOUT:
        dialog(IW_R_ABOUT, (FARPROC)about_proc, 0L);
        break;
    case IW_M_FIND:
        find_run();
        break;
    case IW_M_PANE:
        pane_toggle();
        break;
    case SCMD_SETTINGS:
        settings();
        break;
    case SCMD_PASSWD:
        old = wh_topic(k_passwd);
        r = dialog(IW_R_PASSWD, (FARPROC)passwd_proc, 0L);
        wh_topic(old);
        if (r == IW_D_LOGIN)
            log_in(1);
        else if (r == IW_D_QUIT)
            quit();
        break;
    case SCMD_ITEMS_ID:
    case SCMD_ITEMS_LOC:
    case SCMD_ITEMS_MFR:
    case SCMD_ITEMS_SER:
        list_open(&item_scr, itl_src.orders[id - SCMD_ITEMS_ID], NULL);
        break;
    case IW_M_BYCAT:
        list_open(&cat_scr, NULL, NULL);
        break;
    case SCMD_ITEM_NEW:
        rec_new(&item_scr, k_empty, 0);
        break;
    case SCMD_ASSY:
        list_open(&assy_scr, NULL, NULL);
        break;
    case SCMD_LOCS:
        tree_open(NULL);
        break;
    case SCMD_LOC_NEW:
        if (pane_shown())
            pane_focus(0);
        rec_new(&wloc_scr, k_empty, 0);
        break;
    case SCMD_CATS:
    case SCMD_LTYPES:
        codes(id == SCMD_CATS ? SCR_CK_CAT : SCR_CK_LTYPE);
        break;
    case SCMD_MAINT:
        list_open(&wsvc_recent, NULL, NULL);
        break;
    case SCMD_LOANS:
        wloan_shown = SCR_LV_OPEN;
        list_open(&wloan_list, NULL, NULL);
        break;
    case SCMD_FIND:
    case SCMD_SERIAL:
    case SCMD_FILTER:
        search((u8)id);
        break;
    case SCMD_USERS:
        admin_open(&wuser_scr);
        break;
    case SCMD_BACKUP:
    case SCMD_RESTORE:
    case SCMD_PACK:
    case SCMD_REINDEX:
        old = wh_topic(scr_adm_w_admin);
        act = wadm_op(iw_frame, (u8)id, &r);
        wh_topic(old);
        /* A restore may remove the records the screens to go back to
           are of, so none is kept. */
        if (r && id == SCMD_RESTORE) {
            iw_nback = 0;
            recount();
        }
        after_all(act);
        break;
    case SCMD_AUDIT:
        audit();
        break;
    case SCMD_SESSIONS:
        old = wh_topic("SESSIONS");
        act = wadm_sessions(iw_frame);
        wh_topic(old);
        leave(act);
        break;
    case SCMD_EXPORT:
    case SCMD_IMPORT:
        old = wh_topic(id == SCMD_EXPORT ? scr_csv_w_export
                                         : scr_csv_w_import);
        r = wcsv_run(iw_frame, iw_l, iw_level, iw_kind == IW_S_LOCAL,
                     (u8)id);
        wh_topic(old);
        if (r == WF_OK && id == SCMD_IMPORT)
            recount();
        after_all(ended(r));
        break;
    case SCMD_RPT_LIST:
    case SCMD_RPT_VALUE:
    case SCMD_RPT_LOANS:
    case SCMD_LBL_ITEM:
    case SCMD_LBL_BIN:
        old = wh_topic(id < SCMD_LBL_ITEM ? scr_rpt_k_reports
                                          : scr_rpt_k_labels);
        act = ended(wrpt_run(iw_frame, iw_l, iw_level, (u8)id, &iw_geom,
                             iw_station));
        wh_topic(old);
        leave(act);
        break;
    case TUI_CMD_LOGIN:
        log_in(1);
        break;
    case TUI_CMD_QUIT:
        quit();
        break;
    default:
        if (id >= IW_M_KEY && iw_cur->list != NULL)
            rec_key((u16)(id - IW_M_KEY));
        break;
    }
}

/* The number of the text the menu entry of command id shows (its help,
   IW_H_MENU plus the number): the message of its table entry, a string
   of INVWIN.RC for Exit, Browse by category, the Help and View menus'
   entries and the Record menu's (rec_text, which gives Refresh's too);
   0 for none. */
static UINT entry_text(UINT id)
{
    const tui_menu *mn;
    const tui_item *it;
    int op;

    switch (id) {
    case TUI_CMD_QUIT:
        return IW_S_MEXIT;
    case IW_M_HELP:
        return IW_S_MHELPON;
    case IW_M_START:
        return IW_S_MSTART;
    case IW_M_KEYS:
        return IW_S_MKEYS;
    case IW_M_ABOUT:
        return IW_S_MABOUT;
    case IW_M_PANE:
        return IW_S_VPANE;
    case IW_M_BYCAT:
        return IW_S_MBYCAT;
    default:
        break;
    }
    if (id >= IW_M_KEY)
        return rec_text((u16)(id - IW_M_KEY), &op);
    for (mn = invmenu_bar; mn < invmenu_bar + INVMENU_COUNT; mn++)
        for (it = mn->items; it < mn->items + mn->nitems; it++)
            if (it->cmd == id)
                return it->label;
    return 0;
}

/* Show on the help panel the help of the menu entry of command id that
   WM_MENUSELECT's flags fl say is highlighted, none for a menu's title,
   a separator, the system menu's entries or the menus closed. */
static void menu_help(UINT id, UINT fl)
{
    char t[IW_H_MAXLEN + 1];
    UINT n;

    n = (fl & (MF_POPUP | MF_SEPARATOR | MF_SYSMENU)) ? 0 : entry_text(id);
    if (n == 0 || LoadString(iw_inst, IW_H_MENU + n, t, sizeof t) == 0)
        t[0] = '\0';
    panel(IW_P_HELP, t);
}

/* Write what the settings hold for the pane d (iw_wpane) to the
   settings file, its WINPANE. line, the locations' width as WINSPLIT=,
   only when the file reads, so that no default is written over a file
   that could not be read; a failure is not said, the values holding for
   the session. */
static void pane_save(const ws_pane *d)
{
    const u8 *v;
    cfg c;

    if (cfg_load(&c, iw_cfgpath) != 0)
        return;
    v = iw_wpane[d->cfg];
    c.pane[d->cfg][CFG_PANE_ON] = v[CFG_PANE_ON];
    if (d->cfg == CFG_P_LOC)
        c.split = v[CFG_PANE_W];
    else
        c.pane[d->cfg][CFG_PANE_W] = v[CFG_PANE_W];
    cfg_save(&c, iw_cfgpath);
}

/* After a drag of the splitter: the master as wide as it was left, in
   average characters (iw_wpane), never 0, which leaves the width to the
   frame, the lists laid out again, and the width kept (pane_save), the
   locations' as WINSPLIT= and another screen's in its WINPANE. line,
   which then says that the pane shows, as it does. */
static void split_moved(void)
{
    const ws_pane *d;
    RECT r;
    u8 *v;

    d = iw_panes[0].scr->pane;
    v = iw_wpane[d->cfg];
    GetWindowRect(iw_splitter, &r);
    ScreenToClient(iw_frame, (POINT FAR *)&r.left);
    v[CFG_PANE_W] = (u8)max(1, min((r.left + iw_cw / 2) / iw_cw,
                                   CFG_SPLITMAX));
    if (d->cfg != CFG_P_LOC)
        v[CFG_PANE_ON] = 1;
    place(iw_frame);
    pane_save(d);
}

/* View > Pane beside the list: the screen shown opened again with its
   pane when it shows alone, else without it, in its order, the row it
   had selected sought, and the choice kept for the screen (iw_wpane,
   pane_save). */
static void pane_toggle(void)
{
    char key[LV_KEYSZ];
    const iw_pane *p;
    const ws_pane *d;
    int sel;

    p = iw_panes;
    d = p->scr->pane;
    iw_wpane[d->cfg][CFG_PANE_ON] = (u8)!pane_shown();
    pane_save(d);
    sel = wl_selected(p->list, key);
    list_open(p->scr, p->src.orders[wl_order(p->list)], sel ? key : NULL);
}

/* ---------------------------------------------------------------------
 * The frame.
 * --------------------------------------------------------------------- */

LRESULT WCALLBACK frame_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    switch (wm) {
    case WM_CREATE:
        return chrome(w) ? 0 : -1;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            place(w);
        return 0;
    case WM_ENABLE:
        if (!wp)
            cancel_drags();
        return 0;
    case WM_MENUSELECT:
        menu_help((UINT)wp, LOWORD(lp));
        return 0;
    case WM_SETFOCUS:
        if (iw_cur->list != NULL)
            SetFocus(pane_wnd(pane_at()));
        return 0;
    case WM_SETCURSOR:
        if (!iw_busy)
            break;
        SetCursor(LoadCursor(NULL, IDC_WAIT));
        return TRUE;
    case WM_COMMAND:
        if (wp == IW_C_SPLIT && HIWORD(lp) == WSPN_MOVED)
            split_moved();
        else if (iw_busy)
            return 0;
        else if (wp != IW_C_PREVIEW || HIWORD(lp) != WPVN_KEY)
            command(wp);
        else if (idle())
            rec_key(typed(LOWORD(lp)));
        return 0;
    case WM_TIMER:
        /* The pane's load only marked due: the main loop makes it, as
           this may come from a request's or a dialog's loop. */
        if (wp != IW_T_SETTLE)
            break;
        KillTimer(w, IW_T_SETTLE);
        if (iw_settle == IW_SETTLE_ARMED)
            iw_settle = IW_SETTLE_DUE;
        return 0;
    case IW_WM_POPUP:
        if (!iw_busy && iw_cur->list != NULL)
            TrackPopupMenu(iw_rmenu, 0, (int)LOWORD(lp), (int)HIWORD(lp), 0,
                           w, NULL);
        return 0;
    case WM_CLOSE:
        if (iw_busy)
            return 0;
        break;
    case WM_QUERYENDSESSION:
        return !iw_busy;
    case WM_ENDSESSION:
        /* Windows ends without returning to the main loop, and without
           destroying a window; form_wipe frees the form wf_wipe reads. */
        if (wp) {
            if (iw_pwdlg != NULL)
                wipe_pw(iw_pwdlg);
            wf_wipe();
            form_wipe();
            close_link();
        }
        return 0;
    case WM_DESTROY:
        /* Windows Help is closed while the window that asked for it
           still is one; the lists, the splitter and the preview panel go
           with their parent, its timer with it. */
        wh_done();
        iw_frame = NULL;
        iw_panes[0].list = NULL;
        iw_panes[1].list = NULL;
        iw_splitter = NULL;
        iw_view = NULL;
        iw_settle = IW_SETTLE_NONE;
        iw_fit = 0;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(w, wm, wp, lp);
}

/* Register the frame's class and make the frame, shown as show says,
   clipping its children, as the parent of a list window must (WLIST.H);
   0 when Windows refuses. */
static int make_frame(int show)
{
    char title[IW_FMTSZ];
    WNDCLASS wc;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = frame_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = iw_inst;
    wc.hIcon = LoadIcon(iw_inst, k_class);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = k_class;
    if (RegisterClass(&wc) == 0)
        return 0;
    LoadString(iw_inst, IW_S_TITLE, title, sizeof title);
    iw_frame = CreateWindow(k_class, title,
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                            NULL, NULL, iw_inst, NULL);
    if (iw_frame == NULL)
        return 0;
    ShowWindow(iw_frame, show);
    UpdateWindow(iw_frame);
    return 1;
}

/* ---------------------------------------------------------------------
 * Start and end.
 * --------------------------------------------------------------------- */

/* args_parse's function: the program's own switches, each a name of
   own_names and a value after its colon, into the iw_opts arg. */
static int own_switch(void *arg, const char *s)
{
    iw_opts *o;
    u16 n;
    int i;

    o = (iw_opts *)arg;
    for (i = 0; i < IW_NOWN; i++) {
        n = (u16)strlen(own_names[i]);
        if (txt_ieqn(s, own_names[i], n) && s[n] != '\0' &&
            o->own[i] == NULL) {
            o->own[i] = s + n;
            return ARGS_OK;
        }
    }
    return ARGS_EBAD;
}

/* Split line at its blanks and TABs, each word ended by a NUL in place,
   into argv[1] on, argv holding IW_MAXARGV; argv[0], the program's,
   which args_parse does not read, is NULL.  Returns the count with
   argv[0]. */
static int split(char *line, char **argv)
{
    int n;

    argv[0] = NULL;
    n = 1;
    for (;;) {
        while (*line == ' ' || *line == '\t')
            *line++ = '\0';
        if (*line == '\0')
            return n;
        argv[n++] = line;
        while (*line != '\0' && *line != ' ' && *line != '\t')
            line++;
    }
}

/* Read the messages and the settings, make the frame, open the link,
   log in and run the frame's messages until it is closed, then close
   the link; a failure on the way is said (fail), a settings file that
   cannot be read with what its DOS error means (rqe_doserr).  The
   settings' WINCOLS widths are kept for the run in a far block, the
   lists' widths store (wcol_keep); without one every list keeps its
   own widths.  Their WINPANE values are kept in iw_wpane, WINSPLIT= as
   the width of the locations'. */
static void run(const iw_opts *o, int show)
{
    char path[DOS_PATHMAX];
    char progdir[DOS_PATHMAX];
    char doserr[MSG_MAXLEN + 1];
    const char *why;
    const char *dir;
    const char *target;
    cmem_blk *widths;
    cfg c;
    MSG m;
    int rc;

    GetModuleFileName(iw_inst, path, sizeof path);
    AnsiToOem(path, path);
    txt_path_dir(progdir, (u16)sizeof progdir, path);
    if (msg_load(progdir, MSG_FILE) != 0) {
        why = msg_why();
        OemToAnsi(why, path);
        fail(NULL, IW_S_NOMSG, MSG_FILE, path, 0);
        return;
    }
    iw_cfgpath = o->a.cfg != NULL ? o->a.cfg : CFG_NAME;
    rc = cfg_load(&c, iw_cfgpath);
    if (rc != 0) {
        OemToAnsi(iw_cfgpath, path);
        OemToAnsi(rqe_doserr(rc), doserr);
        fail(NULL, IW_S_NOCFG, path, doserr, 0);
        msg_free();
        return;
    }
    dir = o->a.dir != NULL ? o->a.dir :
          c.data[0] != '\0' ? c.data : cfg_defdir;
    iw_kind = IW_S_LOCAL;
    target = dir;
    if (o->a.kind == ARGS_NET) {
        iw_kind = IW_S_NET;
        target = o->a.net;
    } else if (o->a.kind == ARGS_COM) {
        iw_kind = IW_S_COM;
        target = o->a.comname;
    } else if (o->a.kind == ARGS_TCP) {
        iw_kind = IW_S_TCP;
        target = o->a.tcp;
    } else if (o->a.kind == ARGS_MBOX) {
        iw_kind = IW_S_MBOX;
        target = o->a.mbox;
        iw_mbox = target;
    }
    txt_copy(iw_target, target, (u16)sizeof iw_target);
    OemToAnsi(iw_target, iw_target);
    strcpy(iw_user, c.user);
    strcpy(iw_itemord, c.itemord);
    memcpy(iw_wpane, c.pane, sizeof iw_wpane);
    iw_wpane[CFG_P_LOC][CFG_PANE_W] = c.split;
    widths = cmem_alloc((u16)sizeof c.cols);
    if (widths != NULL) {
        cmem_put(widths, 0, c.cols, (u16)sizeof c.cols);
        wcol_keep(widths);
    }
    iw_geom.pagelen = c.pagelen;
    iw_geom.labelw = c.labelw;
    iw_geom.labelh = c.labelh;
    if (!make_frame(show)) {
        fail(NULL, IW_S_NOFRAME, NULL, NULL, 0);
    } else {
        dos_crit_install();
        dos_yield = yield;
        wh_init(iw_inst, iw_frame, progdir);
        wf_help(wh_show);
        status();
        if (start(o, &c, dir))
            log_in(0);
        else
            quit();
        while (GetMessage(&m, NULL, 0, 0)) {
            if (iw_noted && (m.message == WM_KEYDOWN ||
                             m.message == WM_SYSKEYDOWN ||
                             m.message == WM_LBUTTONDOWN ||
                             m.message == WM_RBUTTONDOWN ||
                             m.message == WM_NCLBUTTONDOWN)) {
                iw_noted = 0;
                panel(IW_P_HELP, NULL);
            }
            if (!find_key(&m) && !wh_key(&m) &&
                !popup_input(&m)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
            pane_follow();
            if (iw_settle == IW_SETTLE_CHECK ||
                (m.message >= WM_USER && pane_shown() &&
                 in_pane(iw_panes, m.hwnd)))
                settle();
            if (iw_fit)
                refit();
            if (iw_settle == IW_SETTLE_DUE && GetCapture() == NULL)
                pane_load(0);
            moved();
        }
        rc = close_link();
        wloc_free();
        dos_yield = NULL;
        dos_crit_remove();
        if (rc != 0)
            fail(NULL, IW_S_NOCLOSE, NULL, NULL, rc);
    }
    wcol_keep(NULL);
    cmem_free(widths);
    msg_free();
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char line[IW_CMDMAX];
    char *argv[IW_MAXARGV];
    const char *w;
    iw_opts o;
    HWND first;
    int rc;
    int i;

    if (prev != NULL) {
        first = FindWindow(k_class, NULL);
        if (first != NULL) {
            if (IsIconic(first))
                ShowWindow(first, SW_RESTORE);
            BringWindowToTop(first);
        }
        return 0;
    }
    iw_inst = inst;
    dos_inst = (u16)inst;
    wrow_intl_read(inst);
    AnsiToOem(cmd, line);
    for (i = 0; i < IW_NOWN; i++)
        o.own[i] = NULL;
    rc = args_parse(&o.a, split(line, argv), argv, own_switch, &o, &w);
    td_init(inst, o.own[IW_O_KEYS], o.own[IW_O_SNAP]);
    td_place();
    if (rc == ARGS_OK &&
        (o.own[IW_O_SNAP] == NULL || o.own[IW_O_KEYS] != NULL))
        run(&o, show);
    else
        fail(NULL, IW_S_USAGE, NULL, NULL, 0);
    td_done();
    return 0;
}
