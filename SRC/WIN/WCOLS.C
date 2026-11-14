/*
 * WCOLS.C - the columns of a list's rows in the Windows programs: their
 * pixel layout in a font and their drawing (WCOLS.H).
 *
 * A drawing converts the text's cells that columns hold to the Windows
 * set once, then draws each piece from that copy: OemToAnsi maps a
 * character alone, so a piece converted on its own would read the same.
 * Only a piece whose place needs it, right-aligned or indented, is
 * measured, but every piece while watched, whose width the watch keeps.
 * Headings are drawn as rows are, then their edges, arrow and dividers
 * over them, each a rectangle filled with FillRect and held to the box.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "PAGER.H"
#include "WCOLS.H"

/* The NULL table's one column: a text drawn whole, as INV shows it. */
static const wcol whole_col = { LV_ROWTEXT, WCOL_INDENT };
static const wcol_tab whole = { 1, 0, &whole_col };

static wcol_seen *wcol_watched; /* wcol_watch's record, or NULL         */
static cmem_blk *wcol_store;    /* wcol_keep's widths, or NULL          */

/* Pixels of n cells of an average character width of avew: at most
   (LV_ROWTEXT + 1) * avew * WCOL_NUM, an int for every font a list
   takes. */
static int px(int n, int avew)
{
    return n * avew * WCOL_NUM / WCOL_DEN;
}

void wcol_keep(cmem_blk *store)
{
    wcol_store = store;
}

void wcol_widths(u8 keep, u8 *w)
{
    if (wcol_store != NULL && keep != 0)
        cmem_get(wcol_store, (u16)((keep - 1) * WCOL_MAX), w, WCOL_MAX);
    else
        memset(w, 0, WCOL_MAX);
}

void wcol_set_width(u8 keep, int k, u8 n)
{
    if (wcol_store != NULL && keep != 0)
        cmem_put(wcol_store, (u16)((keep - 1) * WCOL_MAX + k), &n, 1);
}

int wcol_kept(void)
{
    return wcol_store != NULL;
}

void wcol_layout(wcol_lay *lay, const wcol_tab *t, const TEXTMETRIC *tm)
{
    u8 kept[WCOL_MAX];
    int base;
    int x;
    int s;
    int w;
    int k;

    if (t == NULL)
        t = &whole;
    wcol_widths(t->keep, kept);
    base = 0;
    s = 0;
    x = 0;
    for (k = 0; k < t->n; k++) {
        lay->x[k] = x;
        w = kept[k] == 0 ? t->col[k].width :
            kept[k] < WCOL_MINW ? WCOL_MINW : kept[k];
        if (w != 0) {
            base = x + w * tm->tmAveCharWidth;
            s = 0;
            x = base;
        } else {
            s += t->col[k].cells + 1;
            x = base + px(s, tm->tmAveCharWidth);
        }
    }
    lay->x[k] = x;
    lay->cell = px(1, tm->tmAveCharWidth);
    lay->lineh = tm->tmHeight;
}

void wcol_head(const wcol_tab *t, const char *words, char *text)
{
    const char *e;
    int s;
    int n;
    int w;
    int k;

    s = 0;
    for (k = 0; k < t->n && *words != '\0'; k++) {
        for (e = words; *e != '\0' && *e != WCOL_SEP; e++)
            ;
        w = t->col[k].cells;
        n = (int)(e - words) < w ? (int)(e - words) : w;
        memset(text + s, ' ', (size_t)w + 1);
        memcpy(text + s + (t->col[k].flags & WCOL_RIGHT ? w - n : 0), words,
               (size_t)n);
        s += w + 1;
        words = *e != '\0' ? e + 1 : e;
    }
    while (s > 0 && text[s - 1] == ' ')
        s--;
    text[s] = '\0';
}

int wcol_piece(const wcol_tab *t, int k, const char *text, int len,
               int *at, int *ind)
{
    int s;
    int a;
    int b;
    int i;

    if (t == NULL)
        t = &whole;
    s = 0;
    for (i = 0; i < k; i++)
        s += t->col[i].cells + 1;
    a = s < len ? s : len;
    b = s + t->col[k].cells < len ? s + t->col[k].cells : len;
    while (b > a && text[b - 1] == ' ')
        b--;
    while (a < b && text[a] == ' ')
        a++;
    *at = a;
    *ind = t->col[k].flags & WCOL_INDENT ? a - s : 0;
    return b - a;
}

int wcol_hit(const wcol_tab *t, const wcol_lay *lay, int x, int *div)
{
    int k;

    if (div != NULL)
        *div = 0;
    if (t == NULL)
        t = &whole;
    else if (div != NULL)
        for (k = t->n - 1; k >= 0; k--)
            if (x >= lay->x[k + 1] - WCOL_GRIP &&
                x <= lay->x[k + 1] + WCOL_GRIP) {
                *div = 1;
                return k;
            }
    for (k = 0; k < t->n; k++)
        if (x >= lay->x[k] && x < lay->x[k + 1])
            return k;
    return -1;
}

int wcol_x(const wcol_tab *t, const wcol_lay *lay, int k, int ind, int w)
{
    int room;

    if (t == NULL)
        t = &whole;
    room = lay->x[k + 1] - lay->cell - lay->x[k];
    if (w > room)
        return lay->x[k];
    return lay->x[k] + (t->col[k].flags & WCOL_RIGHT ? room - w :
                        ind * lay->cell);
}

/* ExtTextOut(dc, x, y, opt, clip, str, n) for column k of a drawing
   whose rectangle starts at left, or, while watched, its record in its
   place: the piece's left edge less left, its width, and CUT when it
   reaches past clip's right edge or is not clipped. */
static void text_out(HDC dc, int x, int y, UINT opt, const RECT FAR *clip,
                     const char *str, int n, int k, int left)
{
    wcol_seen *s;
    int w;

    s = wcol_watched;
    if (s == NULL) {
        ExtTextOut(dc, x, y, opt, clip, str, n, NULL);
        return;
    }
    w = n > 0 ? (int)LOWORD(GetTextExtent(dc, str, n)) : 0;
    s->pc[k].x = x - left;
    s->pc[k].w = w;
    if (n > 0 && (!(opt & ETO_CLIPPED) || x + w > clip->right))
        s->cut = 1;
}

/* FillRect of brush b over l..r, t..bo of dc, held to box's width. */
static void fill(HDC dc, const RECT FAR *box, HBRUSH b, int l, int t, int r,
                 int bo)
{
    RECT q;

    q.left = max(l, box->left);
    q.right = min(r, box->right);
    q.top = t;
    q.bottom = bo;
    if (q.left < q.right)
        FillRect(dc, &q, b);
}

/* The arrow's left edge in column k of the rectangle starting at left:
   at its room's left for WCOL_RIGHT, else against its room's right. */
static int arrow_x(const wcol_tab *t, const wcol_lay *lay, int k, int left)
{
    if (t->col[k].flags & WCOL_RIGHT)
        return left + lay->x[k] + WCOL_ARROWGAP;
    return left + lay->x[k + 1] - lay->cell - WCOL_ARROWW;
}

/* Draw the edges, dividers and arrow of headings of states st over r,
   held to box, the arrow in column arrow (-1 for none): a button's white
   top and left edge, a pressed one's in the shadow, each column's
   divider and the arrow, a pixel right and down when pressed. */
static void edges(HDC dc, const RECT FAR *box, const RECT *r,
                  const wcol_tab *t, const wcol_lay *lay, const u8 *st,
                  int arrow)
{
    HBRUSH dark;
    HBRUSH ink;
    HBRUSH lit;
    int h;
    int i;
    int k;
    int l;
    int x;
    int y;

    dark = CreateSolidBrush(wcol_unlike(dc, COLOR_BTNSHADOW));
    ink = CreateSolidBrush(GetSysColor(COLOR_BTNTEXT));
    for (k = 0; k < t->n; k++) {
        l = r->left + lay->x[k];
        x = r->left + lay->x[k + 1];
        if (st[k] != WCOL_H_FLAT) {
            lit = st[k] == WCOL_H_PRESS ? dark :
                  (HBRUSH)GetStockObject(WHITE_BRUSH);
            fill(dc, box, lit, l, r->top, x - 1, r->top + 1);
            fill(dc, box, lit, l, r->top, l + 1, r->bottom);
        }
        fill(dc, box, dark, x - 1, r->top, x, r->bottom);
    }
    if (arrow >= 0) {
        k = st[arrow] == WCOL_H_PRESS;
        h = (WCOL_ARROWW + 1) / 2;
        x = arrow_x(t, lay, arrow, r->left) + k + WCOL_ARROWW / 2;
        y = r->top + (r->bottom - r->top - h) / 2 + k;
        for (i = 0; i < h; i++)
            fill(dc, box, ink, x - i, y + i, x + i + 1, y + i + 1);
    }
    DeleteObject(ink);
    DeleteObject(dark);
}

/* wcol_draw, or with st the headings of states st, one a column
   (WCOL_H_), the arrow in column arrow (-1 for none): a pressed piece a
   pixel right and down, the arrow's column's clipped short of the arrow
   and the rest of its slot filled with the background, then the edges,
   dividers and arrow drawn over them; while watched, each column's slot
   and state recorded too. */
static void draw(HDC dc, const RECT FAR *box, const char *text,
                 const wcol_tab *t, const wcol_lay *lay, int hx,
                 const u8 *st, int arrow)
{
    char ansi[LV_ROWTEXT];
    wcol_seen *s;
    HBRUSH face;
    RECT slot;
    RECT clip;
    RECT rr;
    RECT *r;
    int len;
    int at;
    int ind;
    int ax;
    int sh;
    int n;
    int k;
    int w;

    rr = *box;
    rr.left -= hx;
    if (t != NULL && rr.right < rr.left + lay->x[t->n])
        rr.right = rr.left + lay->x[t->n];
    r = &rr;
    if (t == NULL)
        t = &whole;
    s = wcol_watched;
    len = (int)strlen(text);
    if (len > LV_ROWTEXT)
        len = LV_ROWTEXT;
    OemToAnsiBuff(text, ansi, len);
    if (s != NULL) {
        s->n = t->n;
        memcpy(s->text, ansi, (size_t)len);
        s->fg = GetTextColor(dc);
        s->bg = GetBkColor(dc);
        s->cut = 0;
        s->heads = (u8)(st != NULL);
    }
    slot.left = r->left;
    slot.top = r->top;
    slot.bottom = r->bottom;
    for (k = 0; k < t->n; k++) {
        n = wcol_piece(t, k, ansi, len, &at, &ind);
        w = n > 0 && (t->col[k].flags & WCOL_RIGHT || ind > 0) ?
            (int)LOWORD(GetTextExtent(dc, ansi + at, n)) : 0;
        slot.right = r->right;
        if (k + 1 < t->n && r->left + lay->x[k + 1] - lay->cell < r->right)
            slot.right = r->left + lay->x[k + 1] - lay->cell;
        clip = slot;
        sh = st != NULL && st[k] == WCOL_H_PRESS;
        if (s != NULL) {
            s->pc[k].at = (u8)at;
            s->pc[k].len = (u8)n;
            s->pc[k].flags = t->col[k].flags;
            s->pc[k].sx = lay->x[k] - hx;
            s->pc[k].sw = lay->x[k + 1] - lay->x[k];
            s->pc[k].head = (u8)(st != NULL ? st[k] : WCOL_H_FLAT);
        }
        if (k == arrow) {
            ax = arrow_x(t, lay, k, r->left) + sh;
            if (t->col[k].flags & WCOL_RIGHT)
                clip.left = max(clip.left, ax + WCOL_ARROWW + WCOL_ARROWGAP);
            else
                clip.right = min(clip.right, ax - WCOL_ARROWGAP);
        }
        if (k == arrow && s == NULL) {
            face = CreateSolidBrush(GetNearestColor(dc, GetBkColor(dc)));
            fill(dc, box, face, slot.left, slot.top, clip.left, slot.bottom);
            fill(dc, box, face, clip.right, slot.top, slot.right,
                 slot.bottom);
            DeleteObject(face);
        }
        text_out(dc, r->left + wcol_x(t, lay, k, ind, w) + sh,
                 r->top + (r->bottom - r->top - lay->lineh) / 2 + sh,
                 ETO_CLIPPED | ETO_OPAQUE, &clip, ansi + at, n, k, box->left);
        slot.left = slot.right;
    }
    if (st != NULL && s == NULL)
        edges(dc, box, r, t, lay, st, arrow);
}

void wcol_draw(HDC dc, const RECT FAR *box, const char *text,
               const wcol_tab *t, const wcol_lay *lay, int hx)
{
    draw(dc, box, text, t, lay, hx, NULL, -1);
}

void wcol_heads(HDC dc, const RECT FAR *box, const char *text,
                const wcol_tab *t, const wcol_lay *lay, int hx,
                u8 norders, u8 order, int press)
{
    u8 st[WCOL_MAX];
    int arrow;
    int o;
    int k;

    arrow = -1;
    for (k = 0; k < (t != NULL ? t->n : 1); k++) {
        o = t != NULL ? WCOL_ORDOF(t->col[k].flags) : -1;
        st[k] = (u8)(o < 0 || o >= norders ? WCOL_H_FLAT :
                     k == press ? WCOL_H_PRESS :
                     o == order ? WCOL_H_CUR : WCOL_H_BTN);
        if (st[k] != WCOL_H_FLAT && o == order)
            arrow = k;
    }
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    draw(dc, box, text, t, lay, hx, st, arrow);
}

COLORREF wcol_unlike(HDC dc, int sys)
{
    COLORREF face;
    COLORREF c;

    face = GetNearestColor(dc, GetSysColor(COLOR_BTNFACE));
    c = GetNearestColor(dc, GetSysColor(sys));
    if (c == face && sys != COLOR_BTNSHADOW)
        c = GetNearestColor(dc, GetSysColor(COLOR_BTNSHADOW));
    return c == face ? GetSysColor(COLOR_WINDOWFRAME) : c;
}

void wcol_item(const DRAWITEMSTRUCT FAR *d, const wcol_tab *t,
               const wcol_lay *lay, COLORREF text, int hx)
{
    char row[LV_ROWTEXT + 1];
    int sel;

    if (d->itemAction == ODA_FOCUS) {
        DrawFocusRect(d->hDC, &d->rcItem);
        return;
    }
    if (d->itemID == (UINT)-1)
        return;
    SendMessage(d->hwndItem, LB_GETTEXT, d->itemID, (LPARAM)(LPSTR)row);
    sel = (d->itemState & ODS_SELECTED) != 0;
    SetTextColor(d->hDC, sel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text);
    SetBkColor(d->hDC, GetSysColor(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    wcol_draw(d->hDC, &d->rcItem, row, t, lay, hx);
    if (d->itemState & ODS_FOCUS)
        DrawFocusRect(d->hDC, &d->rcItem);
}

void wcol_watch(wcol_seen *s)
{
    wcol_watched = s;
    if (s != NULL)
        s->n = 0;
}
