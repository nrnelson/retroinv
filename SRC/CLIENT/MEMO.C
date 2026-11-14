/*
 * MEMO.C - the memo editor of the full-screen client.
 *
 * The memo is a mio of MEMOIO.H, its lines in a far block; the editor
 * works on one line at a time in a near buffer, and judges each key by
 * the lengths and the bytes as stored that the mio keeps, without
 * reading the block.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "MEMOIO.H"

#define MM_HINT         300     /* key hints of the editor              */
#define MM_HINTRO       301     /* key hints when read-only             */
#define MM_FULL         305     /* the memo would pass LINK_MAXMEMO     */
#define MM_LINEFULL     306     /* the line would pass MEMO_COLS        */
#define MM_READONLY     308     /* shown read-only                      */

#define MEMO_BOXH       (MEMO_ROWS + 2)
#define MEMO_BOXW       (MEMO_COLS + 2)
#define MEMO_TOPROW     (TUI_TOP + (TUI_WORKROWS - MEMO_BOXH) / 2)
#define MEMO_LEFT       ((VID_COLS - MEMO_BOXW) / 2)
/* The longest title tui_box shows in a frame MEMO_BOXW wide. */
#define MEMO_TITLEW     (MEMO_COLS - 4)

static u8 memo_lvcmd;           /* tui_fail's command, NONE to stay     */

int memo_load_count(form *f, const char *field, link *l, const char *table,
                    const char *id, u8 *cmd)
{
    char num[TXT_U32LEN + 1];
    rq_res res;
    u16 lines;

    if (mio_count(l, table, id, &lines, &res) != RQ_OK) {
        *cmd = tui_fail(&res);
        if (*cmd != TUI_CMD_NONE)
            return 0;
    }
    txt_u32(num, lines);
    form_load(f, field, num);
    return 1;
}

/* ---------------------------------------------------------------------
 * Editing.
 * --------------------------------------------------------------------- */
typedef struct {
    mio  x;
    u16  r;                     /* cursor line                          */
    u16  c;                     /* cursor column                        */
    u16  top;                   /* first line shown                     */
    u8   ins;
    u8   ro;
    u8   dirty;
    char title[MEMO_TITLEW + 1];
} memo_ed;

static void draw(memo_ed *e)
{
    char lb[MEMO_SLOT];
    u16 i;
    u16 li;

    tui_box(MEMO_TOPROW, MEMO_LEFT, MEMO_BOXH, MEMO_BOXW, TB_DOUBLE,
            e->title);
    for (i = 0; i < MEMO_ROWS; i++) {
        li = (u16)(e->top + i);
        lb[0] = '\0';
        if (li < e->x.n)
            mio_get(&e->x, li, lb);
        tui_textn((u8)(MEMO_TOPROW + 1 + i), MEMO_LEFT + 1, lb, MEMO_COLS,
                  TA_NORMAL);
    }
    if (e->ro)
        vid_cursor(0, 0, VID_CUR_HIDE);
    else
        vid_cursor((u8)(MEMO_TOPROW + 1 + e->r - e->top),
                   (u8)(MEMO_LEFT + 1 + e->c),
                   (u8)(e->ins ? VID_CUR_LINE : VID_CUR_BLOCK));
}

static void refuse(u16 m)
{
    tui_msg(msg(m), 1);
}

static void key_char(memo_ed *e, char ch)
{
    char lb[MEMO_SLOT];
    u16 len;

    len = e->x.len[e->r];
    mio_get(&e->x, e->r, lb);
    if (!e->ins && e->c < len) {
        lb[e->c++] = ch;
    } else {
        if (len >= MEMO_COLS) {
            refuse(MM_LINEFULL);
            return;
        }
        if (e->x.bytes + 1 > LINK_MAXMEMO) {
            refuse(MM_FULL);
            return;
        }
        memmove(lb + e->c + 1, lb + e->c, (size_t)(len - e->c));
        lb[e->c++] = ch;
        len++;
    }
    mio_put(&e->x, e->r, lb, len);
    e->dirty = 1;
}

static void key_enter(memo_ed *e)
{
    char lb[MEMO_SLOT];
    u16 len;

    if (e->x.n >= MEMO_MAXLINES || e->x.bytes + 2 > LINK_MAXMEMO) {
        refuse(MM_FULL);
        return;
    }
    len = e->x.len[e->r];
    mio_get(&e->x, e->r, lb);
    mio_insert(&e->x, (u16)(e->r + 1));
    mio_put(&e->x, (u16)(e->r + 1), lb + e->c, (u16)(len - e->c));
    mio_put(&e->x, e->r, lb, e->c);
    e->r++;
    e->c = 0;
    e->dirty = 1;
}

/* Join line i + 1 onto line i; 0 when it would not fit. */
static int join(memo_ed *e, u16 i)
{
    char lb[MEMO_SLOT];
    char nb[MEMO_SLOT];
    u16 a;
    u16 b;

    a = e->x.len[i];
    b = e->x.len[i + 1];
    if (a + b > MEMO_COLS) {
        refuse(MM_LINEFULL);
        return 0;
    }
    mio_get(&e->x, i, lb);
    mio_get(&e->x, (u16)(i + 1), nb);
    memcpy(lb + a, nb, b);
    mio_put(&e->x, i, lb, (u16)(a + b));
    mio_delete(&e->x, (u16)(i + 1));
    e->dirty = 1;
    return 1;
}

static void key_bs(memo_ed *e)
{
    char lb[MEMO_SLOT];
    u16 len;
    u16 a;

    len = e->x.len[e->r];
    if (e->c > 0) {
        mio_get(&e->x, e->r, lb);
        memmove(lb + e->c - 1, lb + e->c, (size_t)(len - e->c));
        e->c--;
        mio_put(&e->x, e->r, lb, (u16)(len - 1));
        e->dirty = 1;
    } else if (e->r > 0) {
        a = e->x.len[e->r - 1];
        if (join(e, (u16)(e->r - 1))) {
            e->r--;
            e->c = a;
        }
    }
}

static void key_del(memo_ed *e)
{
    char lb[MEMO_SLOT];
    u16 len;

    len = e->x.len[e->r];
    if (e->c < len) {
        mio_get(&e->x, e->r, lb);
        memmove(lb + e->c, lb + e->c + 1, (size_t)(len - e->c - 1));
        mio_put(&e->x, e->r, lb, (u16)(len - 1));
        e->dirty = 1;
    } else if (e->r + 1 < e->x.n) {
        join(e, e->r);
    }
}

static void key_kill(memo_ed *e)
{
    if (e->x.n <= 1) {
        mio_put(&e->x, 0, "", 0);
    } else {
        mio_delete(&e->x, e->r);
        if (e->r >= e->x.n)
            e->r = (u16)(e->x.n - 1);
    }
    e->c = 0;
    e->dirty = 1;
}

/* Cursor and window after a move. */
static void clamp(memo_ed *e)
{
    if (e->r >= e->x.n)
        e->r = e->x.n > 0 ? (u16)(e->x.n - 1) : 0;
    if (e->c > e->x.len[e->r])
        e->c = e->x.len[e->r];
    if (e->r < e->top)
        e->top = e->r;
    if (e->r >= e->top + MEMO_ROWS)
        e->top = (u16)(e->r - MEMO_ROWS + 1);
}

/* A movement key; 1 when it was one. */
static int key_move(memo_ed *e, u16 k)
{
    switch (k) {
    case KEY_UP:
        if (e->r > 0)
            e->r--;
        break;
    case KEY_DOWN:
        e->r++;
        break;
    case KEY_LEFT:
        if (e->c > 0)
            e->c--;
        else if (e->r > 0) {
            e->r--;
            e->c = e->x.len[e->r];
        }
        break;
    case KEY_RIGHT:
        if (e->c < e->x.len[e->r])
            e->c++;
        else if (e->r + 1 < e->x.n) {
            e->r++;
            e->c = 0;
        }
        break;
    case KEY_HOME:
        e->c = 0;
        break;
    case KEY_END:
        e->c = e->x.len[e->r];
        break;
    case KEY_CHOME:
        e->r = 0;
        e->c = 0;
        break;
    case KEY_CEND:
        e->r = (u16)(e->x.n - 1);
        e->c = e->x.len[e->r];
        break;
    case KEY_PGUP:
        e->r = e->r > MEMO_ROWS ? (u16)(e->r - MEMO_ROWS) : 0;
        e->top = e->top > MEMO_ROWS ? (u16)(e->top - MEMO_ROWS) : 0;
        break;
    case KEY_PGDN:
        e->r = (u16)(e->r + MEMO_ROWS);
        if (e->top + 2 * MEMO_ROWS <= e->x.n)
            e->top = (u16)(e->top + MEMO_ROWS);
        else if (e->x.n > MEMO_ROWS)
            e->top = (u16)(e->x.n - MEMO_ROWS);
        break;
    default:
        return 0;
    }
    clamp(e);
    return 1;
}

/* Report a save that failed with rc after done of its total lines: the
   message of mio_failmsg, then the failure, with tui_fail's command
   kept for memo_leave; 1 when the session wants the editor left. */
static int save_failed(int rc, u16 done, u16 total, const rq_res *res)
{
    char a[TXT_U32LEN + 1];
    char b[TXT_U32LEN + 1];
    u16 m;

    m = mio_failmsg(rc, done, total, res);
    if (m != 0) {
        txt_u32(a, done);
        txt_u32(b, total);
        tui_msgbox(msg(MT_ERRTITLE), msg_sub(m, a, b));
    }
    memo_lvcmd = m == MM_OLDSRV ? TUI_CMD_NONE : tui_fail(res);
    return memo_lvcmd != TUI_CMD_NONE;
}

u8 memo_leave(void)
{
    return memo_lvcmd;
}

/* Whether the loaded memo opens read-only (e->ro), and the message it
   opens with: its number, or 0 for none, and in *err the err argument
   of tui_msg.  A memo the editor could not save whole (mio_check)
   opens read-only whatever readonly says. */
static u16 open_mode(memo_ed *e, int readonly, int *err)
{
    u16 m;

    m = mio_check(&e->x);
    *err = m != 0 && m != MM_CHANGED;
    e->ro = 1;
    if (*err)
        return m;
    if (readonly)
        return MM_READONLY;
    e->ro = 0;
    return m;
}

int memo_edit(link *l, const char *table, const char *id, int readonly,
              const char *title, u16 *lines)
{
    memo_ed e;
    rq_res res;
    cmem_blk *save_scr;
    const char *oldtopic;
    char num[TXT_U32LEN + 1];
    u16 done;
    u16 total;
    u16 m;
    u16 k;
    int rc;
    int err;

    memo_lvcmd = TUI_CMD_NONE;
    memset(&e, 0, sizeof e);
    txt_copy(e.title, title, (u16)sizeof e.title);
    *lines = 0;
    rc = mio_open(&e.x);
    if (rc != RQ_OK) {
        tui_msg(msg(MT_NOMEM), 1);
        return rc;
    }
    rc = mio_load(&e.x, l, table, id, &res);
    if (rc != RQ_OK) {
        mio_close(&e.x);
        memo_lvcmd = tui_fail(&res);
        return rc;
    }
    *lines = e.x.n;
    save_scr = vid_save(MEMO_TOPROW, MEMO_LEFT, MEMO_BOXH, MEMO_BOXW);
    if (save_scr == NULL) {
        mio_close(&e.x);
        tui_msg(msg(MT_NOMEM), 1);
        return RQ_EINTERNAL;
    }
    oldtopic = tui_topic("MEMO");
    m = open_mode(&e, readonly, &err);
    tui_hint(msg(e.ro ? MM_HINTRO : MM_HINT));
    if (m == MM_TOOMANY) {
        txt_u32(num, MEMO_MAXLINES);
        tui_msg(msg_sub(m, num, NULL), err);
    } else if (m != 0) {
        tui_msg(msg(m), err);
    }
    if (e.x.n == 0)
        mio_insert(&e.x, 0);
    e.ins = 1;
    rc = RQ_OK;
    for (;;) {
        draw(&e);
        k = tui_key();
        if (key_move(&e, k))
            continue;
        if (k == KEY_ESC || k == KEY_F3) {
            if (e.ro || !e.dirty || tui_confirm(msg(MT_DISCARD)))
                break;
            continue;
        }
        if (e.ro)
            continue;
        if (k == KEY_CTRL('S')) {
            rc = mio_save(&e.x, l, table, id, &done, &total, &res);
            if (rc == RQ_OK) {
                *lines = total;
                break;
            }
            if (save_failed(rc, done, total, &res))
                break;
            continue;
        }
        if (k == KEY_ENTER)
            key_enter(&e);
        else if (k == KEY_BS)
            key_bs(&e);
        else if (k == KEY_DEL)
            key_del(&e);
        else if (k == KEY_INS)
            e.ins = (u8)!e.ins;
        else if (k == KEY_CTRL('Y'))
            key_kill(&e);
        else if (k >= 0x20 && k <= 0xFF && k != 0x7F)
            key_char(&e, (char)(u8)k);
        clamp(&e);
    }
    vid_cursor(0, 0, VID_CUR_HIDE);
    vid_restore(save_scr);
    mio_close(&e.x);
    tui_topic(oldtopic);
    return rc;
}
