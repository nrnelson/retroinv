/*
 * TDRV.C - the test driver of the Windows programs (TDRV.H).
 *
 * The driver keeps no copy of the script and no far pointer but
 * td_tick's instance thunk, which lies in fixed memory: each tick reads
 * the script line at its offset (one seek, one read), and an offset
 * inside a K or T line takes it up again at its next key.  td_init
 * reads every line once to check it, so a line read later is good, but
 * for a failed read.
 *
 * A key's press and release come on two ticks: the press while the
 * program is idle, to the focus window, with the modifiers set in
 * Windows' key state; the release on the next tick, when the press and
 * everything it caused have been handled, to whichever window of the
 * program then holds the focus, as a keyboard's release goes, and the
 * modifiers are lifted with it.  CAPS LOCK is turned off at each press,
 * as a machine may start with it on.
 *
 * An M line's message is posted, never sent, as Windows queues the
 * mouse's messages, and to the window of the program holding the
 * mouse when one does, its point converted there, as Windows sends it;
 * the buttons it leaves held are set in Windows' key state, where
 * GetKeyState reads them and the next M line finds them, and the
 * screen's pointer stays where it is.
 *
 * Windows calls td_tick through the instance thunk MakeProcInstance
 * makes, as it calls every timer procedure.  td_tick yields to nothing
 * itself; a program whose WM_ENDSESSION, which a Q line sends from
 * td_tick, yields holds the driver meanwhile (td_busy), so that a tick
 * entered again only checks the wait limit.
 *
 * A run ends in two steps, so that the program leaves as it does when
 * the user ends it: stop records why, kills the timer and ends the
 * program's loop with PostQuitMessage; td_done, which the program calls
 * after its loop, writes the last line and, when /SNAP: was given,
 * calls ExitWindows.  A Q line that every window agrees to ends it in
 * one, from the tick: td_done is called there and always calls
 * ExitWindows, as Windows ends a program after WM_ENDSESSION without
 * returning to its loop.  So does a run that td_init refuses, from
 * td_init: the program's loop has not started, and its start-up may
 * drop a quit posted before it.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "WCOLS.H"
#include "WLIST.H"
#include "TDRV.H"
#include "WTAB.H"
#include "WTOOL.H"
#include "WSTAT.H"
#include "WSPLIT.H"
#include "WPREV.H"

/* The timer's period: one tick of the PC's clock, the shortest Windows
   gives. */
#define TD_TICKMS       55
#define TD_NDLGS        8       /* dialogs open at once whose place
                                   td_place keeps as checked            */
#define TD_KEYLEN       6       /* characters of a key name without its
                                   prefixes, CRIGHT                     */
#define TD_CLASSLEN     32      /* bytes of a class name kept, its NUL
                                   included                             */
#define TD_WORDLEN      9       /* bytes of a word of td_states, td_fails
                                   or td_words and its NUL, DISABLED    */
/* Bytes of a snapshot line, CR LF included: a window's line at its
   longest, its text at TD_TEXTSZ, or a row's drawing (draw_line) of
   WCOL_MAX pieces of LV_ROWTEXT cells in all, 40 levels deep; a deeper
   line is cut before its CR LF. */
#define TD_LINELEN      400
/* Every row of a list is written whole (TD_MAXTEXT). */
extern char td_rows_fit[1];
extern char td_rows_fit[(LV_ROWTEXT <= TD_MAXTEXT) ? 1 : 2];
/* Bytes of a window's text read, its NUL included: TD_MAXTEXT, or a
   status bar's whole (WSB_MAXTEXT), the longer. */
#define TD_TEXTSZ       ((WSB_MAXTEXT > TD_MAXTEXT ? WSB_MAXTEXT : \
                          TD_MAXTEXT) + 1)
/* A bar's line is never cut: a tool bar's text, with a TD_GRAYED and a
   TD_PRESSED for each button, within TD_MAXTEXT, a status bar's within
   TD_TEXTSZ, its depth, class, ID, quotes and states in the room
   left. */
extern char td_bars_fit[1];
extern char td_bars_fit[(WTB_MAXTEXT + 2 * WTB_MAXBTNS <= TD_MAXTEXT &&
                         TD_TEXTSZ + TD_CLASSLEN + 4 * TD_WORDLEN <=
                         TD_LINELEN) ? 1 : 2];
#define TD_PIECE        '|'     /* between a bar's pieces on its line   */
#define TD_GRAYED       '~'     /* before a disabled button's text      */
#define TD_PRESSED      '^'     /* before a pressed button's text       */
#define TD_HILITE       0x0004  /* BM_GETSTATE: the button is pressed   */
#define TD_NKEYS        256     /* entries of Windows' key state table  */
#define TD_DOWN         0x80    /* a held key's bit in the key state    */
#define TD_BTYPE        0x0F    /* the button type of a Button's style  */
/* GetPixel's answer for a point outside what its display context
   shows. */
#define TD_NOPIXEL      0xFFFFFFFFUL
/* The script's offset once a Ctrl-Z has ended it. */
#define TD_EOS          0xFFFFFFFFUL

/* The modifiers of a key code (key_named), above its virtual key, in
   the order of td_mods. */
#define TD_SHIFT        0x100
#define TD_CTRL         0x200
#define TD_ALT          0x400

/* lParam of a key message: one repeat, the scan code from bit 16, the
   ALT bit, and the bits of a release (key down before, key going up). */
#define TD_LPSCAN       16
#define TD_LPALT        0x20000000UL
#define TD_LPUP         0xC0000000UL

/* The failures of =FAIL, the words of td_fails, TDF_END for =END and
   TDF_RUN while the run goes on. */
#define TDF_RUN         (-2)
#define TDF_END         (-1)
#define TDF_SCRIPT      0
#define TDF_FOCUS       1
#define TDF_BUSY        2
#define TDF_ENDED       3
#define TDF_TIMER       4
#define TDF_PLACE       5
#define TDF_TARGET      6

/* The states of a snapshot line, bit i standing for word i of
   td_states. */
#define TDS_DISABLED    0x001
#define TDS_HIDDEN      0x002
#define TDS_ACTIVE      0x004
#define TDS_FOCUS       0x008
#define TDS_CAPTURE     0x010
#define TDS_CHECKED     0x020
#define TDS_MIXED       0x040
#define TDS_DEFAULT     0x080
#define TDS_SELECTED    0x100
#define TDS_DROP        0x200
#define TDS_LONG        0x400
#define TDS_OVERLAP     0x800
#define TDS_CUT         0x1000
#define TDS_BLANK       0x2000

/* The words of td_words from TDW_AT to TDW_BTN are an M line's bases;
   AT, ROW, HEAD and DRAW are also the words of snapshot lines. */
#define TDW_AT          4
#define TDW_MID         5
#define TDW_ROW         6
#define TDW_HEAD        7
#define TDW_DIV         8
#define TDW_BTN         9
#define TDW_DRAW        10
/* The script's mouse events, the first TDW_AT words: each is the message
   WM_MOUSEMOVE plus its number, and after an R, but MOVE, the right
   button's, TD_RIGHT further on; a double click's message less TD_DBL is
   the press. */
#define TD_RIGHT        (WM_RBUTTONDOWN - WM_LBUTTONDOWN)
#define TD_DBL          (WM_LBUTTONDBLCLK - WM_LBUTTONDOWN)
/* An M line's numbers: an index or an offset at most TD_MAXNUM, a
   control ID at most TD_MAXID; TD_NONUM for none. */
#define TD_MAXNUM       32767
#define TD_MAXID        65535u
#define TD_NONUM        0xFFFFFFFFUL

/* The key names but F1..F10, letters and digits, and their codes, in
   the same order. */
static const char td_names[] = "ENTER ESC TAB STAB BS SPACE UP DOWN LEFT "
                               "RIGHT PGUP PGDN HOME END INS DEL CLEFT "
                               "CRIGHT CHOME CEND CPGUP CPGDN SLASH BSLASH";
static const u16 td_codes[] = {
    VK_RETURN, VK_ESCAPE, VK_TAB, TD_SHIFT | VK_TAB, VK_BACK, VK_SPACE,
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_PRIOR, VK_NEXT, VK_HOME, VK_END,
    VK_INSERT, VK_DELETE, TD_CTRL | VK_LEFT, TD_CTRL | VK_RIGHT,
    TD_CTRL | VK_HOME, TD_CTRL | VK_END, TD_CTRL | VK_PRIOR,
    TD_CTRL | VK_NEXT, WL_VK_ALL, WL_VK_NONE
};
static const char *const td_mods[] = { "SHIFT-", "CTRL-", "ALT-" };

static const char td_states[] = "DISABLED HIDDEN ACTIVE FOCUS CAPTURE CHECKED "
                                "MIXED DEFAULT SELECTED DROP LONG OVERLAP CUT "
                                "BLANK";
static const char td_fails[] = "SCRIPT FOCUS BUSY ENDED TIMER PLACE TARGET";
static const char td_words[] = "MOVE DOWN UP DBL AT MID ROW HEAD DIV BTN DRAW";
static const char td_heads[] = "FBAP";  /* by WCOL_H_ state (WCOLS.H)    */

static const char k_button[] = "Button";
static const char k_edit[] = "Edit";
static const char k_listbox[] = "ListBox";
static const char k_combobox[] = "ComboBox";
static const char k_static[] = "Static";
static const char k_wlist[] = "WLIST";
static const char k_hbar[] = " HBAR";
static const char k_wtab[] = "WTAB";
static const char k_cur[] = " CUR";
static const char k_wtool[] = "WTOOL";
static const char k_wstat[] = "WSTAT";
static const char k_wsplit[] = "WSPLIT";
static const char k_limits[] = " LIMITS";
static const char k_wprev[] = "WPREV";
static const char k_vbar[] = " VBAR";
static const char k_dialog[] = "#32770";
static const char k_winhelp[] = "MS_WINHELP";
static const char k_stack[] = "=STACK UNUSED=";

typedef struct {
    int keys;                   /* the script, or -1                    */
    int snap;                   /* the snapshot file, or -1             */
    u32 pos;                    /* offset of the script's next byte to
                                   carry out, or TD_EOS                 */
    u32 since;                  /* GetTickCount of the last idle tick   */
    u32 maxwait;                /* ms an idle tick may be waited for    */
    TIMERPROC tick;             /* td_tick's instance thunk, or NULL    */
    HINSTANCE inst;             /* the program's instance               */
    HWND dlgs[TD_NDLGS];        /* the dialogs whose place was checked,
                                   while they last, or NULL             */
    UINT timer;                 /* the timer, or 0                      */
    u16 line;                   /* the script line last read            */
    u16 up;                     /* the key whose release is due, or 0   */
    int why;                    /* how the run ended, a TDF_ code       */
    u8 kind;                    /* 'K' or 'T' while pos is inside such a
                                   line (a P or A line not typed ahead
                                   is typed as a K line), else 0        */
    u8 busy;                    /* td_busy holds the script             */
    u8 typed;                   /* a key or character was posted after
                                   the last tick a snapshot waited      */
    u8 ended;                   /* the program's loop has ended         */
    u8 on;                      /* a script is running                  */
    u8 quit;                    /* td_done ends Windows: /SNAP: was
                                   given or a Q line ended the session  */
    u8 place;                   /* td_place checks the dialogs' place   */
    u8 off;                     /* a dialog was found elsewhere         */
    u8 covered;                 /* a C line had the active window
                                   painted again after the last
                                   snapshot                             */
    u16 offline;                /* the line carried out last then       */
} td_state;

static td_state td_st;

/* A snapshot line as it is built. */
typedef struct {
    char t[TD_LINELEN];
    u16 n;
} td_line;

/* 1 when c separates the words of a line: a space or a TAB. */
static int is_sp(char c)
{
    return c == ' ' || c == '\t';
}

/* 1 when wnd is a window of this program. */
static int ours(HWND wnd)
{
    return wnd != NULL && GetWindowTask(wnd) == GetCurrentTask();
}

/* 1 when wnd is a window of this program or of Windows Help's task,
   whose windows the snapshots show and the keys go to while it runs. */
static int seen(HWND wnd)
{
    HWND help;

    if (ours(wnd))
        return 1;
    help = FindWindow(k_winhelp, NULL);
    return wnd != NULL && help != NULL &&
           GetWindowTask(wnd) == GetWindowTask(help);
}

/* Read the script line at pos into buf (TD_MAXLINE + 2 bytes), its CR
   LF dropped and a NUL after it, and set *next to the offset after it
   (pos at the end of the script); returns its length, -1 at the end of
   the script, or -2 when the line is longer than TD_MAXLINE or holds a
   NUL, or the script cannot be read. */
static int fetch(u32 pos, char *buf, u32 *next)
{
    u16 got;
    u16 n;

    *next = pos;
    if (pos == TD_EOS)
        return -1;
    if (cf_seek(td_st.keys, pos) != 0 ||
        cf_read(td_st.keys, buf, TD_MAXLINE + 2, &got) != 0)
        return -2;
    if (got == 0)
        return -1;
    for (n = 0; n < got && buf[n] != '\n' && buf[n] != '\032'; n++)
        ;
    if (n == TD_MAXLINE + 2)
        return -2;
    *next = n == got ? pos + got : buf[n] == '\n' ? pos + n + 1 : TD_EOS;
    if (n > 0 && buf[n - 1] == '\r')
        n--;
    buf[n] = '\0';
    return n > TD_MAXLINE || strlen(buf) != n ? -2 : (int)n;
}

/* The TD_ modifiers of the prefixes (td_mods) that start the *n bytes at
   *w, in any order, *w and *n moved past them; at least a byte is left. */
static u16 mods_of(const char **w, u16 *n)
{
    u16 mods;
    u16 len;
    u16 i;

    mods = 0;
    i = 0;
    while (i < sizeof td_mods / sizeof td_mods[0]) {
        len = (u16)strlen(td_mods[i]);
        if (*n > len && txt_ieqn(*w, td_mods[i], len)) {
            mods |= TD_SHIFT << i;
            *w += len;
            *n -= len;
            i = 0;
        } else {
            i++;
        }
    }
    return mods;
}

/* The code of the key named by the n bytes at w, its virtual key in the
   low byte and its TD_ modifiers above, or 0 when they name none. */
static u16 key_named(const char *w, u16 n)
{
    char name[TD_KEYLEN + 1];
    u16 mods;
    int k;
    char c;

    mods = mods_of(&w, &n);
    if (n > TD_KEYLEN)
        return 0;
    memcpy(name, w, n);
    name[n] = '\0';
    k = txt_word_num(td_names, name);
    if (k >= 0)
        return td_codes[k] | mods;
    c = name[0];
    c = (char)TXT_UPPER(c);
    if (c == 'F' && n > 1) {
        k = (int)txt_num(name + 1, 10);
        return k == 0 ? 0 : (u16)(VK_F1 + k - 1) | mods;
    }
    if (n == 1 && (TXT_ISALPHA(c) || TXT_ISDIGIT(c)))
        return (u16)(u8)c | mods;
    return 0;
}

/* The next word at *l, ended by a NUL in place, *l moved past it; NULL
   when there is none. */
static char *next_word(char **l)
{
    char *w;
    char *p;

    for (p = *l; is_sp(*p); p++)
        ;
    if (*p == '\0')
        return NULL;
    for (w = p; *p != '\0' && !is_sp(*p); p++)
        ;
    if (*p != '\0')
        *p++ = '\0';
    *l = p;
    return w;
}

/* The one word of a line's arguments at l, ended by a NUL in place;
   NULL when there is none or more. */
static char *one_word(char *l)
{
    char *w;

    w = next_word(&l);
    return next_word(&l) == NULL ? w : NULL;
}

/* The name of an S line's arguments at l, ended by a NUL in place, when
   it is 1..TD_NAMELEN of A-Z, 0-9 and _, alone; else NULL. */
static char *snap_name(char *l)
{
    char *name;
    char *c;

    name = one_word(l);
    if (name == NULL || strlen(name) > TD_NAMELEN)
        return NULL;
    for (c = name; *c != '\0'; c++)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
              *c == '_'))
            return NULL;
    return name;
}

/* The wait limit in ms of a W line's arguments at l, 0 when they are not
   one number of 1..TD_MAXWAIT / 1000 seconds. */
static u32 wait_ms(char *l)
{
    char *w;

    w = one_word(l);
    return w == NULL ? 0 : txt_num(w, (u16)(TD_MAXWAIT / 1000)) * 1000ul;
}

/* An M line's arguments (TDRV.H). */
typedef struct {
    char *path;                 /* its path, in the line                */
    UINT msg;                   /* its event's message                  */
    u16 mods;                   /* its event's TD_ modifiers            */
    int base;                   /* its base, a TDW_ word                */
    int k;                      /* the base's index, or 0               */
    int dx;                     /* the offsets                          */
    int dy;
} td_mouse;

/* The value of the word w, digits alone, when it is at most max, else
   TD_NONUM. */
static u32 num_of(const char *w, u32 max)
{
    u32 v;

    if (*w == '\0')
        return TD_NONUM;
    for (v = 0; TXT_ISDIGIT(*w) && v <= max; w++)
        v = v * 10 + (u32)(*w - '0');
    return *w == '\0' && v <= max ? v : TD_NONUM;
}

/* The offset of the word w, a '-' before it for one below 0, into *d; 1
   when w is one. */
static int offset(const char *w, int *d)
{
    u32 v;
    int neg;

    neg = *w == '-';
    v = num_of(w + neg, TD_MAXNUM);
    *d = neg ? -(int)v : (int)v;
    return v != TD_NONUM;
}

/* The window of the path p from the program's active window into *w,
   NULL when it names none, unless w is NULL; 1 when p is a path: control
   IDs joined by '.'. */
static int walk(char *p, HWND *w)
{
    char *dot;
    u32 id;

    if (w != NULL) {
        *w = GetActiveWindow();
        if (!ours(*w))
            *w = NULL;
    }
    for (;;) {
        dot = strchr(p, '.');
        if (dot != NULL)
            *dot = '\0';
        id = num_of(p, TD_MAXID);
        if (dot != NULL)
            *dot = '.';
        if (id == TD_NONUM)
            return 0;
        if (w != NULL && *w != NULL)
            *w = GetDlgItem(*w, (int)(u16)id);
        if (dot == NULL)
            return 1;
        p = dot + 1;
    }
}

/* Read the arguments at l of an M line into *m, its words ended by NULs
   in place; 1 when they are good. */
static int mouse_args(char *l, td_mouse *m)
{
    const char *e;
    char *w;
    u32 v;
    u16 n;
    int r;
    int i;

    m->path = next_word(&l);
    w = next_word(&l);
    if (m->path == NULL || !walk(m->path, NULL) || w == NULL)
        return 0;
    e = w;
    n = (u16)strlen(e);
    m->mods = mods_of(&e, &n);
    r = TXT_UPPER(*e) == 'R';
    i = txt_word_num(td_words, e + r);
    if (i < r || i >= TDW_AT)
        return 0;
    m->msg = WM_MOUSEMOVE + i + (r ? TD_RIGHT : 0);
    w = next_word(&l);
    m->base = w == NULL ? -1 : txt_word_num(td_words, w);
    if (m->base < TDW_AT || m->base > TDW_BTN)
        return 0;
    m->k = 0;
    if (m->base >= TDW_ROW) {
        w = next_word(&l);
        v = w == NULL ? TD_NONUM : num_of(w, TD_MAXNUM);
        if (v == TD_NONUM)
            return 0;
        m->k = (int)v;
    }
    m->dx = 0;
    m->dy = 0;
    w = next_word(&l);
    if (w != NULL) {
        if (!offset(w, &m->dx))
            return 0;
        w = next_word(&l);
        if (w == NULL || !offset(w, &m->dy))
            return 0;
        w = next_word(&l);
    }
    return w == NULL;
}

/* 1 when the script line l is good. */
static int line_ok(char *l)
{
    td_mouse m;
    const char *w;
    char c;

    c = l[0];
    c = (char)TXT_UPPER(c);
    if (c == '\0' || c == '#')
        return 1;
    if (l[1] != '\0' && !is_sp(l[1]))
        return 0;
    if (c == 'T')
        return 1;
    if (c == 'S')
        return snap_name(l + 1) != NULL;
    if (c == 'W')
        return wait_ms(l + 1) != 0;
    if (c == 'M')
        return mouse_args(l + 1, &m);
    if (c == 'Q' || c == 'O' || c == 'C' || c == 'E') {
        for (l++; is_sp(*l); l++)
            ;
        return *l == '\0';
    }
    if (c != 'K' && c != 'P' && c != 'A')
        return 0;
    for (l++; ; ) {
        while (is_sp(*l))
            l++;
        if (*l == '\0')
            return 1;
        w = l;
        while (*l != '\0' && !is_sp(*l))
            l++;
        if (key_named(w, (u16)(l - w)) == 0)
            return 0;
    }
}

/* Check every line of the script, a P line only after a K, T, M or P
   line, comments and blank lines aside; 0, or the number of the first
   bad one. */
static u16 check(void)
{
    char buf[TD_MAXLINE + 2];
    u32 pos;
    u16 line;
    int n;
    int keyed;
    char c;

    pos = 0;
    keyed = 0;
    for (line = 1; ; line++) {
        n = fetch(pos, buf, &pos);
        if (n == -1)
            return 0;
        if (n < 0 || !line_ok(buf))
            return line;
        c = buf[0];
        c = (char)TXT_UPPER(c);
        if (c == 'P' && !keyed)
            return line;
        if (c != '#' && c != '\0')
            keyed = c == 'K' || c == 'T' || c == 'M' || c == 'P';
    }
}

/* Start the line l at depth spaces. */
static void ln_start(td_line *l, int depth)
{
    l->n = 0;
    while (l->n < depth)
        l->t[l->n++] = ' ';
}

/* Append c to the line l, unless only its CR LF has room left. */
static void ln_char(td_line *l, char c)
{
    if (l->n < TD_LINELEN - 2)
        l->t[l->n++] = c;
}

static void ln_add(td_line *l, const char *s)
{
    while (*s != '\0')
        ln_char(l, *s++);
}

/* Append word i of the word list list. */
static void ln_word(td_line *l, const char *list, u16 i)
{
    char w[TD_WORDLEN];

    txt_word_at(list, i, w, (u16)sizeof w);
    ln_add(l, w);
}

/* Append the number v. */
static void ln_dec(td_line *l, u16 v)
{
    char d[TXT_U32LEN + 1];

    txt_u32(d, v);
    ln_add(l, d);
}

/* Append a space and the number v. */
static void ln_num(td_line *l, u16 v)
{
    ln_char(l, ' ');
    ln_dec(l, v);
}

/* Append the text t in quotes, each byte below 20h as '?'. */
static void ln_quote(td_line *l, const char *t)
{
    ln_char(l, '"');
    for (; *t != '\0'; t++)
        ln_char(l, (u8)*t < 0x20 ? '?' : *t);
    ln_char(l, '"');
}

/* Append a space and the text t in quotes, as ln_quote. */
static void ln_text(td_line *l, const char *t)
{
    ln_char(l, ' ');
    ln_quote(l, t);
}

/* Append the words of the TDS_ states st, in td_states' order. */
static void ln_states(td_line *l, u16 st)
{
    u16 i;

    for (i = 0; st != 0; i++, st >>= 1)
        if (st & 1) {
            ln_char(l, ' ');
            ln_word(l, td_states, i);
        }
}

/* Write the line l and CR LF to the snapshot file, if still open; a
   failed write closes it, so that it lacks its =END line. */
static void ln_put(td_line *l)
{
    if (td_st.snap < 0)
        return;
    l->t[l->n++] = '\r';
    l->t[l->n++] = '\n';
    if (cf_write(td_st.snap, l->t, l->n) != 0) {
        cf_close(td_st.snap);
        td_st.snap = -1;
    }
}

/* Append the colour c as RRGGBB in hex. */
static void ln_rgb(td_line *l, COLORREF c)
{
    u32 v;
    int i;

    v = (u32)GetRValue(c) << 16 | (u32)GetGValue(c) << 8 | GetBValue(c);
    for (i = 20; i >= 0; i -= 4)
        ln_char(l, txt_hexdig[(int)(v >> i) & 0xF]);
}

/* Write the line of a drawing s, its word what (TDW_HEAD or TDW_DRAW)
   and st its states, at depth: its colours, then each piece, a space
   and "kA@x+w" and its text in quotes, k the column, A its alignment (R
   for WCOL_RIGHT, I for WCOL_INDENT, else L), x and w its place. */
static void draw_line(const wcol_seen *s, u16 what, u16 st, int depth)
{
    td_line l;
    char t[LV_ROWTEXT + 1];
    const wcol_pc *pc;
    int k;

    ln_start(&l, depth);
    ln_word(&l, td_words, what);
    ln_char(&l, ' ');
    ln_rgb(&l, s->fg);
    ln_char(&l, '/');
    ln_rgb(&l, s->bg);
    for (k = 0; k < s->n; k++) {
        pc = &s->pc[k];
        if (pc->len == 0)
            continue;
        ln_num(&l, (u16)k);
        ln_char(&l, pc->flags & WCOL_RIGHT ? 'R' :
                    pc->flags & WCOL_INDENT ? 'I' : 'L');
        ln_char(&l, '@');
        if (pc->x < 0)
            ln_char(&l, '-');
        ln_dec(&l, (u16)(pc->x < 0 ? -pc->x : pc->x));
        ln_char(&l, '+');
        ln_dec(&l, (u16)pc->w);
        memcpy(t, s->text + pc->at, pc->len);
        t[pc->len] = '\0';
        AnsiToOem(t, t);
        ln_quote(&l, t);
    }
    ln_states(&l, st);
    ln_put(&l);
}

/* Write the COLS line of a drawing s of headings at depth: for each
   column a space, k, its state's letter of td_heads, '@', its slot's x
   and '+' its width. */
static void cols_line(const wcol_seen *s, int depth)
{
    td_line l;
    const wcol_pc *pc;
    int k;

    ln_start(&l, depth);
    ln_add(&l, "COLS");
    for (k = 0; k < s->n; k++) {
        pc = &s->pc[k];
        ln_num(&l, (u16)k);
        ln_char(&l, td_heads[pc->head]);
        ln_char(&l, '@');
        if (pc->sx < 0)
            ln_char(&l, '-');
        ln_dec(&l, (u16)(pc->sx < 0 ? -pc->sx : pc->sx));
        ln_char(&l, '+');
        ln_dec(&l, (u16)pc->sw);
    }
    ln_put(&l);
}

/* Convert the text t of the window w to CP437 (AnsiToOem), unless w
   shows it in a font of the OEM character set, as OEM_FIXED_FONT, which
   holds it as CP437 already. */
static void to_oem(HWND w, char *t)
{
    LOGFONT lf;
    HFONT f;

    f = (HFONT)SendMessage(w, WM_GETFONT, 0, 0L);
    if (f == NULL || GetObject(f, sizeof lf, (LPSTR)&lf) == 0 ||
        lf.lfCharSet != OEM_CHARSET)
        AnsiToOem(t, t);
}

/* 1 when w is a button of the type type: a group box, which encloses
   its siblings by design, or one owner-drawn, as a tool bar's buttons
   are. */
static int is_button(HWND w, u32 type)
{
    char cls[TD_CLASSLEN];

    GetClassName(w, cls, (int)sizeof cls);
    return txt_ieq(cls, k_button) &&
           (GetWindowLong(w, GWL_STYLE) & TD_BTYPE) == type;
}

/* 1 when the visible child w, but its border line, reaches outside its
   parent's client area or into a visible sibling but a group box: a
   control laid out wrong, as two that share a border line are not. */
static int overlaps(HWND w)
{
    RECT r;
    RECT o;
    RECT x;
    HWND p;
    HWND s;

    if (is_button(w, BS_GROUPBOX))
        return 0;
    GetWindowRect(w, &r);
    InflateRect(&r, -1, -1);
    p = GetParent(w);
    GetClientRect(p, &o);
    ClientToScreen(p, (POINT FAR *)&o.left);
    ClientToScreen(p, (POINT FAR *)&o.right);
    if (r.left < o.left || r.top < o.top || r.right > o.right ||
        r.bottom > o.bottom)
        return 1;
    for (s = GetWindow(p, GW_CHILD); s != NULL; s = GetWindow(s, GW_HWNDNEXT))
        if (s != w && IsWindowVisible(s) && !is_button(s, BS_GROUPBOX)) {
            GetWindowRect(s, &o);
            if (IntersectRect(&x, &r, &o))
                return 1;
        }
    return 0;
}

/* The text of the tool bar w as its line gives it (TDRV.H), into t,
   which holds TD_MAXTEXT bytes and a NUL. */
static void tool_text(HWND w, char *t)
{
    char s[WTB_MAXTEXT + 1];
    char *p;
    char *e;
    HWND b;
    int k;

    GetWindowText(w, s, (int)sizeof s);
    for (p = s, k = 0; ; p = e + 1) {
        e = strchr(p, WTB_SEP);
        if (e != NULL)
            *e = '\0';
        if ((p[0] == WTB_GAP || p[0] == WTB_SLOT) && p[1] == '\0') {
            *t++ = p[0];
        } else {
            b = (HWND)(UINT)SendMessage(w, WTBM_BUTTON, (WPARAM)k++, 0L);
            if (b != NULL && !IsWindowEnabled(b))
                *t++ = TD_GRAYED;
            if (b != NULL && (SendMessage(b, BM_GETSTATE, 0, 0L) & TD_HILITE))
                *t++ = TD_PRESSED;
            if (b != NULL)
                t += GetWindowText(b, t, WTB_MAXTEXT + 1);
        }
        if (e == NULL)
            break;
        *t++ = TD_PIECE;
    }
    *t = '\0';
}

/* Write the line of the window w, of class cls and style style, at
   depth, a list window's horizontal bar after its text (HBAR), a
   preview panel's vertical one (VBAR), a tab strip's tab (CUR) and a
   splitter's place (AT) and limits (LIMITS). */
static void window_line(HWND w, int depth, const char *cls, u32 style)
{
    td_line l;
    char text[TD_TEXTSZ];
    RECT r;
    HWND cap;
    u32 lim;
    u16 st;
    int max;
    int n;

    ln_start(&l, depth);
    ln_add(&l, cls);
    if (style & WS_CHILD)
        ln_num(&l, (u16)GetWindowWord(w, GWW_ID));
    if (txt_ieq(cls, k_edit) && (style & ES_PASSWORD)) {
        n = GetWindowTextLength(w);
        if (n > TD_MAXTEXT)
            n = TD_MAXTEXT;
        memset(text, '*', (size_t)n);
        text[n] = '\0';
    } else {
        if (txt_ieq(cls, k_wtool)) {
            tool_text(w, text);
        } else {
            GetWindowText(w, text, txt_ieq(cls, k_wstat) ?
                          (int)sizeof text : TD_MAXTEXT + 1);
            if (txt_ieq(cls, k_wstat))
                for (n = 0; text[n] != '\0'; n++)
                    if (text[n] == WSB_SEP)
                        text[n] = TD_PIECE;
        }
        to_oem(w, text);
    }
    ln_text(&l, text);
    st = 0;
    if (style & WS_DISABLED)
        st |= TDS_DISABLED;
    if (!(style & WS_VISIBLE))
        st |= TDS_HIDDEN;
    if (w == GetActiveWindow())
        st |= TDS_ACTIVE;
    if (w == GetFocus())
        st |= TDS_FOCUS;
    cap = GetCapture();
    if ((style & WS_CHILD) && cap != NULL &&
        (cap == w || (txt_ieq(cls, k_wtool) && GetParent(cap) == w &&
                      is_button(cap, BS_OWNERDRAW))))
        st |= TDS_CAPTURE;
    if (depth > 0 && IsWindowVisible(w) && overlaps(w))
        st |= TDS_OVERLAP;
    if (txt_ieq(cls, k_wstat) && SendMessage(w, WSBM_CUT, 0, 0L))
        st |= TDS_CUT;
    if (txt_ieq(cls, k_button)) {
        n = (int)SendMessage(w, BM_GETCHECK, 0, 0L);
        if (n == 1)
            st |= TDS_CHECKED;
        else if (n == 2)
            st |= TDS_MIXED;
        if ((style & TD_BTYPE) == BS_DEFPUSHBUTTON)
            st |= TDS_DEFAULT;
    }
    if (txt_ieq(cls, k_wlist) && (style & WS_HSCROLL)) {
        GetScrollRange(w, SB_HORZ, &n, &max);
        if (max > 0) {
            ln_add(&l, k_hbar);
            ln_num(&l, (u16)GetScrollPos(w, SB_HORZ));
            ln_num(&l, (u16)max);
        }
    }
    if (txt_ieq(cls, k_wprev)) {
        GetScrollRange(w, SB_VERT, &n, &max);
        if (max > 0) {
            ln_add(&l, k_vbar);
            ln_num(&l, (u16)GetScrollPos(w, SB_VERT));
            ln_num(&l, (u16)max);
        }
    }
    if (txt_ieq(cls, k_wtab)) {
        ln_add(&l, k_cur);
        ln_num(&l, (u16)SendMessage(w, WTM_GETCUR, 0, 0L));
    }
    if (txt_ieq(cls, k_wsplit)) {
        GetWindowRect(w, &r);
        ScreenToClient(GetParent(w), (POINT FAR *)&r.left);
        ln_char(&l, ' ');
        ln_word(&l, td_words, TDW_AT);
        ln_num(&l, (u16)r.left);
        lim = (u32)SendMessage(w, WSPM_GETLIMITS, 0, 0L);
        ln_add(&l, k_limits);
        ln_num(&l, LOWORD(lim));
        ln_num(&l, HIWORD(lim));
    }
    ln_states(&l, st);
    ln_put(&l);
}

/* Write the line of item i of the menu m at depth, a popup when sub is
   not NULL. */
static void menu_line(HMENU m, int i, int depth, HMENU sub)
{
    td_line l;
    char text[TD_MAXTEXT + 1];
    UINT st;

    text[0] = '\0';
    GetMenuString(m, i, text, (int)sizeof text, MF_BYPOSITION);
    AnsiToOem(text, text);
    st = GetMenuState(m, i, MF_BYPOSITION);
    ln_start(&l, depth);
    if (sub != NULL) {
        ln_add(&l, "POPUP");
    } else {
        ln_add(&l, "MENU");
        ln_num(&l, (u16)GetMenuItemID(m, i));
    }
    ln_text(&l, text);
    ln_states(&l, (u16)((st & (MF_GRAYED | MF_DISABLED) ? TDS_DISABLED : 0) |
                        (st & MF_CHECKED ? TDS_CHECKED : 0)));
    ln_put(&l);
}

/* Write the items of the menu m at depth, each popup's one deeper. */
static void snap_menu(HMENU m, int depth)
{
    HMENU sub;
    int n;
    int i;

    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        sub = GetSubMenu(m, i);
        menu_line(m, i, depth, sub);
        if (sub != NULL)
            snap_menu(sub, depth + 1);
    }
}

/* 1 when row i of the list box w lies whole in its client area and
   the screen shows one colour all along its middle line, where a row
   drawn has its text; 0 too for a row another window covers. */
static int blank(HWND w, int i)
{
    COLORREF c;
    RECT b;
    RECT r;
    HDC dc;
    int x;
    int y;

    GetClientRect(w, &b);
    if (SendMessage(w, LB_GETITEMRECT, (WPARAM)i, (LPARAM)(RECT FAR *)&r) ==
            LB_ERR || r.top < 0 || r.bottom > b.bottom)
        return 0;
    dc = GetDC(w);
    y = (r.top + r.bottom) / 2;
    c = GetPixel(dc, 0, y);
    for (x = 1; x < b.right && GetPixel(dc, x, y) == c; x++)
        ;
    ReleaseDC(w, dc);
    return c != TD_NOPIXEL && x >= b.right;
}

/* Write the rows of the list box (combo 0) or combo box (combo 1) w, of
   style style, at depth; an owner-drawn box keeps their texts only with
   the style that says so.  A list box that draws its own rows has each
   measured, in its font, against its width; an owner-drawn one has each
   drawn by its owner while watched (wcol_watch), selected as it is but
   never with the focus, through a display context clipped to nothing,
   so that an owner drawing otherwise paints nothing, and the drawing of
   its first and selected rows written one deeper.  A row of a list
   window's box (WLIST.H) is DROP while its item data marks it as the
   drop target, and a list box's row BLANK when the screen shows none of
   it (blank) in the first snapshot after a C line. */
static void snap_rows(HWND w, int depth, int combo, u32 style)
{
    td_line l;
    char text[TD_MAXTEXT + 1];
    DRAWITEMSTRUCT d;
    wcol_seen s;
    RECT r;
    HDC dc;
    HFONT f;
    char cls[TD_CLASSLEN];
    int strings;
    int drawn;
    int drops;
    int sel;
    int n;
    int cur;
    int i;
    u16 st;

    dc = NULL;
    drawn = 0;
    GetClassName(GetParent(w), cls, (int)sizeof cls);
    drops = !combo && txt_ieq(cls, k_wlist);
    if (combo) {
        strings = !(style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) ||
                  (style & CBS_HASSTRINGS) != 0;
    } else {
        dc = GetDC(w);
        GetClientRect(w, &r);
        drawn = (style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0;
        strings = !drawn || (style & LBS_HASSTRINGS) != 0;
        f = (HFONT)SendMessage(w, WM_GETFONT, 0, 0L);
        if (!drawn && f != NULL)
            SelectObject(dc, f);
        memset(&d, 0, sizeof d);
        d.CtlType = ODT_LISTBOX;
        d.CtlID = GetWindowWord(w, GWW_ID);
        d.itemAction = ODA_DRAWENTIRE;
        d.hwndItem = w;
        d.hDC = dc;
        if (drawn)
            IntersectClipRect(dc, 0, 0, 0, 0);
    }
    n = (int)SendMessage(w, combo ? CB_GETCOUNT : LB_GETCOUNT, 0, 0L);
    cur = combo ? (int)SendMessage(w, CB_GETCURSEL, 0, 0L) : -1;
    for (i = 0; i < n; i++) {
        ln_start(&l, depth);
        ln_word(&l, td_words, TDW_ROW);
        sel = combo ? i == cur : SendMessage(w, LB_GETSEL, i, 0L) > 0;
        st = sel ? TDS_SELECTED : 0;
        if (!combo && td_st.covered && blank(w, i))
            st |= TDS_BLANK;
        s.n = 0;
        if (drawn) {
            d.itemID = (UINT)i;
            d.itemState = sel ? ODS_SELECTED : 0;
            d.itemData = (DWORD)SendMessage(w, LB_GETITEMDATA, i, 0L);
            if (drops && (d.itemData & WL_DDROP))
                st |= TDS_DROP;
            d.rcItem = r;
            wcol_watch(&s);
            SendMessage(GetParent(w), WM_DRAWITEM, d.CtlID,
                        (LPARAM)(DRAWITEMSTRUCT FAR *)&d);
            wcol_watch(NULL);
            if (s.n != 0 && s.cut)
                st |= TDS_CUT;
        }
        if (strings) {
            if (SendMessage(w, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, i,
                            0L) > TD_MAXTEXT) {
                st |= TDS_LONG;
            } else {
                SendMessage(w, combo ? CB_GETLBTEXT : LB_GETTEXT, i,
                            (LPARAM)(LPSTR)text);
                if (dc != NULL && !drawn &&
                    (int)LOWORD(GetTextExtent(dc, text, (int)strlen(text))) >
                        r.right)
                    st |= TDS_CUT;
                to_oem(w, text);
                ln_text(&l, text);
            }
        }
        ln_states(&l, st);
        ln_put(&l);
        if (s.n != 0 && (i == 0 || sel))
            draw_line(&s, TDW_DRAW, 0, depth + 1);
    }
    if (dc != NULL)
        ReleaseDC(w, dc);
}

/* Paint w, a static or a list window of WLIST.H, once more while
   watched (wcol_watch), its drawing into *s, its paint pending, if any,
   done first, as the watched one draws nothing. */
static void watch(HWND w, wcol_seen *s)
{
    UpdateWindow(w);
    wcol_watch(s);
    SendMessage(w, WM_PAINT, 0, 0L);
    wcol_watch(NULL);
}

/* Write the HEAD line of w, a static or a list window of WLIST.H, at
   depth when its paint draws columns (watch), and its COLS line when
   they are headings. */
static void snap_head(HWND w, int depth)
{
    wcol_seen s;

    watch(w, &s);
    if (s.n != 0)
        draw_line(&s, TDW_HEAD, s.cut ? TDS_CUT : 0, depth);
    if (s.n != 0 && s.heads)
        cols_line(&s, depth);
}

/* Write the lines of the preview panel w (WPVM_LINE of WPREV.H) at
   depth: PV, then a field's label and its value, or a line of the notes
   alone, in quotes. */
static void snap_lines(HWND w, int depth)
{
    td_line l;
    char t[WPV_LINESZ];
    WPARAM i;
    int kind;

    for (i = 0; ; i++) {
        kind = (int)SendMessage(w, WPVM_LINE, i, (LPARAM)(LPSTR)t);
        if (kind == 0)
            return;
        ln_start(&l, depth);
        ln_add(&l, "PV");
        if (kind == WPV_FIELD)
            ln_text(&l, t);
        ln_text(&l, t + strlen(t) + 1);
        ln_put(&l);
    }
}

/* Write the window w at depth and, one deeper, its drawing, its menu
   bar's items, its rows or lines and its child windows. */
static void snap_window(HWND w, int depth)
{
    char cls[TD_CLASSLEN];
    HWND c;
    HMENU m;
    u32 style;
    int combo;
    int tool;

    GetClassName(w, cls, (int)sizeof cls);
    style = (u32)GetWindowLong(w, GWL_STYLE);
    window_line(w, depth, cls, style);
    if (txt_ieq(cls, k_static) || txt_ieq(cls, k_wlist))
        snap_head(w, depth + 1);
    m = (style & WS_CHILD) ? NULL : GetMenu(w);
    if (m != NULL)
        snap_menu(m, depth + 1);
    combo = txt_ieq(cls, k_combobox);
    if (combo || txt_ieq(cls, k_listbox))
        snap_rows(w, depth + 1, combo, style);
    if (txt_ieq(cls, k_wprev))
        snap_lines(w, depth + 1);
    tool = txt_ieq(cls, k_wtool);
    for (c = GetWindow(w, GW_CHILD); c != NULL; c = GetWindow(c, GW_HWNDNEXT))
        if (!tool || !is_button(c, BS_OWNERDRAW))
            snap_window(c, depth + 1);
}

/* Write the snapshot titled name of every top-level window of the
   program, the active one first, and commit it to the disk, as DOS
   writes a file's size into its directory entry only when a handle on
   it is closed. */
static void snapshot(const char *name)
{
    td_line l;
    HWND act;
    HWND w;

    if (td_st.snap < 0)
        return;
    ln_start(&l, 0);
    ln_add(&l, "=SNAP ");
    ln_add(&l, name);
    ln_put(&l);
    act = GetActiveWindow();
    if (seen(act))
        snap_window(act, 0);
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT))
        if (w != act && seen(w))
            snap_window(w, 0);
    td_st.covered = 0;
    if (td_st.snap >= 0)
        dos_commit_handle(td_st.snap);
}

/* Set SHIFT, CTRL and ALT in Windows' key state to the modifiers of the
   key code key, CAPS LOCK off. */
static void set_mods(u16 key)
{
    BYTE ks[TD_NKEYS];

    GetKeyboardState(ks);
    ks[VK_SHIFT] = (BYTE)(key & TD_SHIFT ? TD_DOWN : 0);
    ks[VK_CONTROL] = (BYTE)(key & TD_CTRL ? TD_DOWN : 0);
    ks[VK_MENU] = (BYTE)(key & TD_ALT ? TD_DOWN : 0);
    ks[VK_CAPITAL] = 0;
    SetKeyboardState(ks);
}

/* Post the press (up 0) or release (up 1) of the key code key to the
   window to: a system key with ALT but not CTRL, and for F10. */
static void post_key(HWND to, u16 key, int up)
{
    UINT vk;
    u32 lp;
    int sys;

    vk = LOBYTE(key);
    sys = (key & (TD_ALT | TD_CTRL)) == TD_ALT || vk == VK_F10;
    lp = 1UL | (u32)MapVirtualKey(vk, 0) << TD_LPSCAN;
    if (key & TD_ALT)
        lp |= TD_LPALT;
    if (up)
        lp |= TD_LPUP;
    PostMessage(to, sys ? (up ? WM_SYSKEYUP : WM_SYSKEYDOWN)
                        : (up ? WM_KEYUP : WM_KEYDOWN), vk, (LPARAM)lp);
}

/* The coordinate a moved d pixels, held within TD_MAXNUM either way,
   which a base far off plus an offset of TD_MAXNUM would pass. */
static int moved(int a, int d)
{
    i32 v;

    v = (i32)a + d;
    return v < -TD_MAXNUM ? -TD_MAXNUM : v > TD_MAXNUM ? TD_MAXNUM : (int)v;
}

/* Post the mouse message of the M line whose arguments are at l
   (TDRV.H): the point found from the window its path names, in screen
   coordinates, then in the client area of the window of the program
   holding the mouse, which gets the message, else of the window named;
   the buttons held, kept in Windows' key state, its wParam.  0 when the
   window or its base is not found. */
static int mouse(char *l)
{
    td_mouse m;
    wcol_seen s;
    BYTE ks[TD_NKEYS];
    char cls[TD_CLASSLEN];
    POINT pt;
    RECT r;
    HWND w;
    HWND box;
    HWND to;
    UINT msg;

    mouse_args(l, &m);
    walk(m.path, &w);
    if (w == NULL)
        return 0;
    GetClassName(w, cls, (int)sizeof cls);
    pt.x = 0;
    pt.y = 0;
    if (m.base == TDW_ROW) {
        if (!txt_ieq(cls, k_listbox) ||
            SendMessage(w, LB_GETITEMRECT, (WPARAM)m.k,
                        (LPARAM)(RECT FAR *)&r) == LB_ERR)
            return 0;
        pt.x = (r.left + r.right) / 2;
        pt.y = (r.top + r.bottom) / 2;
    } else if (m.base == TDW_HEAD || m.base == TDW_DIV) {
        box = GetWindow(w, GW_CHILD);
        if (!txt_ieq(cls, k_wlist) || box == NULL)
            return 0;
        watch(w, &s);
        if (!s.heads || m.k >= s.n)
            return 0;
        GetWindowRect(box, &r);
        ScreenToClient(w, (POINT FAR *)&r.left);
        pt.x = GetSystemMetrics(SM_CXBORDER) + s.pc[m.k].sx +
               (m.base == TDW_HEAD ? s.pc[m.k].sw / 2 : s.pc[m.k].sw);
        pt.y = r.top / 2;
    } else if (m.base == TDW_BTN) {
        if (!txt_ieq(cls, k_wtool))
            return 0;
        w = (HWND)(UINT)SendMessage(w, WTBM_BUTTON, (WPARAM)m.k, 0L);
        if (w == NULL)
            return 0;
    }
    if (m.base == TDW_MID || m.base == TDW_BTN) {
        GetClientRect(w, &r);
        pt.x = r.right / 2;
        pt.y = r.bottom / 2;
    }
    pt.x = moved(pt.x, m.dx);
    pt.y = moved(pt.y, m.dy);
    ClientToScreen(w, &pt);
    to = GetCapture();
    if (!ours(to))
        to = w;
    ScreenToClient(to, &pt);
    msg = m.msg;
    set_mods(m.mods);
    GetKeyboardState(ks);
    if (msg != WM_MOUSEMOVE) {
        ks[msg < WM_RBUTTONDOWN ? VK_LBUTTON : VK_RBUTTON] =
            (BYTE)(msg == WM_LBUTTONUP || msg == WM_RBUTTONUP ? 0 : TD_DOWN);
        SetKeyboardState(ks);
        if ((msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK) &&
            !(GetClassWord(to, GCW_STYLE) & CS_DBLCLKS))
            msg -= TD_DBL;
    }
    PostMessage(to, msg, (ks[VK_LBUTTON] & TD_DOWN ? MK_LBUTTON : 0) |
                         (ks[VK_RBUTTON] & TD_DOWN ? MK_RBUTTON : 0) |
                         (m.mods & TD_SHIFT ? MK_SHIFT : 0) |
                         (m.mods & TD_CTRL ? MK_CONTROL : 0),
                MAKELPARAM(pt.x, pt.y));
    return 1;
}

/* The command letter, in upper case, of the first line at *pos or
   after it but comments and blank lines, read into buf (TD_MAXLINE + 2
   bytes), *pos moved past it and the lines read added to *lines; '\0'
   at the script's end or a line that cannot be read. */
static char next_cmd(char *buf, u32 *pos, u16 *lines)
{
    char c;

    for (;;) {
        if (fetch(*pos, buf, pos) < 0)
            return '\0';
        ++*lines;
        c = buf[0];
        c = (char)TXT_UPPER(c);
        if (c != '#' && c != '\0')
            return c;
    }
}

/* Type ahead to the window to the keys of the lines of command letter
   cmd, 'P' or 'A', that follow the line just carried out, comments and
   blank lines aside: each key's press and release posted at once,
   behind the key or mouse message before them, so that the program
   finds them waiting while it handles that one, or starts the request
   it asked for them at (td_ahead). */
static void ahead(HWND to, char cmd)
{
    char buf[TD_MAXLINE + 2];
    char *l;
    char *w;
    u32 pos;
    u16 lines;
    u16 key;

    pos = td_st.pos;
    lines = 0;
    while (next_cmd(buf, &pos, &lines) == cmd) {
        td_st.line += lines;
        lines = 0;
        td_st.pos = pos;
        for (l = buf + 1; (w = next_word(&l)) != NULL; ) {
            key = key_named(w, (u16)strlen(w));
            post_key(to, key, 0);
            post_key(to, key, 1);
        }
    }
}

/* Stop the timer and free its thunk. */
static void untime(void)
{
    if (td_st.timer != 0)
        KillTimer(NULL, td_st.timer);
    if (td_st.tick != NULL)
        FreeProcInstance((FARPROC)td_st.tick);
    td_st.timer = 0;
    td_st.tick = NULL;
}

/* End the run with why, a TDF_ code: stop the timer and, unless it has
   ended already, the program's loop; td_done reports it.  An open menu
   is closed first (WM_CANCELMODE), as Windows' menu loop does not end
   on WM_QUIT. */
static void stop(int why)
{
    HWND act;

    td_st.why = why;
    untime();
    if (td_st.ended)
        return;
    act = GetActiveWindow();
    if (ours(act))
        SendMessage(act, WM_CANCELMODE, 0, 0L);
    PostQuitMessage(0);
}

/* Ask every top-level window of the program WM_QUERYENDSESSION, then
   send each WM_ENDSESSION, its wParam TRUE when every one agreed, as
   Windows ends its session (a Q line); 1 when every one agreed. */
static int end_session(void)
{
    HWND w;
    int ok;

    ok = 1;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT))
        if (ours(w) && SendMessage(w, WM_QUERYENDSESSION, 0, 0L) == 0)
            ok = 0;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT))
        if (ours(w))
            SendMessage(w, WM_ENDSESSION, (WPARAM)ok, 0L);
    return ok;
}

/* Open again each minimized top-level window of the program (an O
   line). */
static void reopen(void)
{
    HWND w;

    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT))
        if (ours(w) && IsIconic(w))
            OpenIcon(w);
}

/* Have the window w and every window inside it erased and painted
   again by the messages to come. */
static void dirty(HWND w)
{
    HWND c;

    InvalidateRect(w, NULL, TRUE);
    for (c = GetWindow(w, GW_CHILD); c != NULL; c = GetWindow(c, GW_HWNDNEXT))
        dirty(c);
}

/* Leave the program's active window as another program's window lying
   over it does when the user switches to that and back (a C line): the
   window and all inside it to be erased and painted again (dirty), and
   the focus, taken away meanwhile, given back to its window while
   their paint messages still wait; nothing while Windows Help's window
   is the active one.  No window of the driver's could cover it for
   that: Windows erases at once what a window of the same program
   uncovers. */
static void cover(void)
{
    HWND a;
    HWND f;

    a = GetActiveWindow();
    if (!ours(a))
        return;
    f = SetFocus(NULL);
    dirty(a);
    SetFocus(f);
    td_st.covered = 1;
}

/* 1 when the script's next line, comments and blank lines aside, is an
   O line. */
static int o_next(void)
{
    char buf[TD_MAXLINE + 2];
    u32 pos;
    u16 lines;

    pos = td_st.pos;
    lines = 0;
    return td_st.kind == 0 && next_cmd(buf, &pos, &lines) == 'O';
}

/* Carry out the script from pos up to and including its next key,
   typed to the window to, with the keys typed ahead after it (ahead),
   mouse message (M), opening of the program's minimized windows (O) or
   painting again of its active one (C), writing the snapshots and
   ending the session (Q) on the way, but the first of them after a key,
   a mouse message, an O line or a C line only on the next idle tick
   (TDRV.H); stop at its end or an E line, and once the program's loop
   has ended at a key, an M line or a Q line. */
static void play(HWND to)
{
    char buf[TD_MAXLINE + 2];
    char *p;
    const char *w;
    u32 next;
    int n;
    char c;
    char a;

    for (;;) {
        n = fetch(td_st.pos, buf, &next);
        if (n == -2) {
            stop(TDF_SCRIPT);
            return;
        }
        if (n == -1)
            buf[0] = '\0';
        p = buf;
        if (td_st.kind == 0) {
            if (n == -1) {
                stop(TDF_END);
                return;
            }
            c = buf[0];
            c = (char)TXT_UPPER(c);
            if ((c == 'S' || c == 'Q' || c == 'O' || c == 'C') &&
                td_st.typed && !td_st.ended) {
                td_st.typed = 0;
                return;
            }
            td_st.line++;
            if (c == 'E') {
                stop(TDF_END);
                return;
            }
            if (c == 'Q' && td_st.ended) {
                stop(TDF_ENDED);
                return;
            }
            if (c == 'Q' && end_session()) {
                /* Windows ends the program without returning to it,
                   whatever the run. */
                untime();
                td_st.quit = 1;
                td_st.pos = next;
                td_done();
                return;
            }
            if (c == 'O' || c == 'C') {
                if (c == 'O')
                    reopen();
                else
                    cover();
                td_st.typed = 1;
                td_st.pos = next;
                return;
            }
            if (c == 'S')
                snapshot(snap_name(buf + 1));
            if (c == 'W')
                td_st.maxwait = wait_ms(buf + 1);
            if (c == 'M') {
                if (td_st.ended || !mouse(buf + 1)) {
                    stop(td_st.ended ? TDF_ENDED : TDF_TARGET);
                    return;
                }
                td_st.typed = 1;
                td_st.pos = next;
                ahead(to, 'P');
                return;
            }
            if (c == 'K' || c == 'P' || c == 'A' || c == 'T') {
                td_st.kind = (u8)(c == 'T' ? 'T' : 'K');
                p = buf + (c == 'T' && buf[1] != '\0' ? 2 : 1);
            } else {
                p = buf + n;
            }
        }
        if (td_st.kind == 'K')
            while (is_sp(*p))
                p++;
        if (*p != '\0') {
            if (td_st.ended) {
                stop(TDF_ENDED);
                return;
            }
            if (td_st.kind == 'T') {
                OemToAnsiBuff(p, &a, 1);
                PostMessage(to, WM_CHAR, (WPARAM)(u8)a, 1L);
                p++;
            } else {
                w = p;
                while (*p != '\0' && !is_sp(*p))
                    p++;
                td_st.up = key_named(w, (u16)(p - w));
                set_mods(td_st.up);
                post_key(to, td_st.up, 0);
                while (is_sp(*p))
                    p++;
            }
            td_st.typed = 1;
            if (*p != '\0') {
                td_st.pos += (u32)(p - buf);
                return;
            }
            td_st.kind = 0;
            td_st.pos = next;
            ahead(to, 'P');
            return;
        }
        td_st.kind = 0;
        td_st.pos = next;
    }
}

/* 1 when the span lo..hi of a dialog on one axis lies as WDLG.H places
   it on its owner's span a..b, on a screen of 0..max: at 0 when longer
   than the screen, else on it and centred, its margins differing by a
   pixel at most, or against the edge of the screen it would have
   passed. */
static int centred(int lo, int hi, int a, int b, int max)
{
    int off;

    if (hi - lo > max)
        return lo == 0;
    off = (lo - a) - (b - hi);
    return off > 1 ? lo == 0 : off < -1 ? hi == max : lo >= 0 && hi <= max;
}

/* 1 when w is a dialog whose place td_place checks: of class #32770 and
   the program's instance, as a message box is Windows', which centres
   it on the screen, and of a fixed size, as a sizable one is placed by
   its owner and its user (WRPT.H's preview). */
static int followed(HWND w)
{
    char cls[TD_CLASSLEN];

    GetClassName(w, cls, (int)sizeof cls);
    return txt_ieq(cls, k_dialog) &&
           (HINSTANCE)GetWindowWord(w, GWW_HINSTANCE) == td_st.inst &&
           !(GetWindowLong(w, GWL_STYLE) & WS_THICKFRAME);
}

/* Check each top-level dialog of the program that td_place follows the
   first time it is seen, noting the first that td_placed refuses for
   td_done; one seen again is not checked again while it lasts, as its
   owner may move after it has opened.  A dialog seen beyond TD_NDLGS at
   once is checked each time. */
static void places(void)
{
    HWND w;
    int i;
    int slot;

    for (i = 0; i < TD_NDLGS; i++)
        if (td_st.dlgs[i] != NULL && !IsWindow(td_st.dlgs[i]))
            td_st.dlgs[i] = NULL;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT)) {
        if (!ours(w) || !followed(w))
            continue;
        slot = -1;
        for (i = 0; i < TD_NDLGS && td_st.dlgs[i] != w; i++)
            if (td_st.dlgs[i] == NULL)
                slot = i;
        if (i < TD_NDLGS)
            continue;
        if (!td_placed(w) && !td_st.off) {
            td_st.off = 1;
            td_st.offline = td_st.line;
        }
        if (slot >= 0)
            td_st.dlgs[slot] = w;
    }
}

/* The driver's timer procedure: check the dialogs' place when
   td_place asks, then release the key pressed on the last tick, else,
   when the program is idle, or not busy with an O line next and no
   window of its own holding the focus, carry out the script; fail when
   the program has not been idle for the wait limit.  That is measured
   with GetTickCount, as a limit measured with the time Windows 3.0
   passes never ran out. */
void WCALLBACK td_tick(HWND wnd, UINT msg, UINT id, DWORD at)
{
    HWND to;
    HWND act;
    u32 now;

    (void)wnd;
    (void)msg;
    (void)id;
    (void)at;
    if (td_st.place)
        places();
    to = GetFocus();
    if (td_st.up != 0) {
        if (seen(to))
            post_key(to, td_st.up, 1);
        td_st.up = 0;
        set_mods(0);
        return;
    }
    now = GetTickCount();
    act = GetActiveWindow();
    if (!td_st.busy && !seen(to) && o_next()) {
        td_st.since = now;
        play(to);
        return;
    }
    if (td_st.busy || !seen(to) || (seen(act) && !IsWindowVisible(act))) {
        if (now - td_st.since > td_st.maxwait)
            stop(td_st.busy ? TDF_BUSY : TDF_FOCUS);
        return;
    }
    td_st.since = now;
    play(to);
}

/* End the run that td_init cannot start with why, a TDF_ code, there
   and then (td_done), the quit left for a program that Windows does
   not end. */
static void refuse(int why)
{
    stop(why);
    td_done();
}

void td_init(HINSTANCE inst, const char *keys, const char *snap)
{
    u16 bad;

    if (keys == NULL)
        return;
    cstk_fill();
    td_st.keys = -1;
    td_st.snap = -1;
    td_st.why = TDF_RUN;
    td_st.maxwait = TD_MAXWAIT;
    td_st.on = 1;
    td_st.quit = (u8)(snap != NULL);
    if ((snap != NULL && cf_create(snap, &td_st.snap) != 0) ||
        cf_open(keys, CF_READ | CF_DENYW, &td_st.keys) != 0) {
        refuse(TDF_SCRIPT);
        return;
    }
    bad = check();
    if (bad != 0) {
        td_st.line = bad;
        refuse(TDF_SCRIPT);
        return;
    }
    td_st.inst = inst;
    td_st.tick = (TIMERPROC)MakeProcInstance((FARPROC)td_tick, inst);
    td_st.timer = SetTimer(NULL, 0, TD_TICKMS, td_st.tick);
    if (td_st.timer == 0) {
        refuse(TDF_TIMER);
        return;
    }
    /* Windows 3.0's USER runs every timer from one system timer, which
       SetTimer starts before it enters the new timer in its table; a
       tick in between finds no timer and stops it, so a timer set while
       no other runs may never fire.  Setting and killing a second one
       counts this one and starts the system timer again. */
    KillTimer(NULL, SetTimer(NULL, 0, TD_TICKMS, td_st.tick));
    td_st.since = GetTickCount();
}

void td_busy(int busy)
{
    td_st.busy = (u8)(busy != 0);
    if (td_st.place && td_st.on)
        places();
}

void td_ahead(void)
{
    char buf[TD_MAXLINE + 2];
    u32 pos;
    u16 lines;

    if (!td_st.on || td_st.kind != 0 || td_st.why != TDF_RUN)
        return;
    ahead(GetFocus(), 'A');
    pos = td_st.pos;
    lines = 0;
    if (next_cmd(buf, &pos, &lines) == 'E') {
        td_st.line += lines;
        stop(TDF_END);
    }
}

void td_place(void)
{
    td_st.place = 1;
}

int td_placed(HWND dlg)
{
    RECT d;
    RECT o;
    HWND w;
    int cx;
    int cy;

    cx = GetSystemMetrics(SM_CXSCREEN);
    cy = GetSystemMetrics(SM_CYSCREEN);
    w = GetWindow(dlg, GW_OWNER);
    if (w == NULL || IsIconic(w))
        SetRect(&o, 0, 0, cx, cy);
    else
        GetWindowRect(w, &o);
    GetWindowRect(dlg, &d);
    return centred(d.left, d.right, o.left, o.right, cx) &&
           centred(d.top, d.bottom, o.top, o.bottom, cy);
}

void td_done(void)
{
    td_line l;
    char d[TXT_U32LEN + 1];
    u16 n;

    if (!td_st.on)
        return;
    if (td_st.up != 0)
        set_mods(0);
    td_st.up = 0;
    td_st.ended = 1;
    if (td_st.why == TDF_RUN)
        play(NULL);
    if (td_st.why == TDF_END && td_st.off) {
        td_st.why = TDF_PLACE;
        td_st.line = td_st.offline;
    }
    ln_start(&l, 0);
    if (td_st.why == TDF_END) {
        ln_add(&l, "=END");
    } else {
        ln_add(&l, "=FAIL");
        ln_num(&l, td_st.line);
        ln_char(&l, ' ');
        ln_word(&l, td_fails, (u16)td_st.why);
    }
    ln_put(&l);
    n = cstk_unused();
    if (n != CSTK_UNKNOWN) {
        txt_u32(d, n);
        ln_start(&l, 0);
        ln_add(&l, k_stack);
        ln_add(&l, d);
        ln_put(&l);
    }
    if (td_st.snap >= 0)
        cf_close(td_st.snap);
    if (td_st.keys >= 0)
        cf_close(td_st.keys);
    td_st.on = 0;
    if (td_st.quit)
        ExitWindows(0L, 0);
}
