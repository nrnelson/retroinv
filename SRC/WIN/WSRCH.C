/*
 * WSRCH.C - the search of INVWIN (WSRCH.H).
 *
 * The search set is ws_q, which the results' requests read (args), as
 * the frame gives a source's arg the scope's key; a dialog reads its
 * values into a query of its own, copied to ws_q only by OK, so that the
 * results shown meanwhile keep theirs; the Find box's text replaces ws_q
 * at once (set).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "TUI.H"
#include "SCR.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "SCRSFORM.H"
#include "WFORM.H"
#include "WSCR.H"
#include "WSRCH.H"

lv_src wsrch_src;

static scr_query ws_q;          /* the search set                       */
static u8 ws_start;             /* the command its dialog started on    */

/* The search dialog's check (wf_ops): a search it can make, else its
   refusal (scr_search_why) on the first field.  The values go into the
   scr_query arg. */
static int ask_check(void *arg, form *f, u16 *mf)
{
    if (scr_search_get((scr_query *)arg, f) != SCR_SQ_NONE)
        return -1;
    *mf = scr_search_why((const scr_query *)arg);
    return 0;
}

static const wf_ops ask_ops = {
    NULL, NULL, 0, ws_form_busy, NULL, NULL, ask_check, NULL, NULL, 0, 0,
    NULL, &scr_search_loc
};

/* The results' args: those of the search set (scr_search_args). */
static void args(void *arg, rq *q)
{
    (void)arg;
    scr_search_args(&ws_q, q);
}

/* Load the values of q into f, a search form. */
static void load(form *f, const scr_query *q)
{
    form_load(f, scr_search_fields[SCR_SF_TEXT].name, q->text);
    form_load(f, scr_search_fields[SCR_SF_SERIAL].name, q->serial);
    form_load(f, scr_search_fields[SCR_SF_CAT].name, q->cat);
    form_load(f, scr_search_fields[SCR_SF_LOC].name, q->loc);
    form_load(f, scr_search_fields[SCR_SF_ALL].name, q->below ? "T" : "");
    form_load(f, scr_search_fields[SCR_SF_STATUS].name, q->status);
    form_load(f, scr_search_fields[SCR_SF_AVAIL].name, q->avail ? "T" : "");
    form_load(f, scr_search_fields[SCR_SF_TAG].name, q->tag);
    form_load(f, scr_search_fields[SCR_SF_WORKS].name, q->works);
}

/* Make q the search set, its dialog started on the command start, and
   the results' source its search's. */
static void set(const scr_query *q, u8 start)
{
    memcpy(&ws_q, q, sizeof ws_q);
    ws_start = start;
    memcpy(&wsrch_src, &itl_src, sizeof wsrch_src);
    wsrch_src.cmd = scr_search_cmds[ws_q.kind];
    wsrch_src.norders = 0;
    wsrch_src.empty = MS_NONE;
    wsrch_src.args = args;
}

int wsrch_ask(HWND owner, link *l, u8 level, u8 cmd)
{
    scr_query q;
    form_def def;
    form *f;
    u8 start;
    int r;

    start = cmd != TUI_CMD_NONE ? cmd : ws_start;
    scr_search_form(&def, start);
    if (wf_open(&f, &def, FORM_EDIT) != 0)
        return WF_EFAIL;
    if (cmd == TUI_CMD_NONE)
        load(f, &ws_q);
    r = wf_run(f, owner, l, level, &ask_ops, &q, NULL, 0);
    form_close(f);
    if (r == WF_OK)
        set(&q, start);
    return r;
}

int wsrch_find(const char *text)
{
    scr_query q;

    scr_search_item(&q, text, SCR_SI_ALL);
    if (q.kind != SCR_SQ_FIND)
        return 0;
    set(&q, SCMD_FIND);
    return 1;
}

const char *wsrch_title(void)
{
    return scr_search_title(&ws_q);
}

void wsrch_preset(form *f)
{
    scr_item_preset(f);
    if (ws_q.kind != SCR_SQ_SERIAL)
        form_load(f, scr_item_fields[SCR_IF_LOC].name, ws_q.loc);
}
