/*
 * TUI.H - screen primitives and the main loop of the full-screen
 * client.
 *
 * The screen is VID_ROWS x VID_COLS text cells:
 *
 *   row TUI_MENUROW            the menu bar
 *   rows TUI_TOP..TUI_BOTTOM   the work area of the current screen
 *   row TUI_HINTROW            key hints, or a message until the next key
 *   row TUI_STATROW            the status line: station, LOCAL or NET,
 *                              user name and access level
 *
 * Screens draw with logical attributes (TA_ roles), which the palette
 * turns into adapter attributes: a colour palette; a monochrome one
 * for mode 7 that keeps to the five attributes a monochrome display
 * shows distinctly; and the same monochrome scheme for a colour adapter
 * (mode 2, or MONO=1), where underline does not exist and 01h and 09h
 * become 07h and 0Fh.  Frames use the CP437 box characters (TF_).
 *
 * Texts come from the message file (MSG.H): the functions here that
 * take a string are given msg() results by their callers, and the
 * tables (menus, forms, lists) hold message numbers.  Every function
 * here that takes a text (a box title it keeps for redrawing, the text
 * of a message box, question, prompt or pick list) copies it into its
 * own buffer before it calls msg, msg_sub or tui_key, so a caller may
 * pass msg() results straight in.
 *
 * What a screen shows never depends on elapsed time, and nothing waits
 * by polling for a key: keys are read when needed, and the only look
 * ahead is tui_esc_waiting between the requests of a long operation (a
 * list fill or End walk, a report, an export), so a run from a key
 * script draws the same screens on every machine.  The one exception is
 * tui_run's idle hook, which watches the clock while key_read waits and
 * may end the program with a box when its PING finds the session ended;
 * a key script, whose keys never wait, does not reach it.
 *
 * Every key the screens read comes through tui_key, which flushes the
 * screen first and handles F1 (help for the current topic) and F9 (the
 * keys every screen has) itself.  Commands from the menu bar, Ctrl-F and
 * F3 are global: a screen that does not handle a key passes it to
 * tui_global, which may answer with a command for the main loop; one of
 * the bar's last menu, Record, comes back as a key (tui_record).
 *
 * The mouse's clicks come as keys too (KEY_CLICK, KEY_DCLICK; the right
 * button is Esc), and each loop acts on those that hit what it shows,
 * ignoring the rest.  The second press of a double click only chooses
 * in a list or pick list, after its first press selected the row there;
 * everywhere else it is ignored, so a double click on a menu entry or a
 * hint never reaches the screen that the first press opened.
 *
 * The session (link, user, level) is kept in tui_s, set by the login
 * screen.  Screens send requests through RQ.H on tui_s.l and nowhere
 * else.  Whether a menu entry or a key is allowed is decided against
 * tui_s.level here only to dim it and to refuse it early with a
 * message; the server checks the level of every request regardless.
 */
#ifndef TUI_H
#define TUI_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"

/* ---------------------------------------------------------------------
 * Messages of TUI.C that screens and tests use (MSG_R_TUI range; those
 * of failed requests are in RQERR.H).
 * --------------------------------------------------------------------- */
#define MT_NOMEM        15      /* Not enough memory                    */
#define MT_YESNO        16      /* the answer keys of tui_confirm: YN   */
#define MT_QUIT         17      /* Quit RETROINV?                       */
#define MT_DISCARD      18      /* Discard the changes?                 */
#define MT_SAVEFIRST    19      /* Save with Enter or leave with Esc    */
#define MT_WORKING      20      /* Working... records: ^1               */
#define MT_NOCHOICE     21      /* Nothing to choose from               */
#define MT_NOHELP       22      /* No help: ^1                          */
#define MT_ERRTITLE     23      /* Error (message box title)            */
#define MT_HINTOK       24      /* Enter=OK                             */
#define MT_HINTPICK     29      /* hints of a pick list                 */
#define MT_REPLACE      33      /* Replace ^1? (a file)                 */
#define MT_PROG         42      /* the program's name: RETROINV         */

/* ---------------------------------------------------------------------
 * Layout.
 * --------------------------------------------------------------------- */
#define TUI_MENUROW     0
#define TUI_TOP         1
#define TUI_BOTTOM      (VID_ROWS - 3)
#define TUI_HINTROW     (VID_ROWS - 2)
#define TUI_STATROW     (VID_ROWS - 1)
#define TUI_WORKROWS    (TUI_BOTTOM - TUI_TOP + 1)

/* Status line fields: starting column and width of each. */
#define TUI_ST_STA      1       /* station                              */
#define TUI_ST_STAW     LINK_STALEN
#define TUI_ST_CONN     (TUI_ST_STA + TUI_ST_STAW + 2)  /* LOCAL or NET */
#define TUI_ST_CONNW    5
#define TUI_ST_USER     (TUI_ST_CONN + TUI_ST_CONNW + 2) /* "ADMIN 3"   */
#define TUI_ST_USERW    (LINK_USERLEN + 2)
#define TUI_ST_INFO     (TUI_ST_USER + TUI_ST_USERW + 2) /* screen text */
#define TUI_ST_INFOW    (VID_COLS - TUI_ST_INFO)

/* ---------------------------------------------------------------------
 * Attributes and frames.
 * --------------------------------------------------------------------- */
#define TA_NORMAL       0       /* work area text                       */
#define TA_BOLD         1       /* labels, emphasis, a selected value   */
#define TA_FRAME        2       /* box frames                           */
#define TA_TITLE        3       /* box titles, column headers           */
#define TA_SELECT       4       /* the selected list row or menu entry  */
#define TA_MENU         5       /* menu bar and pull-down entries       */
#define TA_MENUOFF      6       /* entries not allowed                  */
#define TA_MENUHOT      7       /* hot-key letter of an entry           */
#define TA_STATUS       8       /* status line                          */
#define TA_INPUT        9       /* an input field                       */
#define TA_INPUTON      10      /* the field being edited               */
#define TA_ERROR        11      /* error messages                       */
#define TA_HINT         12      /* key hints                            */
#define TA_HINTKEY      13      /* the key names in the hints           */
#define TA_ALERT        14      /* marked list rows (overdue loans)     */
#define TA_NROLES       15

/* Box styles. */
#define TB_SINGLE       0
#define TB_DOUBLE       1

/* CP437 frame characters. */
#define TF_SH           ((char)0xC4)    /* single horizontal            */
#define TF_SV           ((char)0xB3)    /* single vertical              */
#define TF_STL          ((char)0xDA)    /* single corners               */
#define TF_STR          ((char)0xBF)
#define TF_SBL          ((char)0xC0)
#define TF_SBR          ((char)0xD9)
#define TF_DH           ((char)0xCD)    /* double horizontal            */
#define TF_DV           ((char)0xBA)    /* double vertical              */
#define TF_DTL          ((char)0xC9)    /* double corners               */
#define TF_DTR          ((char)0xBB)
#define TF_DBL          ((char)0xC8)
#define TF_DBR          ((char)0xBC)
#define TF_MARK         ((char)0x10)    /* right-pointing triangle      */

/* Palettes. */
#define TP_COLOR        0       /* colour                               */
#define TP_MONO         1       /* mode 7 monochrome                    */
#define TP_MONOCGA      2       /* mono scheme on a colour adapter      */

/* Select a palette (TP_).  Only the table is switched: what is on the
   screen keeps its attributes until the next screen draws itself. */
void tui_palette(u8 kind);

/* The palette in use (TP_). */
u8 tui_pal(void);

/* The adapter attribute of a role in the current palette. */
u8 tui_attr(u8 role);

/* ---------------------------------------------------------------------
 * Drawing.  Text is cut at the right edge; bytes 00h, 0Ah, 0Dh and 1Ah
 * are drawn as '.', so a screen never holds them.
 * --------------------------------------------------------------------- */

/* Fill rows top..bottom with spaces of TA_NORMAL. */
void tui_clear(u8 top, u8 bottom);

/* s at row, col; tui_textn writes exactly n cells, s cut or padded with
   spaces; tui_center centres s in columns col .. col + w - 1. */
void tui_text(u8 row, u8 col, const char *s, u8 role);
void tui_textn(u8 row, u8 col, const char *s, u16 n, u8 role);
void tui_center(u8 row, u8 col, u8 w, const char *s, u8 role);

/* A frame of h rows and w columns at top, left, its inside filled with
   spaces of TA_NORMAL; title (or NULL) is centred in the top edge. */
void tui_box(u8 top, u8 left, u8 h, u8 w, u8 style, const char *title);

/* Key hints on TUI_HINTROW (the text is copied, and redrawn after
   messages), from a message such as "Enter=Open 2Del=Delete Esc=Back":
   words are separated by single spaces and hold none themselves; the
   part of each word before '=' is drawn TA_HINTKEY.  A word that
   starts with a digit 1..3 names the access level its key needs; the
   digit is not shown, and when the user's level is lower the word is
   drawn TA_MENUOFF. */
void tui_hint(const char *s);

/* As tui_hint, but while a tui_msg message is up the new hints are only
   kept, and drawn when the message goes at the next key. */
void tui_hint_keep(const char *s);

/* The hint text as tui_hint was given it, in TUI_HINTBUF bytes: a row
   of words plus their level digits, which take no cell (at most
   TUI_HINTLVL of them). */
#define TUI_HINTLVL     8
#define TUI_HINTBUF     (VID_COLS + TUI_HINTLVL + 1)

/* A message on TUI_HINTROW, drawn TA_ERROR when err is 1, which stays
   until the next key is read; the hints come back after it. */
void tui_msg(const char *s, int err);

/* Redraw the menu bar and the status line. */
void tui_frame(void);

/* Text for the information field of the status line ("" clears it);
   copied. */
void tui_info(const char *s);

/* The text of the information field, as tui_info last set it. */
const char *tui_info_text(void);

/* ---------------------------------------------------------------------
 * Keys and dialogs.
 * --------------------------------------------------------------------- */

/* Flush the screen and read a key (a Record menu verb's at once).  A
   click on a word of the hints (not on a tui_msg message) stands for the
   key the word names before its '=' (tui_keyname), or, when that is
   several names joined by '/' ("PgUp/PgDn"), for the one clicked, the
   first when the click is past the '='.  F1 shows the help for the
   current topic and F9 the help's section of the keys; both are
   returned only inside the help box.  Ctrl-Break (KEY_BREAK) is returned
   as KEY_ESC, after vid_invalidate, since DOS may have written ^C over
   the screen.  When the key source has ended (KEY_EOS) the program ends
   through the exit function, which does not return. */
u16 tui_key(void);

/* Read the keys typed ahead, without waiting for one: 1 when one of
   them was Esc or Ctrl-Break (after vid_invalidate for Ctrl-Break, as
   tui_key), and the keys before it are dropped while those after it
   stay; 0 when none was, all of them dropped.  A long operation calls
   it between its requests, so a key typed during it never hides the
   Esc behind it. */
int tui_esc_waiting(void);

#define TUI_HELP        "INV.HLP"

/* The help topic F1 shows: a section name of the help file (TUI_HELP),
   which must be a string literal or other static string: only the
   pointer is kept.  Returns the previous topic, so a dialog can put it
   back. */
const char *tui_topic(const char *topic);

/* The directory prefix (as LINK_MAXDIR, ending in a separator, or "")
   where the help file is looked for first; INV passes the directory of
   the program.  The current directory is tried next. */
void tui_set_helpdir(const char *dir);

/* The pop-up dialogs below (help, question, prompt, pick list; for the
   message box see tui_msgbox) and the menus save the cells they cover
   first; when that fails for lack of memory they show MT_NOMEM, read no
   key and give their cancel answer.

   Show the help section topic in a scrolling box until Esc or Enter;
   F9 there shows the section of the keys every screen has instead. */
void tui_help(const char *topic);

/* Draw a message box with title and text ('\n' breaks lines, long
   lines are wrapped at spaces), centred in the work area, and hide the
   cursor; nothing under it is saved, no hint is drawn and no key is
   read.  It fetches no message, so title and text may be msg()
   results.  tui_msgbox and tui_confirm draw their boxes this way. */
void tui_showbox(const char *title, const char *text);

/* The box of tui_showbox until Enter, Esc or a click, with the
   MT_HINTOK hint; it puts back what it covered and the hints before it.
   Without the memory to save those cells, text is shown as
   tui_msg(text, 1). */
void tui_msgbox(const char *title, const char *text);

/* A yes/no question; 1 for Y, 0 for N or Esc. */
int tui_confirm(const char *question);

/* Line editor flags. */
#define TI_NOECHO       0x01    /* show nothing: passwords              */
#define TI_UPPER        0x02    /* fold a-z to A-Z                      */
#define TI_DIGITS       0x04    /* digits only                          */
#define TI_PRINT        0x08    /* 21h..7Eh only (passwords)            */
/* Seen only by a file that defines TUI_SELECT before the include, for
   Watcom C 6.5's compiler memory, as with TUI_CLICKS below. */
#ifdef TUI_SELECT
#define TI_SELECT       0x10    /* the old value starts selected        */
#endif

/* Edit buf (at most max characters, NUL-terminated, holding its old
   value on entry) in dispw cells at row, col, scrolling sideways when
   longer; a full buf of at most dispw characters is shown whole, and at
   its end the cursor sits on the last character, which Del and
   overwriting then change, and Left moves to the one before it.  Keys:
   characters (20h..FFh but DEL, or as the flags narrow them), Left,
   Right, Home, End, Backspace, Del, Ins (insert or overwrite), Ctrl-Y
   (clear).  With TI_SELECT the old value is drawn TA_BOLD until
   the first key: a character, Backspace or Del then replaces it whole,
   and any other key edits it in place.  Returns the key that ended the
   edit: any other control code (Enter, Esc, Tab, Ctrl-S, ...) or any
   key from KEY_EXT up (Shift-Tab, Up, Down, PgUp, PgDn, a function key,
   a click, ...); buf holds the edited text either way. */
u16 tui_input(u8 row, u8 col, u8 dispw, char *buf, u16 max, u8 flags);

#define TUI_PROMPTMINW  10      /* field cells a prompt keeps at least  */

/* A box asking for one value: label, then an input line.  1 when ended
   with Enter, 0 with Esc.  When the label and the field do not fit the
   screen, the field narrows first, keeping at least TUI_PROMPTMINW
   cells (fewer only when max is smaller), and then the label is cut.
   A tui_msg message still up, such as why the last value was refused,
   stays until the next key (tui_hint_keep). */
int tui_prompt(const char *title, const char *label, char *buf, u16 max,
               u8 flags);

#define TUI_PICKMAX     LINK_MAXCODES
#define TUI_PICKROWS    (TUI_WORKROWS - 4)
#define TUI_PICKW       40

/* A pop-up choice among n entries (n <= TUI_PICKMAX), at most
   TUI_PICKROWS shown at once and scrolled with Up, Down, PgUp, PgDn,
   Home and End; a letter jumps to the next entry starting with it, a
   click selects the entry clicked and a double click chooses it.
   label(arg, i, buf) writes entry i's text, at most TUI_PICKW
   characters, into buf (TUI_PICKW + 1 bytes), so the entries can stay
   in a far block.  *sel is the entry selected first and the one chosen.
   1 when chosen, 0 with Esc.  *sel is clamped to 0..n-1 first; with n
   0 the function shows MT_NOCHOICE and returns 0.  head, when not NULL,
   is a row of column headings (at most TUI_PICKW characters, copied
   first, so msg() results may be passed) drawn above the entries. */
typedef void (*tui_label_fn)(void *arg, u16 i, char *buf);
int tui_pick(const char *title, const char *head, u16 n, tui_label_fn label,
             void *arg, u16 *sel);

/* tui_pick's box for a list the caller acts on, with the hints of
   message hint (kept under a message still up, tui_hint_keep): n may be
   0, which shows an empty box, and every key but the moves and letters
   ends it (Enter, Esc, Ins, Ctrl-E, a click outside the entries, ...).
   Returns that key, with *sel the entry selected.  The box is no
   pop-up: it and its hints stay drawn, under the form, question or menu
   the key opens, for the caller to clear. */
u16 tui_pick_keys(const char *title, const char *head, u16 n,
                  tui_label_fn label, void *arg, u16 *sel, u16 hint);

/* ---------------------------------------------------------------------
 * Session and permissions.
 * --------------------------------------------------------------------- */
#define TUI_CONN_LOCAL  0
#define TUI_CONN_NET    1
#define TUI_CONN_COM    2       /* a serial link                        */

typedef struct {
    link *l;                    /* the open link                        */
    u8   conn;                  /* TUI_CONN_                            */
    u8   level;                 /* 0 before login, then 1..3            */
    u8   gone;                  /* 1 once tui_fail has told of a 200; a
                                   screen clears it before it looks     */
    char station[LINK_STALEN + 1];
    char user[LINK_USERLEN + 1];
    char name[LINK_FULLNAMELEN + 1];    /* from LOGIN; "" after a
                                           forced password change    */
} tui_sess;

extern tui_sess tui_s;

/* Access levels, as the protocol numbers them. */
#define TUI_LVL_READ    1
#define TUI_LVL_OPER    2
#define TUI_LVL_ADMIN   3

/* 1 when the user's level is at least need; otherwise 0 after showing
   rqe_deny's refusal as tui_msg(text, 1), without any request. */
int tui_allow(u8 need);

/* The user's access level (tui_s.level) as one digit and a NUL into d
   (2 bytes). */
void tui_level_text(char *d);

/* ---------------------------------------------------------------------
 * Commands, menus and the main loop.
 *
 * A command is a number 1..TUI_CMD_MAX the application defines;
 * TUI_CMD_NONE and the TUI_CMD_ values above TUI_CMD_MAX are the main
 * loop's own.  A screen function runs until the user leaves it and
 * returns the command to run next: TUI_CMD_NONE for the home screen.
 * --------------------------------------------------------------------- */
#define TUI_CMD_NONE    0       /* back to the home screen              */
#define TUI_CMD_MAX     249     /* highest application command          */
#define TUI_CMD_QUIT    250     /* end the program                      */
#define TUI_CMD_LOGIN   251     /* log out and show the login screen    */

/* A pull-down entry.  label is a message number whose text marks the
   hot-key letter with '&' ("&Settings"; "&&" is a literal '&'); level
   is the access level it needs (an entry above the user's is drawn
   TA_MENUOFF and refused through tui_allow); cmd is the command it
   gives. */
typedef struct {
    u16  label;
    u8   level;
    u8   cmd;
} tui_item;

/* A menu of the menu bar, opened by F10 and Left/Right, by Alt and its
   hot-key letter, or by a click on its title.  Its title is drawn
   TA_MENUOFF when no entry is allowed at the user's level; such a menu
   can still be opened, to show what it holds. */
typedef struct {
    u16  label;
    u8   nitems;
    const tui_item *items;
} tui_menu;

typedef u8 (*tui_screen_fn)(u8 cmd);

typedef struct {
    u8   nmenus;
    const tui_menu *menus;
    tui_screen_fn run;          /* runs command cmd, returns the next   */
    tui_screen_fn home;         /* the home screen (cmd TUI_CMD_NONE)   */
    tui_screen_fn login;        /* login screen: TUI_CMD_NONE once a
                                   user is logged in, LOGIN to be run
                                   again, else QUIT                     */
    u8   search;                /* the command of Ctrl-F                */
} tui_app;

/* Run the application: the login screen (again, with the user cleared,
   whenever a screen returns TUI_CMD_LOGIN), then commands until
   TUI_CMD_QUIT.  F3 asks before quitting.  Returns when the user
   quits; the caller closes the link and the screen.  Meanwhile, while
   key_read waits, the key_idle hook reads the DOS date once the tick
   count has passed midnight, so that DOS moves its date on, and on a
   remote link (tui_s.conn not TUI_CONN_LOCAL) sends PING every
   TUI_PINGT ticks (TUI.C), half the least IDLE= (one minute), so that
   the server does not end the session for IDLE= while INV waits at a
   key outside a request (a PING due during one, rq_busy, is dropped);
   a PING that finds the session ended is reported through tui_fail,
   ending the program when it says to quit. */
void tui_run(const tui_app *app);

/* Handle a key a screen did not use: F10, Alt-letter and a click on a
   menu's title in the menu bar open the menu, Ctrl-F gives the search
   command, F3 asks to quit.  Returns the command the screen should
   return, or TUI_CMD_NONE with *used 0 when the key is not global. */
u8 tui_global(u16 key, int *used);

/* The function that ends the program from tui_key at the end of a key
   script and from fatal errors: it closes the link and the screen and
   exits with status.  It does not return (a test may leave through
   longjmp instead of exit). */
typedef void (*tui_exit_fn)(int status);
void tui_set_exit(tui_exit_fn fn);

/* End the program with status through the installed exit function;
   never returns. */
void tui_exit(int status);

#define TUI_EXIT_LOST   9       /* the status after a lost NET link     */

/* Show text in the box of tui_showbox, titled MT_ERRTITLE, with the
   MT_HINTOK hint, wait for any key and end the program with status
   (tui_exit).  The key is read with key_read rather than tui_key, so
   the status stays status when a key script ends at the box.  text may
   be a msg() result.  The login screen uses it after the third failed
   login (SCR_EXIT_LOGIN) and a failed HELLO (SCR_EXIT_OPEN), tui_fail
   after a lost NET link (TUI_EXIT_LOST).  The idle hook (key_idle) is
   removed first, so that no PING is sent while the box waits. */
void tui_fatal(const char *text, int status);

/* ---------------------------------------------------------------------
 * Errors.
 * --------------------------------------------------------------------- */

/* Tell the user about the failed request res as rqe_class classifies it
   at the user's level: an RQE_LINE text as tui_msg(text, 1), an RQE_BOX
   one in tui_msgbox titled MT_ERRTITLE, but an RQE_LOST one through
   tui_fatal, which ends the program with TUI_EXIT_LOST.  A 200 (the
   record no longer exists) sets tui_s.gone.  Returns
   TUI_CMD_LOGIN for RQE_LOGIN, TUI_CMD_QUIT for RQE_QUIT, else
   TUI_CMD_NONE: the result is not TUI_CMD_NONE exactly when the screen
   must leave and return it at once. */
u8 tui_fail(const rq_res *res);

/* Send "words id" on tui_s.l, its data lines to fn (which may be NULL),
   the outcome to res (or NULL when the caller does not need it).  1 on
   success; else the failure has been reported through tui_fail and
   *cmd is the command the screen must return at once, or TUI_CMD_NONE
   to stay.  The request lives on this function's stack only, so no
   request buffer is held while another screen runs. */
int tui_req(const char *words, const char *id, rq_fn fn, void *arg,
            rq_res *res, u8 *cmd);

/* ---------------------------------------------------------------------
 * The Record menu: TUI_VERBS shows it, TUI_VERBTAB its verbs' table, to
 * a file that defines them before the include, as TUI_POPS below.
 * --------------------------------------------------------------------- */
#ifdef TUI_VERBS
#define TUI_VERBKEY     0x7F00  /* the keys of the verbs without one   */
#define TUI_VERBLAST    0x7FFF

/* Give the Record menu the verbs v of the screen shown, verb n (from 1,
   in the table of tui_verbs) when bit n - 1 is set, at most TUI_RECMAX
   (TUI.C); none with v 0.  The menu lists them in their order.  Redraws
   the bar; returns the verbs it had. */
u32 tui_record(u32 v);
#endif

#ifdef TUI_VERBTAB
/* The verbs the Record menus draw from: verb n (from 1) has the text of
   message text + n - 1; when chosen, the next tui_key gives the key its
   text names after two or more blanks (tui_keyname), as a pull-down
   entry names its key, else TUI_VERBKEY + n, which no keyboard gives; it
   needs TUI_LVL_OPER when bit n - 1 of oper is set, else TUI_LVL_READ.
   text 0, as before the first call, means no Record menu. */
void tui_verbs(u32 oper, u16 text);
#endif

/* ---------------------------------------------------------------------
 * Pop-ups: a box over the screen with hints of its own, which puts back
 * what it covered and the hints before it when it closes.  A file sees
 * this part only when it defines TUI_POPS before the include, for
 * Watcom C 6.5's compiler memory, as with TUI_CLICKS below.
 * --------------------------------------------------------------------- */
#ifdef TUI_POPS
typedef struct {
    cmem_blk *save;             /* the cells covered                    */
    char hint[TUI_HINTBUF];     /* the hints before it                  */
} tui_pop;

/* Keep the hints and the cells of the h rows and w columns at top,
   left, then show the hints of message hint (none for 0, for the caller
   to show): 1.  When the cells cannot be kept (memory has run out) show
   MT_NOMEM and return 0: the pop-up must not open. */
int tui_pop_open(tui_pop *p, u8 top, u8 left, u8 h, u8 w, u16 hint);

/* Put back the cells and the hints. */
void tui_pop_close(tui_pop *p);
#endif

/* ---------------------------------------------------------------------
 * Clicks and key names.  A file sees this part only when it defines
 * TUI_CLICKS before the include (and the click codes when it includes
 * CMOUSE.H), as Watcom C 6.5's compiler memory holds every declaration
 * a file reads.
 * --------------------------------------------------------------------- */
#ifdef TUI_CLICKS
/* When key k is a click (KEY_CLICK) on the h rows of w cells at top,
   left: the offset of the cell clicked in them, its row * KEY_CROW +
   its column, as a click's code holds a screen cell.  Else -1, also for
   the second press of a double click (KEY_DCLICK), which a list or pick
   list compares with the click before it. */
int tui_hit(u16 k, u8 top, u8 left, u8 h, u8 w);

/* The key named by the n bytes at w, matched without regard to case: a
   key name of KEYSCR.MSG (F1, ENTER, ...), its ALT- or CTRL- prefix and
   a letter, its CTRL- prefix and the name after the C of a key name
   that starts with C (CTRL-PGDN as CPGDN), xHHHH for the key code HHHH
   in hex (but KEY_NONE and KEY_EOS), or a single character, which names
   itself; else KEY_NONE (also for n 0).  Key scripts and the hint row
   name keys so. */
u16 tui_keyname(const char *w, u16 n);

/* 1 when tui_global acts on key: F3, F10, Ctrl-F, Alt-letter or a click
   on the menu bar. */
int tui_isglobal(u16 key);
#endif

#endif
