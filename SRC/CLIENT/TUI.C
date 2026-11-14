/*
 * TUI.C - screen primitives, dialogs, menus and the main loop of the
 * full-screen client (TUI.H).
 *
 * Everything is drawn on the shadow screen of CVIDEO.C; tui_key flushes
 * it before each key is read.  Texts given to the functions here are
 * copied into buffers of their own (on the stack for dialogs, in near
 * data for the hints and the status line information) before any
 * message is fetched or any key read, so callers may pass msg()
 * results straight in.
 *
 * Global keys: tui_global answers F3, F10, Ctrl-F and Alt-letter.  When
 * F3 is answered No, or a menu is closed with Esc, nothing was asked for,
 * and the key is reported as not used (*used 0), so the screen simply
 * stays.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#define RQERR_CLASS
#include "RQERR.H"
#define TUI_POPS
#define TUI_CLICKS
#define TUI_VERBS
#define TUI_VERBTAB
#define TUI_SELECT
#include "TUI.H"

/* TUI.C's own messages (the shared ones are in TUI.H). */
#define MT_LOCAL        25      /* LOCAL                                */
#define MT_NET          26      /* NET                                  */
#define MT_COM          41      /* COM                                  */
#define MT_HELPTITLE    27      /* Help box title                       */
#define MT_HINTHELP     28      /* hints of the help box                */
#define MT_HINTYN       30      /* hints of a question                  */
#define MT_HINTPROMPT   31      /* hints of a prompt                    */
#define MT_HINTMENU     32      /* hints of an open menu                */
#define MT_RECORD       51      /* the Record menu's title: Re&cord     */

/* The key names, KEYSCR.MSG's messages (the MSG_R_KEYSCR range), which
   tui_keyname matches: MK_F1..MK_BREAK name the keys of tui_keycodes,
   in order; MK_ALT and MK_CTRL are the prefixes of ALT-A..ALT-Z and
   CTRL-A..CTRL-Z. */
#define MK_F1           350
#define MK_F2           351
#define MK_F3           352
#define MK_F4           353
#define MK_F5           354
#define MK_F6           355
#define MK_F7           356
#define MK_F8           357
#define MK_F9           358
#define MK_F10          359
#define MK_ENTER        360
#define MK_ESC          361
#define MK_TAB          362
#define MK_STAB         363
#define MK_BS           364
#define MK_SPACE        365
#define MK_UP           366
#define MK_DOWN         367
#define MK_LEFT         368
#define MK_RIGHT        369
#define MK_PGUP         370
#define MK_PGDN         371
#define MK_HOME         372
#define MK_END          373
#define MK_INS          374
#define MK_DEL          375
#define MK_CLEFT        376
#define MK_CRIGHT       377
#define MK_CHOME        378
#define MK_CEND         379
#define MK_CPGUP        380
#define MK_CPGDN        381
#define MK_BREAK        382
#define MK_ALT          383     /* prefix of ALT-A..ALT-Z               */
#define MK_CTRL         384     /* prefix of CTRL-A..CTRL-Z             */

/* The help box spans the screen's width, so nothing of a row it covers
   (a list row's text or selection bar) shows beside it.  Its lines hold
   TUI_HELPW characters, the width INV.HLP lines keep to, from column
   TUI_HELPCOL, which centres them; at most TUI_HELPLINES lines of a
   section are shown.  The section is read through the file's index with
   one read into a far block (msg_section), where its lines are packed
   in place, split from it through a buffer of TUI_HELPBUF bytes.  F9,
   in the box or anywhere else, shows section TUI_HELPKEYS, the keys
   every screen has. */
#define TUI_HELPTOP     (TUI_TOP + 1)
#define TUI_HELPH       (TUI_WORKROWS - 2)
#define TUI_HELPW       70
#define TUI_HELPCOL     ((VID_COLS - TUI_HELPW) / 2)
#define TUI_HELPLINES   48
#define TUI_HELPBUF     128
#define TUI_HELPKEYS    "KEYS"

/* Text of a message box, its lines and their width; the box is at
   least TUI_BOXMINW columns wide. */
#define TUI_BOXTEXT     (2 * MSG_MAXLEN)
#define TUI_BOXLINES    12
#define TUI_BOXW        60
#define TUI_BOXMINW     24

/* A prompt's box: TUI_PROMPTH rows, its field at most TUI_PROMPTW
   cells. */
#define TUI_PROMPTH     5
#define TUI_PROMPTW     40

/* key_wait's PING interval: half the least IDLE= of the server, one
   minute, as INV counts it from its PING's send and the server from its
   answer, so a PING at the full minute would race the server's end. */
#define TUI_PINGT       (3 * TMR_10SEC)

#define TUI_MAXMENU     8       /* menus of the menu bar                */
#define TUI_RECMAX      14      /* verbs of one Record menu at most     */
#define TUI_MENUMINW    10      /* the fewest columns of a pull-down    */
#define TUI_NOHOT       0xFF    /* draw_label: no hot-key letter drawn  */

tui_sess tui_s;

/* Palettes, indexed by TA_ role. */
static const u8 tui_pals[3][TA_NROLES] = {
    { 0x17, 0x1F, 0x1B, 0x1E, 0x30, 0x70, 0x78, 0x74, 0x70, 0x07, 0x70,
      0x4F, 0x17, 0x1E, 0x1C },
    { 0x07, 0x0F, 0x07, 0x0F, 0x70, 0x0F, 0x07, 0x09, 0x70, 0x01, 0x70,
      0x70, 0x07, 0x0F, 0x0F },
    { 0x07, 0x0F, 0x07, 0x0F, 0x70, 0x0F, 0x07, 0x0F, 0x70, 0x07, 0x70,
      0x70, 0x07, 0x0F, 0x0F }
};

static u8 tui_palk;
static tui_exit_fn tui_exitf;
static const char *tui_topicp = "";
static const char *tui_helpdir = "";
static char tui_helpbuf[LINK_MAXDIR + 1];
static const tui_app *tui_app_p;
static char tui_hintbuf[TUI_HINTBUF];
static char tui_infobuf[TUI_ST_INFOW + 1];
static u8 tui_msgup;            /* a message covers the hints           */
static u8 tui_inhelp;
static u16 tui_pend;            /* the key of a verb chosen, given by the
                                   next tui_key; KEY_NONE when none     */
static u32 tui_voper;           /* tui_verbs's: the verbs that need
                                   TUI_LVL_OPER, ...                    */
static u16 tui_vtext;           /* ... and the first one's text         */
static u32 tui_recv;            /* the screen's verbs (tui_record)      */
/* The Record menu: its entries are set only while tui_global runs. */
static tui_menu tui_recm = { MT_RECORD, 0, NULL };
static u32 tui_tick;            /* tmr_ticks at key_wait's last look    */
static u32 tui_waited;          /* ticks since its last PING            */

/* ---------------------------------------------------------------------
 * Palettes and drawing.
 * --------------------------------------------------------------------- */

void tui_palette(u8 kind)
{
    if (kind <= TP_MONOCGA)
        tui_palk = kind;
}

u8 tui_pal(void)
{
    return tui_palk;
}

u8 tui_attr(u8 role)
{
    if (role >= TA_NROLES)
        role = TA_NORMAL;
    return tui_pals[tui_palk][role];
}

/* Write n bytes of s, the bytes a screen must not hold drawn as '.'. */
static void put_clean(u8 row, u8 col, const char *s, u16 n, u8 attr)
{
    char buf[VID_COLS];
    u16 i;
    char c;

    if (col >= VID_COLS)
        return;
    if (n > (u16)(VID_COLS - col))
        n = (u16)(VID_COLS - col);
    for (i = 0; i < n; i++) {
        c = s[i];
        if (c == '\0' || c == '\n' || c == '\r' || c == '\032')
            c = '.';
        buf[i] = c;
    }
    vid_put(row, col, buf, n, attr);
}

void tui_clear(u8 top, u8 bottom)
{
    u8 r;

    for (r = top; r <= bottom && r < VID_ROWS; r++)
        vid_fill(r, 0, VID_COLS, ' ', tui_attr(TA_NORMAL));
}

void tui_text(u8 row, u8 col, const char *s, u8 role)
{
    put_clean(row, col, s, (u16)strlen(s), tui_attr(role));
}

void tui_textn(u8 row, u8 col, const char *s, u16 n, u8 role)
{
    size_t len;

    if (col >= VID_COLS)
        return;
    if (n > (u16)(VID_COLS - col))
        n = (u16)(VID_COLS - col);
    len = strlen(s);
    if (len > n)
        len = n;
    put_clean(row, col, s, (u16)len, tui_attr(role));
    if (len < n)
        vid_fill(row, (u8)(col + len), (u16)(n - len), ' ', tui_attr(role));
}

void tui_center(u8 row, u8 col, u8 w, const char *s, u8 role)
{
    size_t n;

    n = strlen(s);
    if (n > w)
        n = w;
    put_clean(row, (u8)(col + (w - n) / 2), s, (u16)n, tui_attr(role));
}

void tui_box(u8 top, u8 left, u8 h, u8 w, u8 style, const char *title)
{
    char t[VID_COLS + 1];
    u8 fa;
    u8 r;
    u8 hc;
    u8 vc;
    size_t n;

    if (h < 2 || w < 2)
        return;
    fa = tui_attr(TA_FRAME);
    hc = (u8)(style == TB_DOUBLE ? TF_DH : TF_SH);
    vc = (u8)(style == TB_DOUBLE ? TF_DV : TF_SV);
    t[0] = '\0';
    if (title != NULL && *title != '\0' && w > 6) {
        n = (size_t)(w - 6);
        if (n > sizeof t - 3)
            n = sizeof t - 3;
        t[0] = ' ';
        txt_copy(t + 1, title, (u16)(n + 1));
        strcat(t, " ");
    }
    vid_fill(top, left, 1, (u8)(style == TB_DOUBLE ? TF_DTL : TF_STL), fa);
    vid_fill(top, (u8)(left + 1), (u16)(w - 2), hc, fa);
    vid_fill(top, (u8)(left + w - 1), 1,
             (u8)(style == TB_DOUBLE ? TF_DTR : TF_STR), fa);
    for (r = 1; r + 1 < h; r++) {
        vid_fill((u8)(top + r), left, 1, vc, fa);
        vid_fill((u8)(top + r), (u8)(left + 1), (u16)(w - 2), ' ',
                 tui_attr(TA_NORMAL));
        vid_fill((u8)(top + r), (u8)(left + w - 1), 1, vc, fa);
    }
    vid_fill((u8)(top + h - 1), left, 1,
             (u8)(style == TB_DOUBLE ? TF_DBL : TF_SBL), fa);
    vid_fill((u8)(top + h - 1), (u8)(left + 1), (u16)(w - 2), hc, fa);
    vid_fill((u8)(top + h - 1), (u8)(left + w - 1), 1,
             (u8)(style == TB_DOUBLE ? TF_DBR : TF_SBR), fa);
    if (t[0] != '\0')
        tui_center(top, left, w, t, TA_TITLE);
}

/* ---------------------------------------------------------------------
 * Hints, messages, the menu bar and the status line.
 * --------------------------------------------------------------------- */

/* Lay tui_hintbuf out on the hint row from column 1, as tui_hint
   describes, and draw it when at is VID_COLS.  Otherwise draw nothing
   and return the key that a click on column at stands for (tui_key),
   or KEY_NONE when it hits no word that names one. */
static u16 hints(u8 at)
{
    const char *p;
    u8 col;
    u8 role;
    u8 keyrole;
    const char *w;
    u16 n;
    u16 k;
    u16 s;
    u16 e;

    p = tui_hintbuf;
    col = 1;
    if (at >= VID_COLS)
        vid_fill(TUI_HINTROW, 0, VID_COLS, ' ', tui_attr(TA_HINT));
    while (*p != '\0' && col < VID_COLS) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        role = TA_HINT;
        keyrole = TA_HINTKEY;
        if (*p >= '1' && *p <= '3') {
            if (tui_s.level < (u8)(*p - '0')) {
                role = TA_MENUOFF;
                keyrole = TA_MENUOFF;
            }
            p++;
        }
        w = p;
        while (*p != '\0' && *p != ' ')
            p++;
        n = (u16)(p - w);
        for (k = 0; k < n && w[k] != '='; k++)
            ;
        if (at >= VID_COLS) {
            put_clean(TUI_HINTROW, col, w, k, tui_attr(keyrole));
            if (k < n && col + k < VID_COLS)
                put_clean(TUI_HINTROW, (u8)(col + k), w + k, (u16)(n - k),
                          tui_attr(role));
        } else if (at >= col && at < col + n) {
            /* Of the names '/' joins, the one clicked, or the first
               when the click is past them. */
            at = (u8)(at - col < k ? at - col : 0);
            s = 0;
            for (e = 0; e < k && (w[e] != '/' || e < at); e++)
                if (w[e] == '/')
                    s = (u16)(e + 1);
            return tui_keyname(w + s, (u16)(e - s));
        }
        if (col + n + 1 >= VID_COLS)
            break;
        col = (u8)(col + n + 1);
    }
    return KEY_NONE;
}

void tui_hint(const char *s)
{
    tui_msgup = 0;
    tui_hint_keep(s);
}

void tui_hint_keep(const char *s)
{
    txt_copy(tui_hintbuf, s, (u16)sizeof tui_hintbuf);
    if (!tui_msgup)
        hints(VID_COLS);
}

void tui_msg(const char *s, int err)
{
    u8 role;

    role = (u8)(err ? TA_ERROR : TA_HINT);
    vid_fill(TUI_HINTROW, 0, VID_COLS, ' ', tui_attr(TA_HINT));
    vid_fill(TUI_HINTROW, 0, 1, ' ', tui_attr(role));
    tui_textn(TUI_HINTROW, 1, s, (u16)(strlen(s) + 1), role);
    tui_msgup = 1;
}

/* Write a label whose hot-key letter is marked with '&' ("&&" a literal
   '&') in at most w cells; returns the cells used.  The hot letter is
   drawn hot unless hot is TUI_NOHOT. */
static u8 draw_label(u8 row, u8 col, const char *s, u8 w, u8 role, u8 hot)
{
    u8 n;
    u8 r;
    char c;

    n = 0;
    while (*s != '\0' && n < w && col + n < VID_COLS) {
        r = role;
        c = *s++;
        if (c == '&') {
            c = *s++;
            if (c == '\0')
                break;
            if (c != '&' && hot != TUI_NOHOT)
                r = hot;
        }
        put_clean(row, (u8)(col + n), &c, 1, tui_attr(r));
        n++;
    }
    return n;
}

/* Visible length of a label; its hot-key letter, upper case ('\0' when
   it has none), into *hotc when hotc is not NULL. */
static u8 label_len(const char *s, char *hotc)
{
    u8 n;
    char hot;
    char c;

    n = 0;
    hot = '\0';
    while (*s != '\0') {
        if (*s == '&') {
            s++;
            if (*s == '\0')
                break;
            c = *s;
            if (c != '&' && hot == '\0')
                hot = (char)TXT_UPPER(c);
        }
        s++;
        n++;
    }
    if (hotc != NULL)
        *hotc = hot;
    return n;
}

/* 1 when the user's level allows an entry of m. */
static int menu_on(const tui_menu *m)
{
    u8 i;

    for (i = 0; i < m->nitems; i++)
        if (tui_s.level >= m->items[i].level)
            return 1;
    return 0;
}

/* The application's menus of the bar: at most TUI_MAXMENU - 1, as the
   Record menu may follow them. */
static u8 app_menus(void)
{
    return (u8)(tui_app_p->nmenus >= TUI_MAXMENU ? TUI_MAXMENU - 1
                                                 : tui_app_p->nmenus);
}

/* Menus of the bar in use: the application's, then the Record menu
   while the screen gives it verbs. */
static u8 menu_count(void)
{
    return (u8)(app_menus() + (tui_recm.nitems != 0));
}

/* Menu i of the bar. */
static const tui_menu *menu(u8 i)
{
    if (i < app_menus())
        return &tui_app_p->menus[i];
    return &tui_recm;
}

/* The first menu of the bar whose hot letter is letter, or menu_count()
   when none has it. */
static u8 menu_by_letter(char letter)
{
    u8 i;
    char hc;

    for (i = 0; i < menu_count(); i++) {
        label_len(msg(menu(i)->label), &hc);
        if (hc == letter)
            break;
    }
    return i;
}

/* The first entry of m whose hot letter is letter, or m->nitems when
   none has it. */
static u8 item_by_letter(const tui_menu *m, char letter)
{
    u8 i;
    char hc;

    for (i = 0; i < m->nitems; i++) {
        label_len(msg(m->items[i].label), &hc);
        if (hc == letter)
            break;
    }
    return i;
}

/* Column of menu i's title in the menu bar, at most VID_COLS - 1. */
static u8 menu_col(u8 i)
{
    u16 col;
    u8 k;

    col = 1;
    for (k = 0; k < i && col < VID_COLS; k++)
        col = (u16)(col + label_len(msg(menu(k)->label), NULL)
                    + 2);
    return (u8)(col < VID_COLS ? col : VID_COLS - 1);
}

/* The menu whose title in the menu bar covers column c, or menu_count()
   when none does. */
static u8 menu_at(u8 c)
{
    u8 i;

    for (i = 0; i < menu_count(); i++)
        if (c >= menu_col(i) &&
            c < menu_col(i) + label_len(msg(menu(i)->label), NULL))
            break;
    return i;
}

/* The menu whose title in the menu bar the click key hits, or
   TUI_MAXMENU when key is no such click (none before login, when the
   bar is empty). */
static u8 bar_click(u16 key)
{
    int at;
    u8 i;

    at = tui_hit(key, TUI_MENUROW, 0, 1, VID_COLS);
    if (at < 0 || tui_app_p == NULL || tui_s.level == 0)
        return TUI_MAXMENU;
    i = menu_at((u8)at);
    return (u8)(i < menu_count() ? i : TUI_MAXMENU);
}

/* The key verb n gives. */
static u16 verb_key(u8 n)
{
    char t[MSG_MAXLEN + 1];
    const char *p;
    u16 k;

    msg_get((u16)(tui_vtext + n - 1), t, (u16)sizeof t);
    p = strstr(t, "  ");
    k = KEY_NONE;
    if (p != NULL) {
        while (*p == ' ')
            p++;
        k = tui_keyname(p, (u16)strlen(p));
    }
    return k != KEY_NONE ? k : (u16)(TUI_VERBKEY + n);
}

/* The access level verb n needs. */
static u8 verb_level(u8 n)
{
    return (u8)((tui_voper >> (n - 1)) & 1 ? TUI_LVL_OPER : TUI_LVL_READ);
}

/* 1 when the user's level allows a verb of the Record menu. */
static int rec_on(void)
{
    return tui_s.level >= ((tui_recv & ~tui_voper) != 0 ? TUI_LVL_READ
                                                         : TUI_LVL_OPER);
}

/* Give the Record menu its entries, in it (TUI_RECMAX of them), each
   entry's cmd its verb's number. */
static void rec_fill(tui_item *it)
{
    u32 v;
    u8 i;
    u8 n;

    i = 0;
    for (v = tui_recv, n = 1; i < tui_recm.nitems; v >>= 1, n++)
        if (v & 1) {
            it[i].label = (u16)(tui_vtext + n - 1);
            it[i].level = verb_level(n);
            it[i++].cmd = n;
        }
    tui_recm.items = it;
}

/* Draw the menu bar, menu open (or none, -1) selected; before login the
   bar is empty. */
static void draw_bar(int open)
{
    u8 i;
    u8 col;
    u8 on;
    char t[MSG_MAXLEN + 1];

    vid_fill(TUI_MENUROW, 0, VID_COLS, ' ', tui_attr(TA_MENU));
    if (tui_app_p == NULL || tui_s.level == 0)
        return;
    col = 1;
    for (i = 0; i < menu_count(); i++) {
        msg_get(menu(i)->label, t, (u16)sizeof t);
        on = (u8)(i < app_menus() ? menu_on(menu(i)) : rec_on());
        if (open == (int)i)
            col = (u8)(col + draw_label(TUI_MENUROW, col, t, VID_COLS,
                                        TA_SELECT, TUI_NOHOT));
        else
            col = (u8)(col + draw_label(TUI_MENUROW, col, t, VID_COLS,
                                        (u8)(on ? TA_MENU : TA_MENUOFF),
                                        (u8)(on ? TA_MENUHOT : TUI_NOHOT)));
        col = (u8)(col + 2);
        if (col >= VID_COLS)
            break;
    }
}

void tui_level_text(char *d)
{
    d[0] = (char)('0' + tui_s.level);
    d[1] = '\0';
}

/* Draw the status line: the station, the link, the user and level once
   logged in, and the information field. */
static void draw_status(void)
{
    char t[TUI_ST_USERW + 1];
    size_t n;

    vid_fill(TUI_STATROW, 0, VID_COLS, ' ', tui_attr(TA_STATUS));
    tui_textn(TUI_STATROW, TUI_ST_STA, tui_s.station, TUI_ST_STAW, TA_STATUS);
    if (tui_s.l != NULL)
        tui_textn(TUI_STATROW, TUI_ST_CONN,
                  msg(tui_s.conn == TUI_CONN_NET ? MT_NET :
                      tui_s.conn == TUI_CONN_COM ? MT_COM : MT_LOCAL),
                  TUI_ST_CONNW, TA_STATUS);
    if (tui_s.level > 0) {
        strcpy(t, tui_s.user);
        n = strlen(t);
        t[n] = ' ';
        tui_level_text(t + n + 1);
        tui_textn(TUI_STATROW, TUI_ST_USER, t, TUI_ST_USERW, TA_STATUS);
    }
    tui_textn(TUI_STATROW, TUI_ST_INFO, tui_infobuf, TUI_ST_INFOW,
              TA_STATUS);
}

void tui_frame(void)
{
    draw_bar(-1);
    draw_status();
}

void tui_info(const char *s)
{
    txt_copy(tui_infobuf, s, (u16)sizeof tui_infobuf);
    tui_textn(TUI_STATROW, TUI_ST_INFO, tui_infobuf, TUI_ST_INFOW,
              TA_STATUS);
}

const char *tui_info_text(void)
{
    return tui_infobuf;
}

/* ---------------------------------------------------------------------
 * Pop-ups (TUI.H).
 * --------------------------------------------------------------------- */
int tui_pop_open(tui_pop *p, u8 top, u8 left, u8 h, u8 w, u16 hint)
{
    p->save = vid_save(top, left, h, w);
    if (p->save == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return 0;
    }
    strcpy(p->hint, tui_hintbuf);
    if (hint != 0)
        tui_hint(msg(hint));
    return 1;
}

void tui_pop_close(tui_pop *p)
{
    vid_restore(p->save);
    tui_hint(p->hint);
}

/* ---------------------------------------------------------------------
 * Keys and help.
 * --------------------------------------------------------------------- */

u16 tui_key(void)
{
    u16 k;
    u16 h;
    int c;

    if (tui_pend != KEY_NONE) {
        k = tui_pend;
        tui_pend = KEY_NONE;
        return k;
    }
    for (;;) {
        vid_flush();
        k = key_read();
        if (k == KEY_EOS)
            tui_exit(0);
        if (tui_msgup) {
            tui_msgup = 0;
            hints(VID_COLS);
        } else if ((c = tui_hit(k, TUI_HINTROW, 0, 1, VID_COLS)) >= 0) {
            h = hints((u8)c);
            if (h != KEY_NONE)
                k = h;
        }
        if (k == KEY_BREAK) {
            vid_invalidate();
            return KEY_ESC;
        }
        if (tui_inhelp || (k != KEY_F1 && k != KEY_F9))
            return k;
        if (k == KEY_F9)
            tui_help(TUI_HELPKEYS);
        else if (*tui_topicp != '\0')
            tui_help(tui_topicp);
    }
}

/* A click's cell fits below KEY_DCLICK - KEY_CLICK. */
extern char tui_hit_fits[1];
extern char tui_hit_fits[(VID_COLS <= KEY_CROW &&
                          VID_ROWS * KEY_CROW <= KEY_DCLICK - KEY_CLICK)
                         ? 1 : 2];

int tui_hit(u16 k, u8 top, u8 left, u8 h, u8 w)
{
    u16 r;
    u16 c;

    /* Below KEY_CLICK, k wraps to a value past every cell; so do r and
       c above the rectangle and left of it. */
    k = (u16)(k - KEY_CLICK);
    if (k >= KEY_DCLICK - KEY_CLICK)
        return -1;
    r = (u16)(k / KEY_CROW - top);
    c = (u16)(k % KEY_CROW - left);
    if (r >= h || c >= w)
        return -1;
    return (int)(r * KEY_CROW + c);
}

/* Key codes of the names MK_F1..MK_BREAK, in the same order. */
static const u16 tui_keycodes[] = {
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8,
    KEY_F9, KEY_F10, KEY_ENTER, KEY_ESC, KEY_TAB, KEY_STAB, KEY_BS, ' ',
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_PGUP, KEY_PGDN, KEY_HOME,
    KEY_END, KEY_INS, KEY_DEL, KEY_CLEFT, KEY_CRIGHT, KEY_CHOME, KEY_CEND,
    KEY_CPGUP, KEY_CPGDN, KEY_BREAK
};

/* The letters in the order of their scan codes: Q..P from 10h, A..L
   from 1Eh, Z..M from 2Ch. */
static const char tui_letters[] = "QWERTYUIOPASDFGHJKLZXCVBNM";

/* 1 when the n bytes at w equal s, ignoring case. */
static int same(const char *w, u16 n, const char *s)
{
    return strlen(s) == n && txt_ieqn(w, s, n);
}

/* The key of tui_keycodes whose name is c and then the n bytes at w,
   or those bytes alone when c is 0; KEY_NONE when none is. */
static u16 key_named(char c, const char *w, u16 n)
{
    const char *s;
    u16 i;

    for (i = 0; i < sizeof tui_keycodes / sizeof tui_keycodes[0]; i++) {
        s = msg((u16)(MK_F1 + i));
        if (c == 0 ? same(w, n, s)
                   : TXT_UPPER(s[0]) == c && same(w, n, s + 1))
            return tui_keycodes[i];
    }
    return KEY_NONE;
}

u16 tui_keyname(const char *w, u16 n)
{
    char pre[MSG_MAXLEN + 1];
    u16 i;
    u16 v;
    u16 pl;
    int h;
    char c;

    if (n < 2)
        return n == 1 ? (u16)(u8)w[0] : KEY_NONE;
    v = key_named(0, w, n);
    if (v != KEY_NONE)
        return v;
    c = w[0];
    if (n == 5 && TXT_UPPER(c) == 'X') {
        v = 0;
        for (i = 1; i < 5; i++) {
            h = txt_hexval(w[i]);
            if (h < 0)
                return KEY_NONE;
            v = (u16)(v * 16 + (u16)h);
        }
        return (v == KEY_NONE || v == KEY_EOS) ? KEY_NONE : v;
    }
    msg_get(MK_ALT, pre, (u16)sizeof pre);
    pl = (u16)strlen(pre);
    c = w[n - 1];
    c = (char)TXT_UPPER(c);
    if (n == pl + 1 && same(w, pl, pre) && c >= 'A' && c <= 'Z') {
        for (i = 0; tui_letters[i] != c; i++)
            ;
        return (u16)(KEY_EXT + (i < 10 ? 0x10 + i
                                       : i < 19 ? 0x1E + i - 10
                                                : 0x2C + i - 19));
    }
    msg_get(MK_CTRL, pre, (u16)sizeof pre);
    pl = (u16)strlen(pre);
    if (n <= pl || !same(w, pl, pre))
        return KEY_NONE;
    if (n == pl + 1)
        return (u16)(c >= 'A' && c <= 'Z' ? c - 'A' + 1 : KEY_NONE);
    return key_named('C', w + pl, (u16)(n - pl));
}

int tui_esc_waiting(void)
{
    u16 k;

    while ((k = key_peek()) != KEY_NONE) {
        key_read();
        if (k == KEY_BREAK)
            vid_invalidate();
        if (k == KEY_ESC || k == KEY_BREAK)
            return 1;
    }
    return 0;
}

const char *tui_topic(const char *topic)
{
    const char *old;

    old = tui_topicp;
    tui_topicp = topic == NULL ? "" : topic;
    return old;
}

void tui_set_helpdir(const char *dir)
{
    txt_copy(tui_helpbuf, dir, (u16)sizeof tui_helpbuf);
    tui_helpdir = tui_helpbuf;
}

/* Read help section topic into a new far block *b and pack its lines
   there in place, each cut to TUI_HELPW characters and without its line
   end: line i of the *n is bytes off[i]..off[i + 1] - 1 (off holds
   TUI_HELPLINES + 1 entries).  Returns as msg_section does. */
static int help_read(const char *topic, cmem_blk **b, u16 *off, int *n)
{
    msg_rd r;
    char rbuf[TUI_HELPBUF];
    char line[TUI_HELPW + 1];
    u16 size;
    u16 len;
    int rc;

    *n = 0;
    rc = msg_section(tui_helpdir, TUI_HELP, topic, b, &size);
    if (rc != 0)
        return rc;
    /* A line takes at least its own length in the file, so it is never
       written past the bytes the reader has taken. */
    off[0] = 0;
    msg_rd_blk(&r, *b, 0, size, rbuf, (u16)sizeof rbuf);
    while (*n < TUI_HELPLINES &&
           msg_rd_line(&r, line, TUI_HELPW, &len) > 0) {
        len = (u16)strlen(line);
        cmem_put(*b, off[*n], line, len);
        off[*n + 1] = (u16)(off[*n] + len);
        ++*n;
    }
    return 0;
}

/* Show the n lines of help in b, packed as help_read leaves them, in
   the open help box, titled title, scrolling until Esc, Enter or F9;
   that key. */
static u16 help_show(cmem_blk *b, const u16 *off, int n, const char *title)
{
    char line[TUI_HELPW + 1];
    u16 len;
    int top;
    int i;
    int rows;
    u16 k;

    top = 0;
    rows = TUI_HELPH - 2;
    for (;;) {
        tui_box(TUI_HELPTOP, 0, TUI_HELPH, VID_COLS, TB_DOUBLE, title);
        for (i = 0; i < rows && top + i < n; i++) {
            len = (u16)(off[top + i + 1] - off[top + i]);
            cmem_get(b, off[top + i], line, len);
            line[len] = '\0';
            tui_text((u8)(TUI_HELPTOP + 1 + i), TUI_HELPCOL, line, TA_NORMAL);
        }
        k = tui_key();
        if (k == KEY_ESC || k == KEY_ENTER || k == KEY_F9)
            return k;
        if (k == KEY_DOWN && top + rows < n)
            top++;
        else if (k == KEY_UP && top > 0)
            top--;
        else if (k == KEY_PGDN)
            top = (top + 2 * rows <= n) ? top + rows
                                        : (n > rows ? n - rows : 0);
        else if (k == KEY_PGUP)
            top = top > rows ? top - rows : 0;
        else if (k == KEY_HOME)
            top = 0;
        else if (k == KEY_END)
            top = n > rows ? n - rows : 0;
    }
}

void tui_help(const char *topic)
{
    char title[MSG_MAXLEN + 1];
    u16 off[TUI_HELPLINES + 1];
    tui_pop pop;
    cmem_blk *b;
    int n;
    int rc;
    u8 crow;
    u8 ccol;
    u8 cshape;

    rc = help_read(topic, &b, off, &n);
    if (rc != 0) {
        tui_msg(rc == CF_ENOMEM ? msg(MT_NOMEM)
                                : msg_sub(MT_NOHELP, topic, NULL), 1);
        return;
    }
    msg_get(MT_HELPTITLE, title, (u16)sizeof title);
    if (!tui_pop_open(&pop, TUI_HELPTOP, 0, TUI_HELPH, VID_COLS,
                      MT_HINTHELP)) {
        cmem_free(b);
        return;
    }
    /* The cursor of a field being typed in is hidden under the box and
       comes back with the field.  Without the keys' section the box goes
       on showing topic. */
    vid_cursor_get(&crow, &ccol, &cshape);
    vid_cursor(0, 0, VID_CUR_HIDE);
    tui_inhelp = 1;
    while (help_show(b, off, n, title) == KEY_F9) {
        cmem_free(b);
        if (help_read(TUI_HELPKEYS, &b, off, &n) == 0)
            topic = TUI_HELPKEYS;
        else if (help_read(topic, &b, off, &n) != 0)
            break;
    }
    tui_inhelp = 0;
    cmem_free(b);
    tui_pop_close(&pop);
    vid_cursor(crow, ccol, cshape);
}

/* ---------------------------------------------------------------------
 * Dialogs.
 * --------------------------------------------------------------------- */

/* Break text into lines of at most TUI_BOXW characters, at '\n' and at
   spaces; start and length of each into st and ln; the count. */
static u8 wrap(const char *text, u16 *st, u8 *ln)
{
    u16 p;
    u16 len;
    u16 e;
    u16 sp;
    u8 n;

    p = 0;
    len = (u16)strlen(text);
    n = 0;
    while (p < len && n < TUI_BOXLINES) {
        e = p;
        sp = 0xFFFFu;
        while (e < len && text[e] != '\n' && e - p < TUI_BOXW) {
            if (text[e] == ' ')
                sp = e;
            e++;
        }
        if (e < len && text[e] != '\n' && text[e] != ' ' && sp != 0xFFFFu)
            e = sp;
        st[n] = p;
        ln[n] = (u8)(e - p);
        n++;
        p = e;
        if (p < len && (text[p] == '\n' || text[p] == ' '))
            p++;
    }
    if (n == 0) {
        st[0] = 0;
        ln[0] = 0;
        n = 1;
    }
    return n;
}

/* Draw title and text as a message box centred in the work area, as
   tui_showbox describes, the cursor hidden; when p is not NULL, the box
   is opened as a pop-up with the hints of message hint first.  0 when
   that pop-up could not open (tui_pop_open), and nothing is drawn; else 1. */
static int draw_box(const char *title, const char *text, tui_pop *p,
                    u16 hint)
{
    u16 st[TUI_BOXLINES];
    u8 ln[TUI_BOXLINES];
    u8 n;
    u8 i;
    u8 w;
    u8 h;
    u8 top;
    u8 left;
    u8 tl;
    char line[TUI_BOXW + 1];

    n = wrap(text, st, ln);
    w = TUI_BOXMINW;
    for (i = 0; i < n; i++)
        if (ln[i] + 4 > w)
            w = (u8)(ln[i] + 4);
    tl = (u8)strlen(title);
    if (tl + 6 > w)
        w = (u8)(tl + 6 > VID_COLS ? VID_COLS : tl + 6);
    h = (u8)(n + 4);
    top = (u8)(TUI_TOP + (TUI_WORKROWS - h) / 2);
    left = (u8)((VID_COLS - w) / 2);
    if (p != NULL && !tui_pop_open(p, top, left, h, w, hint))
        return 0;
    vid_cursor(0, 0, VID_CUR_HIDE);
    tui_box(top, left, h, w, TB_DOUBLE, title);
    for (i = 0; i < n; i++) {
        memcpy(line, text + st[i], ln[i]);
        line[ln[i]] = '\0';
        tui_text((u8)(top + 2 + i), (u8)(left + 2), line, TA_NORMAL);
    }
    return 1;
}

void tui_showbox(const char *title, const char *text)
{
    draw_box(title, text, NULL, 0);
}

void tui_msgbox(const char *title, const char *text)
{
    char t[MSG_MAXLEN + 1];
    char x[TUI_BOXTEXT + 1];
    tui_pop pop;
    u16 k;

    txt_copy(t, title, (u16)sizeof t);
    txt_copy(x, text, (u16)sizeof x);
    if (!draw_box(t, x, &pop, MT_HINTOK)) {
        tui_msg(x, 1);
        return;
    }
    do
        k = tui_key();
    while (k != KEY_ENTER && k != KEY_ESC &&
           tui_hit(k, 0, 0, VID_ROWS, VID_COLS) < 0);
    tui_pop_close(&pop);
}

int tui_confirm(const char *question)
{
    char q[TUI_BOXTEXT + 1];
    char yn[MSG_MAXLEN + 1];
    tui_pop pop;
    u16 k;
    int ans;

    txt_copy(q, question, (u16)sizeof q);
    msg_get(MT_YESNO, yn, (u16)sizeof yn);
    if (strlen(yn) < 2)
        strcpy(yn, "YN");
    if (!draw_box("", q, &pop, MT_HINTYN))
        return 0;
    ans = -1;
    while (ans < 0) {
        k = tui_key();
        if (k < 0x100)
            k = (u16)TXT_UPPER(k);
        if (k == (u16)(u8)yn[0])
            ans = 1;
        else if (k == (u16)(u8)yn[1] || k == KEY_ESC)
            ans = 0;
    }
    tui_pop_close(&pop);
    return ans;
}

/* Draw the edit field: cells from off of buf, cursor at pos. */
static void input_draw(u8 row, u8 col, u8 dispw, const char *buf, u16 off,
                       u16 pos, u8 flags, int ins, u8 role)
{
    u16 len;
    u16 n;

    vid_fill(row, col, dispw, ' ', tui_attr(role));
    if (!(flags & TI_NOECHO)) {
        len = (u16)strlen(buf);
        n = 0;
        if (off < len)
            n = (u16)(len - off);
        if (n > dispw)
            n = dispw;
        put_clean(row, col, buf + off, n,
                  tui_attr((u8)(flags & TI_SELECT ? TA_BOLD : role)));
        vid_cursor(row, (u8)(col + pos - off),
                   (u8)(ins ? VID_CUR_LINE : VID_CUR_BLOCK));
    } else {
        vid_cursor(row, col, (u8)(ins ? VID_CUR_LINE : VID_CUR_BLOCK));
    }
}

/* 1 when key k is a character the field takes under flags: never a
   control byte or DEL, which the server refuses in a value. */
static int input_ok(u16 k, u8 flags)
{
    if (k > 0xFF || k < 0x20 || k == 0x7F)
        return 0;
    if ((flags & TI_PRINT) && (k < 0x21 || k > 0x7E))
        return 0;
    if ((flags & TI_DIGITS) && (k < '0' || k > '9'))
        return 0;
    return 1;
}

u16 tui_input(u8 row, u8 col, u8 dispw, char *buf, u16 max, u8 flags)
{
    u16 len;
    u16 pos;
    u16 at;
    u16 off;
    u16 k;
    int ins;
    int last;

    if (dispw == 0)
        dispw = 1;
    buf[max] = '\0';
    len = (u16)strlen(buf);
    pos = len;
    off = 0;
    ins = 1;
    for (;;) {
        /* At the end of a full field that fits its cells the cursor is
           shown on the last character, at, so the whole text stays in
           view; Del and overwriting then act on that character, and
           Left moves to the one before it, while Backspace and End act
           as at the end. */
        last = pos == max && max > 0 && max <= dispw;
        at = last ? (u16)(max - 1) : pos;
        if (at < off)
            off = at;
        if (at >= off + dispw)
            off = (u16)(at - dispw + 1);
        input_draw(row, col, dispw, buf, off, at, flags, ins, TA_INPUTON);
        k = tui_key();
        if ((flags & TI_SELECT) &&
            (input_ok(k, flags) || k == KEY_BS || k == KEY_DEL)) {
            txt_wipe(buf, len);
            pos = 0;
            len = 0;
        }
        flags &= (u8)~TI_SELECT;
        if (input_ok(k, flags)) {
            if (flags & TI_UPPER)
                k = (u16)TXT_UPPER(k);
            if (last && !ins) {
                buf[at] = (char)(u8)k;
            } else if (ins || pos >= len) {
                if (len < max) {
                    memmove(buf + pos + 1, buf + pos, (size_t)(len - pos + 1));
                    buf[pos++] = (char)(u8)k;
                    len++;
                }
            } else {
                buf[pos++] = (char)(u8)k;
            }
            continue;
        }
        switch (k) {
        case KEY_LEFT:
            if (at > 0)
                pos = (u16)(at - 1);
            break;
        case KEY_RIGHT:
            if (pos < len)
                pos++;
            break;
        case KEY_HOME:
            pos = 0;
            break;
        case KEY_END:
            pos = len;
            break;
        case KEY_BS:
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, (size_t)(len - pos + 1));
                pos--;
                len--;
            }
            break;
        case KEY_DEL:
            if (at < len) {
                memmove(buf + at, buf + at + 1, (size_t)(len - at));
                len--;
                pos = at;
            }
            break;
        case KEY_INS:
            ins = !ins;
            break;
        case KEY_CTRL('Y'):
            txt_wipe(buf, len);
            pos = 0;
            len = 0;
            break;
        default:
            if (k < 0x20 || k >= KEY_EXT) {
                input_draw(row, col, dispw, buf, 0, 0, flags, ins, TA_INPUT);
                vid_cursor(0, 0, VID_CUR_HIDE);
                return k;
            }
            break;
        }
    }
}

/* The cells of a prompt's label lab and of its field, for a value of max
   characters: the box holds both and 7 cells of frame and spacing, the
   field narrowed to TUI_PROMPTMINW first, then lab cut.  Returns the
   label's cells, the field's in *dispw.  Apart from tui_prompt so that
   Watcom C 6.5 can still optimise that in full. */
static u8 prompt_fit(char *lab, u16 max, u8 *dispw)
{
    u16 n;
    u8 d;
    u8 ll;

    d = (u8)(max > TUI_PROMPTW ? TUI_PROMPTW : max);
    n = (u16)strlen(lab);
    ll = (u8)(n > VID_COLS ? VID_COLS : n);
    if (ll + d + 7 > VID_COLS)
        d = (u8)(VID_COLS - 7 - ll < TUI_PROMPTMINW
                 ? (d < TUI_PROMPTMINW ? d : TUI_PROMPTMINW)
                 : VID_COLS - 7 - ll);
    if (ll + d + 7 > VID_COLS) {
        ll = (u8)(VID_COLS - 7 - d);
        lab[ll] = '\0';
    }
    *dispw = d;
    return ll;
}

int tui_prompt(const char *title, const char *label, char *buf, u16 max,
               u8 flags)
{
    char t[MSG_MAXLEN + 1];
    char lab[MSG_MAXLEN + 1];
    tui_pop pop;
    u8 dispw;
    u8 ll;
    u8 w;
    u8 top;
    u8 left;
    u16 n;
    u16 k;

    txt_copy(t, title, (u16)sizeof t);
    txt_copy(lab, label, (u16)sizeof lab);
    ll = prompt_fit(lab, max, &dispw);
    w = (u8)(ll + dispw + 7);
    n = (u16)strlen(t);
    if (n + 6 > w)
        w = (u8)(n + 6 > VID_COLS ? VID_COLS : n + 6);
    top = (u8)(TUI_TOP + (TUI_WORKROWS - TUI_PROMPTH) / 2);
    left = (u8)((VID_COLS - w) / 2);
    if (!tui_pop_open(&pop, top, left, TUI_PROMPTH, w, 0))
        return 0;
    tui_hint_keep(msg(MT_HINTPROMPT));
    tui_box(top, left, TUI_PROMPTH, w, TB_DOUBLE, t);
    tui_text((u8)(top + 2), (u8)(left + 2), lab, TA_BOLD);
    do
        k = tui_input((u8)(top + 2), (u8)(left + 3 + ll), dispw, buf, max,
                      flags);
    while (k != KEY_ENTER && k != KEY_ESC);
    tui_pop_close(&pop);
    return k == KEY_ENTER;
}

/* With hint 0, the pop-up of tui_pick, MT_HINTPICK its hints and Enter
   choosing only an entry; else a box that keeps nothing of what it
   covers and stays drawn. */
u16 tui_pick_keys(const char *title, const char *head, u16 n,
                  tui_label_fn label, void *arg, u16 *sel, u16 hint)
{
    char t[MSG_MAXLEN + 1];
    char hd[TUI_PICKW + 1];
    char buf[TUI_PICKW + 1];
    tui_pop pop;
    u16 rows;
    u16 top;
    u16 cur;
    u16 first;
    u16 i;
    u16 k;
    u16 last;
    u8 h;
    u8 w;
    u8 bt;
    u8 bl;
    u8 hr;
    char c;
    int at;

    if (n > TUI_PICKMAX)
        n = TUI_PICKMAX;
    if (*sel >= n)
        *sel = n > 0 ? (u16)(n - 1) : 0;
    txt_copy(t, title, (u16)sizeof t);
    hr = head != NULL;
    if (hr)
        txt_copy(hd, head, (u16)sizeof hd);
    first = *sel;
    cur = *sel;
    rows = n < TUI_PICKROWS - hr ? n : (u16)(TUI_PICKROWS - hr);
    if (rows == 0)
        rows = 1;
    h = (u8)(rows + 2 + hr);
    w = TUI_PICKW + 4;
    bt = (u8)(TUI_TOP + (TUI_WORKROWS - h) / 2);
    bl = (u8)((VID_COLS - w) / 2);
    if (hint != 0)
        tui_hint_keep(msg(hint));
    else if (!tui_pop_open(&pop, bt, bl, h, w, MT_HINTPICK))
        return KEY_ESC;
    top = 0;
    last = KEY_NONE;
    for (;;) {
        if (cur < top)
            top = cur;
        if (cur >= top + rows)
            top = (u16)(cur - rows + 1);
        tui_box(bt, bl, h, w, TB_SINGLE, t);
        if (hr)
            tui_textn((u8)(bt + 1), (u8)(bl + 2), hd, TUI_PICKW, TA_BOLD);
        for (i = 0; i < rows; i++) {
            buf[0] = '\0';
            if (top + i < n)
                label(arg, (u16)(top + i), buf);
            buf[TUI_PICKW] = '\0';
            tui_textn((u8)(bt + 1 + hr + i), (u8)(bl + 2), buf, TUI_PICKW,
                      (u8)(top + i == cur ? TA_SELECT : TA_NORMAL));
            if (top + i == first && n > 0)
                vid_fill((u8)(bt + 1 + hr + i), (u8)(bl + 1), 1,
                         (u8)TF_MARK, tui_attr(TA_NORMAL));
        }
        k = tui_key();
        /* A click on an entry selects it, and the second press of a
           double click whose first press did so chooses it. */
        if (last != KEY_NONE && k - last == KEY_DCLICK - KEY_CLICK)
            k = KEY_ENTER;
        last = KEY_NONE;
        at = tui_hit(k, (u8)(bt + 1 + hr), (u8)(bl + 1), (u8)rows,
                     (u8)(w - 2));
        if (at >= 0 && top + at / KEY_CROW < n) {
            cur = (u16)(top + at / KEY_CROW);
            last = k;
        } else if (at >= 0) {
            continue;
        }
        if (k == KEY_UP) {
            if (cur > 0)
                cur--;
        } else if (k == KEY_DOWN) {
            if (cur + 1 < n)
                cur++;
        } else if (k == KEY_PGUP) {
            cur = cur > rows ? (u16)(cur - rows) : 0;
        } else if (k == KEY_PGDN) {
            cur = (u16)(cur + rows < n ? cur + rows : n - 1);
        } else if (k == KEY_HOME) {
            cur = 0;
        } else if (k == KEY_END) {
            cur = n > 0 ? (u16)(n - 1) : 0;
        } else if (k > 0x20 && k < 0x100 && n > 0) {
            k = (u16)TXT_UPPER(k);
            for (i = 1; i < n; i++) {
                buf[0] = '\0';
                label(arg, (u16)((cur + i) % n), buf);
                c = buf[0];
                if ((u16)TXT_UPPER(c) == k) {
                    cur = (u16)((cur + i) % n);
                    break;
                }
            }
        } else if ((k == KEY_ENTER && n > 0) || k == KEY_ESC ||
                   (hint != 0 && at < 0)) {
            break;
        }
    }
    *sel = cur;
    if (hint == 0)
        tui_pop_close(&pop);
    return k;
}

int tui_pick(const char *title, const char *head, u16 n, tui_label_fn label,
             void *arg, u16 *sel)
{
    u16 s;

    if (n == 0) {
        tui_msg(msg(MT_NOCHOICE), 1);
        return 0;
    }
    s = *sel;
    if (tui_pick_keys(title, head, n, label, arg, &s, 0) != KEY_ENTER)
        return 0;
    *sel = s;
    return 1;
}

/* ---------------------------------------------------------------------
 * Permissions and errors.
 * --------------------------------------------------------------------- */

int tui_allow(u8 need)
{
    const char *no;

    no = rqe_deny(tui_s.level, need);
    if (no == NULL)
        return 1;
    tui_msg(no, 1);
    return 0;
}

u8 tui_fail(const rq_res *res)
{
    rqe e;

    rqe_class(&e, res, tui_s.level);
    if (res->code == RQ_ENOTFOUND)
        tui_s.gone = 1;
    if (e.act == RQE_LOST)
        tui_fatal(e.text, TUI_EXIT_LOST);
    if (e.show == RQE_BOX)
        tui_msgbox(msg(MT_ERRTITLE), e.text);
    else if (e.show == RQE_LINE)
        tui_msg(e.text, 1);
    if (e.act == RQE_LOGIN)
        return TUI_CMD_LOGIN;
    return e.act == RQE_QUIT ? TUI_CMD_QUIT : TUI_CMD_NONE;
}

int tui_req(const char *words, const char *id, rq_fn fn, void *arg,
            rq_res *res, u8 *cmd)
{
    rq q;
    rq_res own;

    if (res == NULL)
        res = &own;
    *cmd = TUI_CMD_NONE;
    rq_begin(&q, words);
    rq_word(&q, id);
    if (rq_send(tui_s.l, &q, fn, arg, res) == RQ_OK)
        return 1;
    *cmd = tui_fail(res);
    return 0;
}

/* ---------------------------------------------------------------------
 * Menus and the main loop.
 * --------------------------------------------------------------------- */

/* Run menu mi open, with the help topic MENU while it is; the command chosen,
   or TUI_CMD_NONE when closed or when the entry chosen is a verb of the Record
   menu, whose key the next tui_key gives.  A click on a menu's title in the
   bar opens that menu, one on an entry chooses it, and one anywhere else
   closes the menu. */
static u8 menu_run(u8 mi)
{
    const tui_menu *m;
    char t[MSG_MAXLEN + 1];
    tui_pop pop;
    u8 sel;
    u8 i;
    u8 w;
    u8 left;
    u8 role;
    u8 l;
    u8 nm;
    u8 to;
    u8 cmd;
    u16 k;
    char letter;
    int at;
    int chosen;
    const char *oldtopic;

    oldtopic = tui_topic("MENU");
    nm = menu_count();
    sel = 0;
    cmd = TUI_CMD_NONE;
    for (;;) {
        m = menu(mi);
        w = TUI_MENUMINW;
        for (i = 0; i < m->nitems; i++) {
            l = label_len(msg(m->items[i].label), NULL);
            if (l + 4 > w)
                w = (u8)(l + 4 > VID_COLS ? VID_COLS : l + 4);
        }
        left = menu_col(mi);
        if (left > 0)
            left--;
        if (left + w > VID_COLS)
            left = (u8)(VID_COLS - w);
        draw_bar((int)mi);
        if (!tui_pop_open(&pop, TUI_TOP, left, (u8)(m->nitems + 2), w,
                      MT_HINTMENU))
            break;
        if (sel >= m->nitems)
            sel = 0;
        chosen = -1;
        for (;;) {
            tui_box(TUI_TOP, left, (u8)(m->nitems + 2), w, TB_SINGLE, NULL);
            for (i = 0; i < m->nitems; i++) {
                msg_get(m->items[i].label, t, (u16)sizeof t);
                role = (u8)(tui_s.level < m->items[i].level ? TA_MENUOFF
                                                            : TA_MENU);
                if (i == sel)
                    role = TA_SELECT;
                vid_fill((u8)(TUI_TOP + 1 + i), (u8)(left + 1),
                         (u16)(w - 2), ' ', tui_attr(role));
                draw_label((u8)(TUI_TOP + 1 + i), (u8)(left + 2), t,
                           (u8)(w - 4), role,
                           (u8)(role == TA_MENU ? TA_MENUHOT : TUI_NOHOT));
            }
            k = tui_key();
            letter = key_alt_letter(k);
            if (k == KEY_ESC || k == KEY_F10) {
                chosen = -2;
            } else if (k == KEY_LEFT) {
                to = (u8)(mi > 0 ? mi - 1 : nm - 1);
                break;
            } else if (k == KEY_RIGHT) {
                to = (u8)(mi + 1 < nm ? mi + 1 : 0);
                break;
            } else if (letter != '\0') {
                to = menu_by_letter(letter);
                break;
            } else if ((to = bar_click(k)) < TUI_MAXMENU) {
                break;
            } else if ((at = tui_hit(k, TUI_TOP + 1, (u8)(left + 1),
                                     m->nitems, (u8)(w - 2))) >= 0) {
                sel = (u8)(at / KEY_CROW);
                chosen = sel;
            } else if (tui_hit(k, 0, 0, VID_ROWS, VID_COLS) >= 0) {
                chosen = -2;
            } else if (m->nitems == 0) {
                continue;
            } else if (k == KEY_UP) {
                sel = (u8)(sel > 0 ? sel - 1 : m->nitems - 1);
            } else if (k == KEY_DOWN) {
                sel = (u8)(sel + 1 < m->nitems ? sel + 1 : 0);
            } else if (k == KEY_HOME) {
                sel = 0;
            } else if (k == KEY_END) {
                sel = (u8)(m->nitems - 1);
            } else if (k == KEY_ENTER) {
                chosen = sel;
            } else if (k > 0x20 && k < 0x100) {
                i = item_by_letter(m, (char)TXT_UPPER(k));
                if (i < m->nitems) {
                    sel = i;
                    chosen = i;
                }
            }
            if (chosen >= 0 && !tui_allow(m->items[chosen].level))
                chosen = -1;
            if (chosen != -1)
                break;
        }
        tui_pop_close(&pop);
        if (chosen != -1) {
            if (chosen >= 0 && mi >= app_menus())
                tui_pend = verb_key(m->items[chosen].cmd);
            else if (chosen >= 0)
                cmd = m->items[chosen].cmd;
            break;
        }
        if (to < nm) {
            mi = to;
            sel = 0;
        }
    }
    draw_bar(-1);
    tui_topic(oldtopic);
    return cmd;
}

/* menu_run(mi) with the Record menu's entries on this frame. */
static u8 menu_open(u8 mi)
{
    tui_item rec[TUI_RECMAX];
    u8 cmd;

    rec_fill(rec);
    cmd = menu_run(mi);
    tui_recm.items = NULL;
    return cmd;
}

u8 tui_global(u16 key, int *used)
{
    char letter;
    u8 i;
    u8 cmd;

    *used = 0;
    if (tui_app_p == NULL || tui_s.level == 0)
        return TUI_CMD_NONE;
    if (key == KEY_F3) {
        if (tui_confirm(msg(MT_QUIT))) {
            *used = 1;
            return TUI_CMD_QUIT;
        }
        return TUI_CMD_NONE;
    }
    if (key == KEY_CTRL('F')) {
        *used = 1;
        return tui_app_p->search;
    }
    if (menu_count() == 0)
        return TUI_CMD_NONE;
    letter = key_alt_letter(key);
    if (key == KEY_F10)
        i = 0;
    else if (letter != '\0')
        i = menu_by_letter(letter);
    else
        i = bar_click(key);
    if (i >= menu_count())
        return TUI_CMD_NONE;
    cmd = menu_open(i);
    if (cmd == TUI_CMD_QUIT && !tui_confirm(msg(MT_QUIT)))
        cmd = TUI_CMD_NONE;
    *used = cmd != TUI_CMD_NONE;
    return cmd;
}

void tui_verbs(u32 oper, u16 text)
{
    tui_voper = oper;
    tui_vtext = text;
}

u32 tui_record(u32 v)
{
    u32 old;
    u8 n;

    old = tui_recv;
    tui_recv = v;
    n = 0;
    if (tui_app_p != NULL && tui_vtext != 0)
        for (; v != 0 && n < TUI_RECMAX; v &= v - 1)
            n++;
    tui_recm.nitems = n;
    draw_bar(-1);
    return old;
}

int tui_isglobal(u16 key)
{
    return key == KEY_F3 || key == KEY_F10 || key == KEY_CTRL('F') ||
           key_alt_letter(key) != 0 || bar_click(key) < TUI_MAXMENU;
}

/* key_wait's PING, in a frame of its own, so that the hook's frame
   stays small in every key wait.  A PING that finds the session ended is
   reported as any failed request, the hook removed first, and the
   program ends when the report says to quit; any other failure is
   dropped. */
static void ping(void)
{
    rq q;
    rq_res res;

    rq_begin(&q, "PING");
    if (rq_send(tui_s.l, &q, NULL, NULL, &res) == RQ_OK || !res.closed)
        return;
    key_idle = NULL;
    if (tui_fail(&res) == TUI_CMD_QUIT)
        tui_exit(0);
}

/* key_idle, one tick read a call.  When the tick count has gone back
   (midnight passed) the DOS date is read, as DOS moves its date on only
   when it reads its clock and a program left waiting over two midnights
   would otherwise stamp its records days late.  On a remote link, a PING
   (ping) once TUI_PINGT ticks have passed since the last, so that the
   server does not end, for its IDLE=, the session of a user who types or
   reads at length; but none while a request is under way (rq_busy: a
   printer's question inside a report's response), which rq_send would
   refuse, so that the deepest key waits never carry the frames of ping
   and rq_send. */
static void key_wait(void)
{
    u32 now;
    u16 y;
    u8 m;
    u8 d;

    now = tmr_ticks();
    if (now < tui_tick) {
        dos_today(&y, &m, &d);
        tui_waited += (u32)TMR_DAY;
    }
    tui_waited += now - tui_tick;
    tui_tick = now;
    if (tui_s.conn != TUI_CONN_LOCAL && tui_waited >= TUI_PINGT) {
        tui_waited = 0;
        if (!rq_busy)
            ping();
    }
}

void tui_run(const tui_app *app)
{
    u8 cmd;

    tui_app_p = app;
    tui_tick = tmr_ticks();
    tui_waited = 0;
    key_idle = key_wait;
    cmd = TUI_CMD_LOGIN;
    while (cmd != TUI_CMD_QUIT) {
        tui_clear(TUI_TOP, TUI_BOTTOM);
        tui_infobuf[0] = '\0';
        /* A screen that could not open (no memory, a failed request)
           leaves its message up: it stays until the next key. */
        tui_hint_keep("");
        if (cmd == TUI_CMD_LOGIN) {
            tui_s.level = 0;
            tui_s.user[0] = '\0';
            tui_s.name[0] = '\0';
            tui_frame();
            cmd = app->login(TUI_CMD_LOGIN);
            if (cmd != TUI_CMD_NONE && cmd != TUI_CMD_LOGIN)
                cmd = TUI_CMD_QUIT;
            continue;
        }
        tui_frame();
        if (cmd == TUI_CMD_NONE)
            cmd = app->home(TUI_CMD_NONE);
        else
            cmd = app->run(cmd);
    }
    tui_app_p = NULL;
    key_idle = NULL;
}

void tui_set_exit(tui_exit_fn fn)
{
    tui_exitf = fn;
}

void tui_exit(int status)
{
    if (tui_exitf != NULL)
        tui_exitf(status);
    exit(status);
}

void tui_fatal(const char *text, int status)
{
    key_idle = NULL;
    tui_showbox(msg(MT_ERRTITLE), text);
    tui_hint(msg(MT_HINTOK));
    vid_flush();
    key_read();
    tui_exit(status);
}
