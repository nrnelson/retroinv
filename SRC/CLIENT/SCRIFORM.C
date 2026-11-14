/*
 * SCRIFORM.C - the item form's table and its first values (SCRIFORM.H).
 *
 * Compile-time checks here are pairs of declarations of one array, the
 * second of size 2 when the condition fails: they then disagree, which
 * every compiler refuses.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "MSG.H"
#include "TUI.H"
#include "FMODEL.H"
#include "FORM.H"
#include "SCRSHR.H"
#include "ITEMLS.H"
#include "SCRSFORM.H"
#include "SCRNFORM.H"
#include "SCRIFORM.H"
#ifdef COMPAT_WIN16
#include "INTL.H"

/* INVWIN's number forms (WROWS.C, whose header needs windows.h). */
extern intl_fmt wrow_intl;
#endif

/* ITEM GET's counts that HELD words, in the order of their kinds
   (SCR_K_ of SCRNFORM.H) and ATTACHED last, kept by scr_item_kline in
   si_held until scr_item_loaded, which ITEM GET's caller calls next,
   words them. */
#define HELD_N          4
/* The line scr_item_loaded words them into, cut to SCR_HELDW cells. */
#define HELD_LINEW      (2 * SCR_HELDW)
static const char k_held[] = "ON_LOAN RESERVED IN_USE ATTACHED";
static const char k_zero[] = "0";
static u32 si_held[HELD_N];

/* The cells of a count as HELD shows it: under INVWIN with the Control
   Panel's thousands separators, as its Quantity has them. */
#ifdef COMPAT_WIN16
#define HELD_NUMW       INTL_NUMW(TXT_U32LEN, 0)
#else
#define HELD_NUMW       TXT_U32LEN
#endif

extern char item_fits_form[1];
extern char item_fits_form[(LINK_DESCRLEN <= FORM_MAXVAL &&
                            SCR_PARENTW <= FORM_MAXVAL &&
                            SCR_V1 + SCR_LF_PATHW < VID_COLS - 2 &&
                            SCR_V1 + LINK_LOCLEN < SCR_C2) ? 1 : 2];

/* The names of STATUS's, COND's and WORKS' choices lie within a u8
   after their labels (form_field's dec). */
extern char item_names_reach[1];
extern char item_names_reach[(MS_C_A > MI_L_STATUS &&
                              MS_C_A - MI_L_STATUS <= 0xFF &&
                              MS_C_NEW > MI_L_COND &&
                              MS_C_NEW - MI_L_COND <= 0xFF &&
                              MS_C_WQ > MI_L_WORKS &&
                              MS_C_WQ - MI_L_WORKS <= 0xFF) ? 1 : 2];

/* On the grid of SCRSHR.H. */
const form_field scr_item_fields[] = {
    { itl_k_itemid, MI_L_ID,      0, SCR_C1, SCR_V1, FT_NUM, LINK_IDLEN, 0,
      FF_RO | FF_ID, 0, NULL },
    { itl_k_cat,    MI_L_CAT,     0, SCR_C2, SCR_V2, FT_CODE, LINK_CATLEN, 0,
      FF_REQ | FF_UPPER, 0, NULL },
    { itl_k_status, MI_L_STATUS,  0, SCR_C3, SCR_V3, FT_CHOICE,
      LINK_STATUSLEN, MS_C_A - MI_L_STATUS, FF_REQ | FF_UPPER, 0, "A D" },
    { "ITEMTYPE",   MI_L_TYPE,    1, SCR_C1, SCR_V1, FT_CHAR, LINK_TYPELEN, 0,
      0, 0, NULL },
    { itl_k_cond,   MI_L_COND,    1, SCR_C3, SCR_V3, FT_CHOICE, LINK_CONDLEN,
      MS_C_NEW - MI_L_COND, FF_UPPER, 0, "NEW EXC GD FR PR" },
    { itl_k_mfr,    MI_L_MFR,     2, SCR_C1, SCR_V1, FT_CHAR, LINK_MFRLEN, 0,
      0, 0, NULL },
    { itl_k_model,  MI_L_MODEL,   3, SCR_C1, SCR_V1, FT_CHAR, LINK_MFRLEN, 0,
      0, 0, NULL },
    { itl_k_serial, MI_L_SERIAL,  4, SCR_C1, SCR_V1, FT_CHAR, LINK_SERIALLEN,
      0, 0, 0, NULL },
    { "DESCR",      MI_L_DESCR,   5, SCR_C1, SCR_V1, FT_CHAR, LINK_DESCRLEN,
      0, 0, 0, NULL },
    { itl_k_works,  MI_L_WORKS,   6, SCR_C1, SCR_V1, FT_CHOICE, LINK_WORKSLEN,
      MS_C_WQ - MI_L_WORKS, FF_REQ | FF_UPPER, 0, "? Y I N" },
    { itl_k_qty,    MI_L_QTY,     6, SCR_C2, SCR_V2, FT_NUM, LINK_QTYLEN, 0,
      FF_NONNEG, 0, NULL },
    { "HELD",       MI_L_WHERE,   6, SCR_HELDC, SCR_HELDC, FT_CHAR, SCR_HELDW,
      0, FF_LOCAL | FF_RO, 0, NULL },
    { "LOC_ID",     MI_L_LOC,     7, SCR_C1, SCR_V1, FT_CODE, LINK_LOCLEN, 0,
      FF_UPPER, 0, NULL },
    { "PARENT_ID",  MI_L_PARENT,  7, SCR_C2, SCR_V2, FT_CHAR, SCR_PARENTW,
      0, FF_RO, 0, NULL },
    { "WHERE",      MI_L_WHERE,   8, SCR_C1, SCR_V1, FT_CHAR, SCR_LF_PATHW,
      0, FF_LOCAL | FF_RO, 0, NULL },
    { "ACQ_DATE",   MI_L_ACQ,     9, SCR_C1, SCR_V1, FT_DATE, FORM_DATEW, 0,
      0, 0, NULL },
    { "ACQ_SRC",    MI_L_SRC,     9, SCR_C2, SCR_V2, FT_CHAR, LINK_ACQSRCLEN,
      0, 0, 0, NULL },
    { "ACQ_PRICE",  MI_L_PRICE,  10, SCR_C1, SCR_V1, FT_NUM, LINK_PRICELEN,
      LINK_PRICEDEC, FF_NONNEG, 0, NULL },
    { "CUR_VALUE",  MI_L_VALUE,  10, SCR_C2, SCR_V2, FT_NUM, LINK_PRICELEN,
      LINK_PRICEDEC, FF_NONNEG, 0, NULL },
    { "TAGS",       MI_L_TAGS,   11, SCR_C1, SCR_V1, FT_CODE, LINK_DESCRLEN,
      0, 0, 0, NULL },
    { "CREATED",    MI_L_CREATED, 12, SCR_C1, SCR_V1, FT_DATE, FORM_DATEW, 0,
      FF_RO, 0, NULL },
    { "UPDATED",    MI_L_UPDATED, 12, SCR_C2, SCR_V2, FT_DATE, FORM_DATEW, 0,
      FF_RO, 0, NULL },
    { "NOTES",      MI_L_NOTES,  13, SCR_C1, SCR_V1, FT_MEMO, FORM_MEMOW, 0,
      0, FORM_MEMODISP, NULL }
};

extern char item_f_count[1];
extern char item_f_count[(sizeof scr_item_fields / sizeof scr_item_fields[0] ==
                          SCR_IF_COUNT) ? 1 : 2];

const form_def scr_item_def = {
    MI_NEWTITLE, "ITEM", TUI_TOP, 0, SCR_ITEMFORM_H, VID_COLS, SCR_IF_COUNT,
    scr_item_fields
};

const scr_locf scr_item_loc = {
    &scr_item_fields[SCR_IF_LOC], &scr_item_fields[SCR_IF_WHERE], MO_NOLOC, 0
};

void scr_item_preset(form *f)
{
    form_load(f, itl_k_works, "?");
    form_load(f, itl_k_qty, "1");
    form_load(f, itl_k_status, "A");
}

/* The number of digits d, after leading blanks (a quantity or count of
   ITEM GET). */
static u32 held_num(const char *d)
{
    u32 v;

    while (*d == ' ')
        d++;
    for (v = 0; TXT_ISDIGIT(*d); d++)
        v = v * 10 + (u32)(*d - '0');
    return v;
}

int scr_item_kline(void *arg, const rq_line *ln)
{
    int i;

    form_kline(arg, ln);
    if (ln->kind != 'K' || ln->n < 2)
        return 0;
    /* ITEM_ID, the first K line, starts the counts again. */
    if (strcmp(ln->v[0], itl_k_itemid) == 0)
        memset(si_held, 0, sizeof si_held);
    i = txt_word_num(k_held, ln->v[0]);
    if (i >= 0)
        si_held[i] = held_num(ln->v[1]);
    return 0;
}

/* Count v into c (HELD_NUMW + 1 bytes) as HELD shows it. */
#ifdef COMPAT_WIN16
static void held_count(char *c, u32 v)
{
    char d[TXT_U32LEN + 1];

    txt_u32(d, v);
    intl_num(&wrow_intl, c, d, HELD_NUMW);
}
#else
#define held_count(c, v) txt_u32(c, v)
#endif

/* Append text a to t (HELD_LINEW + 1 bytes), after two blanks unless t
   is empty, cut to fit. */
static void held_add(char *t, const char *a)
{
    u16 n;

    n = (u16)strlen(t);
    if (n > 0 && n + 2 < HELD_LINEW) {
        t[n++] = ' ';
        t[n++] = ' ';
    }
    txt_copy(t + n, a, (u16)(HELD_LINEW + 1 - n));
}

void scr_item_loaded(form *f)
{
    char t[HELD_LINEW + 1];
    char c[HELD_NUMW + 1];
    char w[MSG_MAXLEN + 1];
    u32 left;
    u16 i;
    u16 n;

    /* LOC_ID and PARENT_ID, the one after it. */
    for (i = SCR_IF_LOC; i <= SCR_IF_PARENT; i++)
        if (strcmp(form_get(f, scr_item_fields[i].name), k_zero) == 0)
            form_load(f, scr_item_fields[i].name, "");
    t[0] = '\0';
    left = held_num(form_get(f, itl_k_qty));
    for (i = 0; i < SCR_NKINDS; i++) {
        if (si_held[i] != 0) {
            n = msg_get((u16)(MH_K_NAME + i), w, (u16)(sizeof w - sizeof c));
            w[n++] = ' ';
            held_count(w + n, si_held[i]);
            held_add(t, w);
            left = si_held[i] < left ? left - si_held[i] : 0;
        }
    }
    if (left > 0 && strcmp(form_get(f, itl_k_status), "A") == 0 &&
        form_get(f, scr_item_fields[SCR_IF_PARENT].name)[0] == '\0') {
        held_count(c, left);
        held_add(t, msg_sub(MH_AVAIL, c, NULL));
    }
    if (si_held[SCR_NKINDS] != 0) {
        held_count(c, si_held[SCR_NKINDS]);
        held_add(t, msg_sub(MH_HOSTS, c, NULL));
    }
    if (strlen(t) > SCR_HELDW) {
        t[SCR_HELDW - 1] = MSG_CUTMARK;
        t[SCR_HELDW] = '\0';
    }
    form_load(f, scr_item_fields[SCR_IF_HELD].name, t);
}

/* The parent's row is matched by its ID, not taken as the last row: a
   response cut short by K ERROR= ends before it, after the rows of the
   ancestors above it. */
int scr_item_parent(void *arg, const rq_line *ln)
{
    char v[SCR_PARENTW + 1];
    const char *par;
    u16 i;
    u16 n;

    par = form_get((form *)arg, scr_item_fields[SCR_IF_PARENT].name);
    if (ln->kind != 'R' || ln->n <= SCR_AP_MODEL || par == NULL ||
        strcmp(ln->v[SCR_AP_ID], par) != 0)
        return 0;
    txt_copy(v, par, (u16)sizeof v);
    for (i = SCR_AP_MFR; i <= SCR_AP_MODEL; i++) {
        n = (u16)strlen(v);
        if (ln->v[i][0] != '\0' && n + 1 < (u16)sizeof v) {
            v[n] = ' ';
            txt_copy(v + n + 1, ln->v[i], (u16)(sizeof v - n - 1));
        }
    }
    form_load((form *)arg, scr_item_fields[SCR_IF_PARENT].name, v);
    return 0;
}
