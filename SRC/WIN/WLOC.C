/*
 * WLOC.C - the location tree of INVWIN (WLOC.H).
 *
 * The names of the location types are a ctab (CODETAB.H) that the tree's
 * format function reads; the location whose contents show is wloc_id,
 * with its name and path, which the contents' requests, title and count
 * read, with wloc_top and wloc_all, as the search's results read the
 * search set (WSRCH.C).
 */
#define RQERR_CLASS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "MEMOIO.H"
#include "TUI.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "CODETAB.H"
#include "SCRKFORM.H"
#include "SCRLFORM.H"
#include "WFORM.H"
#include "WPICK.H"
#include "WCOLS.H"
#include "WROWS.H"
#include "WSCR.H"
#include "WLOC.H"
#include "WPANE.H"
#include "INVWIN.H"

/* The type of a new top-level location. */
#define WL_ROOM         "ROOM"

/* The field of a new location that takes the selected one's key. */
static const char k_parent[] = "PARENT_ID";

static ctab wl_types;           /* the location types, b NULL until
                                   wloc_types opens it                  */
static char wl_name[LINK_LOCNAMELEN + 1];       /* wloc_id's name       */
static char wl_path[LINK_LOCPATHLEN + 1];       /* and its path         */

u8 wloc_top;
u8 wloc_all;
char wloc_id[LINK_IDLEN + 1];

/* ---------------------------------------------------------------------
 * The tree.
 * --------------------------------------------------------------------- */

static void loc_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_tree(r, &wl_types, text, key);
}

int wloc_types(HWND w, link *l, u8 *act)
{
    rq_res res;
    int rc;

    *act = RQE_STAY;
    if (wl_types.b == NULL && !ct_open(&wl_types)) {
        ws_say_oem(w, MB_ICONSTOP, msg(MT_NOMEM));
        return 0;
    }
    ws_form_busy(NULL, w, 1);
    rc = ct_load(&wl_types, l, wp_tables[SCR_CK_LTYPE], &res);
    ws_form_busy(NULL, w, 0);
    if (rc == RQ_OK)
        return 1;
    *act = ws_fail(w, &res);
    return 0;
}

void wloc_free(void)
{
    ct_close(&wl_types);
}

/* The field naming a location (wf_ops' names), alone. */
static const char *const loc_names[] = { "LOC_NAME", NULL };

/* The location form, its type's name beside the type (WFORM.H's name
   field) from the names the tree holds. */
static const wf_ops loc_ops = {
    "LOC", NULL, MO_FTITLE, ws_form_busy, mio_load, mio_save, NULL, NULL,
    NULL, 0, WF_O_NOKEY | WF_O_SELF, loc_names, &scr_loc_inside, &wl_types
};

/* The type of the first location inside location in into t
   (LINK_LTYPELEN + 1 bytes), from w held busy, NULL for the frame
   (scr_loc_first): "" when it holds none or the request fails. */
static void first_type(HWND w, const char *in, char *t)
{
    rq_res res;

    /* A failure is not shown: the new location starts with no type. */
    ws_form_busy(NULL, w, 1);
    scr_loc_first(ws_link(), in, t, &res);
    ws_form_busy(NULL, w, 0);
}

/* A new location's first values, a screen's preset, inside the one in
   PARENT_ID, which the frame has loaded: the type of the first location
   inside that one (first_type), or WL_ROOM at the top level. */
static void loc_preset(form *f)
{
    char t[LINK_LTYPELEN + 1];
    const char *in;

    in = form_get(f, k_parent);
    if (in[0] != '\0')
        first_type(NULL, in, t);
    else
        strcpy(t, WL_ROOM);
    form_load(f, scr_loc_fields[SCR_LF_TYPE].name, t);
}

int wloc_get(HWND w, const char *id, scr_locinfo *x, u8 *act)
{
    rq q;
    rq_res res;

    *act = RQE_STAY;
    memset(x, 0, sizeof *x);
    rq_begin(&q, wp_w_locget);
    rq_word(&q, id);
    if (ws_send(w, &q, scr_loc_info, x, &res) == RQ_OK)
        return 1;
    *act = ws_fail(w, &res);
    return 0;
}

int wloc_room(HWND w, const char *id, u8 *act)
{
    scr_locinfo x;

    if (!wloc_get(w, id, &x, act))
        return 0;
    if (x.depth < LINK_MAXLOCDEPTH)
        return 1;
    ws_say_oem(w, MB_ICONEXCLAMATION, msg_sub(MO_TOODEEP, x.name, NULL));
    return 0;
}

/* Move (ws_scr's move): location key moved into the one chosen from the
   pick list over w, captioned IW_S_MOVETO with its name, which opens on
   its container (LOC GET), or to the top level; its own container chosen
   again sends nothing, and one at the deepest level is refused before
   LOC SET (wloc_room). */
static int loc_move(HWND w, link *l, u8 level, const char *key, u8 *act)
{
    char fmt[MSG_MAXLEN + 1];
    char title[MSG_MAXLEN + 1];
    char to[WP_CODESZ];
    scr_locinfo x;
    rq q;
    rq_res res;
    int r;

    if (!wloc_get(w, key, &x, act))
        return 0;
    LoadString((HINSTANCE)GetWindowWord(w, GWW_HINSTANCE), IW_S_MOVETO, fmt,
               sizeof fmt);
    AnsiToOem(fmt, fmt);
    wsprintf(title, fmt, (LPCSTR)x.name);
    r = wp_pick(w, l, level, WP_LOCS, title, MO_TOP, x.parent, key,
                ws_form_busy, NULL, to, &res, act);
    if (r == WP_ENOMEM)
        ws_say_oem(w, MB_ICONSTOP, msg(MT_NOMEM));
    if (r == WP_FAIL)
        *act = ws_fail(w, &res);
    if (r != WP_OK || strcmp(to, x.parent) == 0 ||
        (strcmp(to, "0") != 0 && !wloc_room(w, to, act)))
        return 0;
    rq_begin(&q, "LOC SET");
    rq_word(&q, key);
    rq_pair(&q, k_parent, to);
    if (ws_send(w, &q, NULL, NULL, &res) == RQ_OK)
        return 1;
    *act = ws_fail(w, &res);
    return 0;
}

static const u16 loc_notes[] = { MO_ADDED, MO_SAVED, MO_DELETED, MO_MOVED };

const ws_scr wloc_scr = {
    "LOCS", &scr_loc_src, loc_fmt, &wrow_tree_cols, MO_TITLE, &scr_loc_def,
    &loc_ops, wp_w_locget, "LOC DEL", MO_DELQ, loc_preset, NULL,
    WS_HOLDS | WS_TREE, k_parent, NULL, NULL, NULL, NULL, loc_notes, NULL,
    loc_move, &wpane_loc
};

/* ---------------------------------------------------------------------
 * The contents.
 * --------------------------------------------------------------------- */

int wloc_enter(HWND w, const char *id, u8 *act)
{
    char t[LINK_LTYPELEN + 1];
    scr_locinfo x;

    if (!wloc_get(w, id, &x, act))
        return 0;
    first_type(w, id, t);
    txt_copy(wloc_id, id, (u16)sizeof wloc_id);
    strcpy(wl_name, x.name);
    strcpy(wl_path, x.path);
    wloc_top = 0;
    wloc_all = (u8)(t[0] != '\0');
    return 1;
}

int wloc_refresh(HWND w, u8 *act)
{
    scr_locinfo x;

    if (!wloc_get(w, wloc_id, &x, act))
        return 0;
    strcpy(wl_name, x.name);
    strcpy(wl_path, x.path);
    return 1;
}

/* The contents' args: the location set, LOC CONTENTS's word, TOP=1 for
   its items not installed in another only (wloc_top) and ALL=1 for
   those of every location below it too (wloc_all). */
static void contents_args(void *arg, rq *q)
{
    (void)arg;
    rq_word(q, wloc_id);
    if (wloc_top)
        rq_pair(q, "TOP", "1");
    if (wloc_all)
        rq_pair(q, "ALL", "1");
}

const lv_src wloc_csrc = {
    "LOC CONTENTS", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MI_HDR, MI_HDR, MI_HDR, MI_HDR },
    MO_NOITEMS, itl_k_itemid, contents_args, NULL, NULL, NULL
};

const char *wloc_ctitle(void)
{
    return wl_path;
}

const char *wloc_cname(void)
{
    return wl_name;
}

void wloc_saved(const char *id, form *f)
{
    if (strcmp(id, wloc_id) == 0)
        txt_copy(wl_name, form_get(f, loc_names[0]), (u16)sizeof wl_name);
}

void wloc_cpreset(form *f)
{
    scr_item_preset(f);
    form_load(f, scr_item_fields[SCR_IF_LOC].name, wloc_id);
}
