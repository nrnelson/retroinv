/*
 * LISTV.C - the paged list view of the full-screen client.
 *
 * The view draws its pager's rows and runs the keys; the pager calls
 * back into progress() between requests and report() on a failed one,
 * and every move ends in end_move(), which puts the status info back,
 * says that a stopped move stopped and repaints.
 */
#define PAGER_CALLS
#define TUI_CLICKS
#include <stddef.h>
#include <stdlib.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "LISTV.H"

/* A note's first argument is an ID (item, loan or service record), its
   second an ID or a location's name, so the second's buffer, sized for
   a name, must hold an ID too. */
extern char lv_note_holds_id[1];
extern char lv_note_holds_id[(LINK_IDLEN <= LINK_LOCNAMELEN) ? 1 : 2];

#define ML_WALK         200     /* progress of the End walk             */
#define ML_STOPPED      201     /* a fill stopped by Esc                */

struct lv {
    pg   p;
    u8   top;
    u8   left;
    u8   w;
    u8   shown;                 /* progress replaced the status info    */
    u8   quiet;                 /* lv_run is not reading the keys       */
    u8   leave;                 /* tui_fail's command, NONE to stay     */
    char info[TUI_ST_INFOW + 1]; /* the status info under the progress  */
};

/* The pending note: message number (0: none) and its arguments. */
static u16 lv_notem;
static char lv_notea[LINK_IDLEN + 1];
static char lv_noteb[LINK_LOCNAMELEN + 1];

void lv_note(u16 m, const char *a, const char *b)
{
    lv_notem = m;
    txt_copy(lv_notea, a, (u16)sizeof lv_notea);
    txt_copy(lv_noteb, b, (u16)sizeof lv_noteb);
}

void lv_note_show(void)
{
    if (lv_notem != 0)
        tui_msg(msg_sub(lv_notem, lv_notea, lv_noteb), 0);
    lv_notem = 0;
}

u8 lv_note_end(u8 cmd)
{
    if (cmd == TUI_CMD_NONE)
        lv_note_show();
    lv_notem = 0;
    return cmd;
}

/* ---------------------------------------------------------------------
 * The pager's functions and the end of a move.
 * --------------------------------------------------------------------- */
/* Put back the status info the progress replaced. */
static void info_back(lv *v)
{
    if (v->shown) {
        tui_info(v->info);
        v->shown = 0;
    }
}

/* pg_wait_fn: the progress in the status info, keeping the info it
   replaces the first time, a flush, then 1 when Esc was typed. */
static int progress(void *arg, int walk, u32 n)
{
    lv *v;
    char num[TXT_U32LEN + 1];

    v = (lv *)arg;
    if (!v->shown) {
        txt_copy(v->info, tui_info_text(), (u16)sizeof v->info);
        v->shown = 1;
    }
    txt_u32(num, n);
    tui_info(msg_sub(walk ? ML_WALK : MT_WORKING, num, NULL));
    vid_flush();
    return tui_esc_waiting();
}

/* pg_fail_fn: the status info back, then tui_fail, whose command is
   kept for lv_leave; 1 when it is TUI_CMD_NONE.  Inside lv_run the
   view, painted with the bar, is painted quiet first, so a box tui_fail
   opens leaves no ends of the bar beside it. */
static int report(void *arg, const rq_res *res)
{
    lv *v;
    u8 quiet;

    v = (lv *)arg;
    info_back(v);
    quiet = v->quiet;
    if (!quiet) {
        v->quiet = 1;
        lv_paint(v);
    }
    v->leave = tui_fail(res);
    v->quiet = quiet;
    return v->leave == TUI_CMD_NONE;
}

/* End a move of the pager that returned rc: after a success the screen
   stays (a failure has set v->leave through report), the status info
   comes back, a stop is said and the view is repainted.  Returns rc. */
static int end_move(lv *v, int rc)
{
    if (rc == RQ_OK)
        v->leave = TUI_CMD_NONE;
    info_back(v);
    if (v->p.stopped)
        tui_msg(msg(ML_STOPPED), 0);
    lv_paint(v);
    return rc;
}

/* Make move m of the pager and end it; the move's code. */
static int move(lv *v, int m)
{
    return end_move(v, pg_move(&v->p, m));
}

int lv_home(lv *v)
{
    return move(v, PG_HOME);
}

int lv_end(lv *v)
{
    return move(v, PG_END);
}

int lv_up(lv *v)
{
    return move(v, PG_UP);
}

int lv_down(lv *v)
{
    return move(v, PG_DOWN);
}

int lv_pgup(lv *v)
{
    return move(v, PG_PGUP);
}

int lv_pgdn(lv *v)
{
    return move(v, PG_PGDN);
}

int lv_seek(lv *v, lv_match_fn match, void *arg, u16 max)
{
    return end_move(v, pg_seek(&v->p, match, arg, max));
}

int lv_reload(lv *v)
{
    int rc;

    rc = move(v, PG_RELOAD);
    if (rc == RQ_OK)
        lv_note_show();
    else
        lv_notem = 0;
    return rc;
}

u8 lv_order_index(lv *v)
{
    return v->p.order;
}

void lv_lead(lv *v, const char *key, const char *text)
{
    pg_lead(&v->p, key, text);
}

const char *lv_order_name(lv *v)
{
    return pg_order_name(&v->p);
}

int lv_set_order(lv *v, const char *name)
{
    return pg_set_order(&v->p, name);
}

lv *lv_open(link *l, const lv_src *src, u8 top, u8 left, u8 h, u8 w)
{
    lv *v;

    if (w > LV_TEXTLEN)
        return NULL;
    v = (lv *)malloc(sizeof *v);
    if (v == NULL)
        return NULL;
    v->top = top;
    v->left = left;
    v->w = w;
    v->shown = 0;
    v->quiet = 1;
    v->leave = TUI_CMD_NONE;
    if (pg_open(&v->p, l, src, (u8)(h - 1), progress, report, v) != PG_OK) {
        free(v);
        return NULL;
    }
    return v;
}

void lv_close(lv *v)
{
    if (v == NULL)
        return;
    pg_close(&v->p);
    free(v);
}

void lv_paint(lv *v)
{
    char text[LV_ROWTEXT + 1];
    u16 t;
    u16 first;
    u16 sel;
    u16 i;
    u16 li;
    u8 row;
    u8 role;

    t = pg_rows(&v->p);
    first = v->p.first;
    sel = v->p.sel;
    tui_textn(v->top, v->left, msg(v->p.src->header[v->p.order]), v->w,
              TA_TITLE);
    for (i = 0; i < v->p.ps; i++) {
        row = (u8)(v->top + 1 + i);
        li = (u16)(first + i);
        if (li < t) {
            role = (u8)(pg_text(&v->p, li, text) ? TA_ALERT : TA_NORMAL);
            if (li == sel)
                role = (u8)(v->quiet ? TA_BOLD : TA_SELECT);
            tui_textn(row, v->left, text, v->w, role);
        } else {
            tui_textn(row, v->left, "", v->w, TA_NORMAL);
        }
    }
    if (t == 0 && pg_at_end(&v->p))
        tui_textn((u8)(v->top + 1), v->left, msg(v->p.src->empty), v->w,
                  TA_NORMAL);
}

/* 1 when key k is a click on a row held in the window, which is then
   selected row by row as Up and Down do, without repainting between
   them: the row needs no fetch, but a Down that takes the window into B
   fetches what follows, as it always does; else 0. */
static int click_row(lv *v, u16 k)
{
    u16 i;
    int at;
    int rc;

    at = tui_hit(k, (u8)(v->top + 1), v->left, v->p.ps, v->w);
    if (at < 0)
        return 0;
    i = (u16)(v->p.first + at / KEY_CROW);
    if (i >= pg_rows(&v->p))
        return 0;
    rc = RQ_OK;
    while (rc == RQ_OK && v->p.sel != i)
        rc = pg_move(&v->p, v->p.sel < i ? PG_DOWN : PG_UP);
    end_move(v, rc);
    return 1;
}

u16 lv_run(lv *v)
{
    u16 k;
    u16 last;

    /* Every move paints the view itself and goes on to the next key
       unless it made the screen leave.  A click on
       a row selects it, and a double click's second press there gives
       Enter after the first press selected it. */
    v->quiet = 0;
    lv_paint(v);
    last = KEY_NONE;
    do {
        k = tui_key();
        if (last != KEY_NONE && k - last == KEY_DCLICK - KEY_CLICK) {
            k = KEY_ENTER;
            break;
        }
        last = KEY_NONE;
        if (click_row(v, k)) {
            last = k;
            continue;
        }
        switch (k) {
        case KEY_UP:
            lv_up(v);
            continue;
        case KEY_DOWN:
            lv_down(v);
            continue;
        case KEY_PGUP:
            lv_pgup(v);
            continue;
        case KEY_PGDN:
            lv_pgdn(v);
            continue;
        case KEY_HOME:
            lv_home(v);
            continue;
        case KEY_END:
            lv_end(v);
            continue;
        case KEY_F5:
            lv_reload(v);
            continue;
        default:
            break;
        }
        break;
    } while (v->leave == TUI_CMD_NONE);
    v->quiet = 1;
    lv_paint(v);
    return v->leave != TUI_CMD_NONE ? KEY_ESC : k;
}

u8 lv_leave(lv *v)
{
    return v->leave;
}

int lv_selected(lv *v, char *key)
{
    return pg_selected(&v->p, key);
}

u16 lv_rows(lv *v)
{
    return pg_rows(&v->p);
}

void lv_seltext(lv *v, char *text)
{
    pg_text(&v->p, v->p.sel, text);
}
